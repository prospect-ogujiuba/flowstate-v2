// Planner: natural-language request + musical context -> score IR, via a model backend.
// Validation errors are fed back for repair. This is the seed of the v2 agent service.
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { GROOVES, Score, IR_ID, type LibraryClip, type Part } from "@flowstate/schema";
import { examplesText } from "./examples.ts";
import { checkPart, partLabel, PartStream, type Head, type Role, type Unplayable } from "./part-stream.ts";
import { validateScore } from "./validate.ts";
import { backendFor, selectionFromEnv, type Backend, type Completion, type Turn } from "./backends.ts";
import { FlowstateError } from "./errors.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const IR_SPEC = readFileSync(path.join(here, "..", "..", "docs", "ir-spec.md"), "utf8");


const SYSTEM_PROMPT = `You are Flowstate's composer: a session musician and producer who writes parts for other producers inside their DAW.

You answer every request with one score in the Flowstate score IR. A deterministic engine performs your score: it voices chords, maps motif degrees to pitches, applies swing and humanization, and enforces key, length and range. You make the musical decisions; the engine makes them sound played.

What good looks like:
- Idiomatic for the style. A trap beat, a gospel progression and a house groove should each be unmistakable.
- Parts that listen to each other: bass locks with the kick, chord rhythm leaves room for the melody, drums breathe with the phrase.
- Development over repetition. State an idea, then answer, vary or build it. Use form energy, drum variations, fills at phrase ends, and motif transforms rather than copy-pasting identical bars.
- Harmony with intent: voice-leading-friendly progressions, colour tones and borrowed chords where the style wants them, cadences that land on phrase boundaries.
- A melody someone would hum: a clear motif with a memorable rhythm, contour and space.

Honour the request's explicit constraints (key, mode, bars, meter, tempo, lanes) exactly. Where the request is vague, choose boldly and musically rather than blandly. Only include the lanes requested.

The IR specification follows. Follow it exactly: step strings must have exactly (beats per bar x grid) steps per bar. Every key is present, except block fields that do not apply to the part's role: leave those out.

Reply with the score as a single JSON object and nothing else.

${IR_SPEC}`;

export interface PlanRequest {
  prompt: string;
  controls: {
    tonic: string;
    mode: string;
    bars: number;
    tempo: number;
    meterNumerator: number;
    meterDenominator: number;
    style: string[];
    lanes: string[];
  };
  /** Library clips to show as style examples (examples.ts); none by default. */
  examples?: LibraryClip[];
}

export interface PlanResult {
  score: Score;
  attempts: number;
  validationErrors: string[];
  usage: { inputTokens: number; outputTokens: number; cacheReadTokens: number };
  /** The full plan, including any repair passes. */
  latencyMs: number;
  /** First attempt: first streamed token (thinking or text), and first answer text. */
  firstTokenMs: number | null;
  firstTextMs: number | null;
  /** When the first part became playable (head and part both valid), while the score streamed. */
  firstPartMs: number | null;
  /** First attempt: streamed parts that were not playable as they landed, and why. */
  unplayable: Unplayable[];
  /** The model's raw reply per attempt, for diagnosis. */
  replies: string[];
}

export interface PlanOptions {
  /**
   * Each playable part as soon as it lands, from the first reply and from parts-only repairs. A later call
   * with the same part id replaces the earlier part.
   */
  onPart?: (part: Part, atMs: number) => void;
  /**
   * The plan's head (a score with no parts) once it passes the checks, before any part. A full rewrite may
   * report a new head, but only while no part has been reported.
   */
  onHead?: (score: Score) => void;
  /** A part has started streaming. It may still turn out unplayable and come back in a repair. */
  onPartStarted?: (partId: string, role: Role) => void;
  signal?: AbortSignal;
}

// No constrained decoding: the IR schema compiles to a grammar larger than the API accepts.
// The model writes JSON text; strict Zod parsing plus semantic validation drive up to two repair passes,
// of only the broken parts when the head is sound.
const MAX_ATTEMPTS = 3;

function extractJson(text: string): unknown {
  const fenced = text.match(/```(?:json)?\s*([\s\S]*?)```/);
  const body = (fenced ? fenced[1]! : text).trim();
  const start = body.indexOf("{");
  const end = body.lastIndexOf("}");
  if (start < 0 || end < start) throw new SyntaxError("no JSON object in response");
  return JSON.parse(body.slice(start, end + 1));
}

function userMessage(req: PlanRequest): string {
  const c = req.controls;
  return [
    `Request: ${req.prompt}`,
    "",
    "Session context (hard constraints):",
    `- key: ${c.tonic} ${c.mode}`,
    `- meter: ${c.meterNumerator}/${c.meterDenominator}, tempo ${c.tempo} BPM`,
    `- one bar = ${c.meterNumerator} beats, so every step-string bar has ${c.meterNumerator} × grid steps ` +
      `(grid 2: ${c.meterNumerator * 2}, grid 3: ${c.meterNumerator * 3}, grid 4: ${c.meterNumerator * 4})`,
    `- length: ${c.bars} bars`,
    `- lanes to write: ${c.lanes.join(", ")} (one part per lane, with the lane name as its role)`,
    `- style tags: ${c.style.join(", ") || "none"}`,
    ...grooveHint(c),
    "",
    `The session fixes the key, meter, tempo and length, so in "context" write only "swing" and "style".`,
    ...(req.examples?.length ? ["", examplesText(req.examples)] : []),
  ].join("\n");
}

/**
 * Names the grooves that match the request's style tags and meter, so the model reaches for the idiomatic
 * pattern instead of writing its own (models tended to skip e.g. `trap` on a trap prompt).
 */
function grooveHint(c: PlanRequest["controls"]): string[] {
  const tags = c.style.map((t) => t.toLowerCase());
  const matches = (Object.entries(GROOVES) as [string, (typeof GROOVES)[keyof typeof GROOVES]][])
    .filter(([name, g]) => g.meter[0] === c.meterNumerator && g.meter[1] === c.meterDenominator)
    .filter(([name, g]) => tags.some((t) => name === t.replace(/-/g, "_") || g.description.toLowerCase().startsWith(t.replace(/-/g, " "))))
    .map(([name]) => name);
  return c.lanes.includes("drums") && matches.length ? [`- drum grooves for these styles: ${matches.join(", ")} (see Grooves)`] : [];
}

/**
 * The model leaves out the context fields the session fixes; they come from the request instead. A value
 * the model wrote anyway is overridden, so a key or length the model got wrong never needs a repair.
 */
function completeHead(raw: Record<string, unknown>, req: PlanRequest): Head {
  const c = req.controls;
  const written = (raw.context && typeof raw.context === "object" ? raw.context : {}) as Record<string, unknown>;
  return {
    ...raw,
    ir: raw.ir ?? IR_ID,
    context: {
      ...written,
      tempo: c.tempo,
      meterNumerator: c.meterNumerator,
      meterDenominator: c.meterDenominator,
      tonic: c.tonic,
      mode: c.mode,
      bars: c.bars,
      style: written.style ?? c.style,
    },
  } as Head;
}

let envBackend: Backend | undefined;

/** The backend from the environment (selectionFromEnv), for callers that don't choose one per request. */
export function defaultBackend(): Backend {
  return (envBackend ??= backendFor(selectionFromEnv()));
}

/** The model's score so far: a head and raw parts. Part repairs replace or add parts by id. */
interface Draft { head: Head; parts: unknown[] }

type Review =
  | { ok: true; score: Score }
  | { ok: false; scope: "score"; errors: string[]; score: Score | null }
  | { ok: false; scope: "parts"; errors: string[]; score: Score | null; badParts: string[] };

// Head problems need the whole score rewritten (every part depends on the head). Problems that sit in
// single parts, or a missing lane, need only those parts.
function review(draft: Draft, req: PlanRequest): Review {
  const assembled = Score.safeParse({ ...draft.head, parts: draft.parts });
  const score = assembled.success ? assembled.data : null;
  const bare = Score.safeParse({ ...draft.head, parts: [] });
  if (!bare.success)
    return { ok: false, scope: "score", score, errors: bare.error.issues.map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`) };
  const headErrors = [...validateScore(bare.data), ...headMismatches(bare.data, req)];
  if (headErrors.length > 0) return { ok: false, scope: "score", score, errors: headErrors };

  const errors: string[] = [];
  const badParts: string[] = [];
  for (const value of draft.parts) {
    const checked = checkPart(draft.head, value);
    if ("errors" in checked) {
      badParts.push(partLabel(value));
      errors.push(...checked.errors.map((e) => (e.startsWith("part ") ? e : `part ${partLabel(value)}: ${e}`)));
    }
  }
  // Across parts: duplicate ids and missing lanes.
  if (score) errors.push(...validateScore(score).filter((e) => e.startsWith("duplicate part id")));
  const roles = new Set(draft.parts.map((p) => (p as { role?: unknown })?.role));
  for (const lane of req.controls.lanes) if (!roles.has(lane)) errors.push(`missing requested lane '${lane}': add a part for it`);
  if (errors.length === 0 && score) return { ok: true, score };
  return { ok: false, scope: "parts", score, errors, badParts };
}

function draftFrom(text: string, req: PlanRequest): Draft {
  const raw = extractJson(text);
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) throw new SyntaxError("the reply is not a JSON object");
  const { parts, ...head } = raw as Record<string, unknown>;
  return { head: completeHead(head, req), parts: Array.isArray(parts) ? parts : [] };
}

// A parts-only repair: each returned part replaces the draft's part with the same id, else the first broken
// part with the same role; anything else is added.
function mergeParts(draft: Draft, text: string, badParts: string[]): Draft {
  const raw = extractJson(text) as { parts?: unknown };
  if (!Array.isArray(raw?.parts)) throw new SyntaxError("the repair reply has no parts array");
  const parts = [...draft.parts];
  const open = new Set(badParts);
  for (const fixed of raw.parts) {
    const { id, role } = (fixed ?? {}) as { id?: unknown; role?: unknown };
    let i = parts.findIndex((p) => (p as { id?: unknown })?.id === id);
    if (i < 0) i = parts.findIndex((p) => open.has(partLabel(p)) && (p as { role?: unknown })?.role === role);
    if (i >= 0) {
      open.delete(partLabel(parts[i]));
      parts[i] = fixed;
    } else parts.push(fixed);
  }
  return { head: draft.head, parts };
}

export async function planScore(req: PlanRequest, backend: Backend = defaultBackend(), options: PlanOptions = {}): Promise<PlanResult> {
  const started = Date.now();
  const usage = { inputTokens: 0, outputTokens: 0, cacheReadTokens: 0 };
  const turns: Turn[] = [{ role: "user", text: userMessage(req) }];
  let first: Pick<Completion, "firstTokenMs" | "firstTextMs"> = { firstTokenMs: null, firstTextMs: null };
  let firstPartMs: number | null = null;
  let unplayable: Unplayable[] = [];
  const replies: string[] = [];
  let draft: Draft | null = null;
  // What the next reply is: the whole score, or only the listed parts (with the draft's head).
  let next: { scope: "score" } | { scope: "parts"; badParts: string[] } = { scope: "score" };
  let lastErrors: string[] = [];

  const reportPart = (part: Part) => {
    const at = Date.now() - started;
    firstPartMs ??= at;
    options.onPart?.(part, at);
  };

  for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    // A parts-only repair keeps the head, so its parts stream too. A full rewrite may change the head, so it
    // streams only while nothing has been reported yet: earlier parts could clash with the new head.
    const headCheck = (head: Head) => headMismatches({ ...head, parts: [] }, req);
    const { onHead, onPartStarted } = options;
    const stream = next.scope === "parts" && draft
      ? new PartStream(reportPart, { knownHead: draft.head, ...(onPartStarted ? { onPartStarted } : {}) })
      : firstPartMs === null
        ? new PartStream(reportPart, {
          normalizeHead: (raw) => completeHead(raw, req), headCheck,
          ...(onHead ? { onHead } : {}), ...(onPartStarted ? { onPartStarted } : {}),
        })
        : null;
    const completion = await backend.complete(SYSTEM_PROMPT, turns, {
      ...(stream ? { onText: (d: string) => stream.push(d) } : {}),
      ...(options.signal ? { signal: options.signal } : {}),
    });
    const { text } = completion;
    replies.push(text);
    if (attempt === 1) {
      first = { firstTokenMs: completion.firstTokenMs, firstTextMs: completion.firstTextMs };
      unplayable = stream!.unplayable;
    }
    usage.inputTokens += completion.inputTokens;
    usage.outputTokens += completion.outputTokens;
    usage.cacheReadTokens += completion.cacheReadTokens;

    let result: Review;
    try {
      draft = next.scope === "parts" && draft ? mergeParts(draft, text, next.badParts) : draftFrom(text, req);
      result = review(draft, req);
    } catch (err) {
      // Unreadable reply: ask again for the same thing.
      lastErrors = [`response was not valid JSON: ${String(err)}`];
      if (attempt === MAX_ATTEMPTS) throw new FlowstateError("invalid_score", `planner output invalid after ${attempt} attempts: ${lastErrors.join("; ")}`);
      turns.push({ role: "assistant", text }, { role: "user", text: repairRequest(next, lastErrors) });
      continue;
    }

    const done = (score: Score, errors: string[]): PlanResult => ({
      score, attempts: attempt, validationErrors: errors, usage, latencyMs: Date.now() - started,
      ...first, firstPartMs, unplayable, replies,
    });
    if (result.ok) return done(result.score, []);
    lastErrors = result.errors;
    if (attempt === MAX_ATTEMPTS) {
      if (result.score) return done(result.score, result.errors);
      throw new FlowstateError("invalid_score", `planner output invalid after ${attempt} attempts: ${result.errors.slice(0, 5).join("; ")}`);
    }
    next = result.scope === "parts" ? { scope: "parts", badParts: result.badParts } : { scope: "score" };
    turns.push({ role: "assistant", text }, { role: "user", text: repairRequest(next, result.errors) });
  }
  throw new Error("unreachable");
}

function repairRequest(next: { scope: "score" } | { scope: "parts"; badParts: string[] }, errors: string[]): string {
  const list = `- ${errors.slice(0, 40).join("\n- ")}`;
  if (next.scope === "score")
    return `The score has problems. Return the complete corrected score as a single JSON object, keeping everything else the same:\n${list}`;
  return [
    "Some parts have problems. The head (context, form, harmony, motifs) and the other parts are fine and stay as they are.",
    `Return only the corrected or missing parts, each complete and keeping its id, as a single JSON object {"parts": [...]}:`,
    list,
  ].join("\n");
}

function headMismatches(score: Score, req: PlanRequest): string[] {
  const c = req.controls;
  const ctx = score.context;
  const errors: string[] = [];
  if (score.ir !== IR_ID) errors.push(`ir must be ${IR_ID}`);
  if (ctx.bars !== c.bars) errors.push(`context.bars must be ${c.bars}`);
  if (ctx.tonic !== c.tonic || ctx.mode !== c.mode) errors.push(`key must be ${c.tonic} ${c.mode}`);
  if (ctx.meterNumerator !== c.meterNumerator || ctx.meterDenominator !== c.meterDenominator)
    errors.push(`meter must be ${c.meterNumerator}/${c.meterDenominator}`);
  return errors;
}
