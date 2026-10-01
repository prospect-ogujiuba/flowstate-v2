// Planner: natural-language request + musical context -> score IR, via a model backend.
// Validation errors are fed back for repair. This is the seed of the v2 agent service.
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Score, IR_ID, type Part } from "@flowstate/schema";
import { PartStream, type Unplayable } from "./part-stream.ts";
import { validateScore } from "./validate.ts";
import { backendFor, selectionFromEnv, type Backend, type Completion, type Turn } from "./backends.ts";

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

The IR specification follows. Follow it exactly: step strings must have exactly (beats per bar x grid) steps per bar, and every key is present, with null for block fields that do not apply.

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
  /** First attempt: when the first part became playable (head and part both valid), while the score streamed. */
  firstPartMs: number | null;
  /** First attempt: streamed parts that were not playable as they landed, and why. */
  unplayable: Unplayable[];
  /** The model's raw reply per attempt, for diagnosis. */
  replies: string[];
}

export interface PlanOptions {
  /** Each playable part of the first attempt, as soon as it lands. A repair replaces the whole score. */
  onPart?: (part: Part, atMs: number) => void;
  signal?: AbortSignal;
}

// No constrained decoding: the IR schema compiles to a grammar larger than the API accepts.
// The model writes JSON text; strict Zod parsing plus semantic validation drive up to two repair passes.
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
    `- length: ${c.bars} bars`,
    `- lanes to write: ${c.lanes.join(", ")}`,
    `- style tags: ${c.style.join(", ") || "none"}`,
  ].join("\n");
}

let envBackend: Backend | undefined;

/** The backend from the environment (selectionFromEnv), for callers that don't choose one per request. */
export function defaultBackend(): Backend {
  return (envBackend ??= backendFor(selectionFromEnv()));
}

export async function planScore(req: PlanRequest, backend: Backend = defaultBackend(), options: PlanOptions = {}): Promise<PlanResult> {
  const started = Date.now();
  const usage = { inputTokens: 0, outputTokens: 0, cacheReadTokens: 0 };
  const turns: Turn[] = [{ role: "user", text: userMessage(req) }];
  let lastErrors: string[] = [];
  let first: Pick<Completion, "firstTokenMs" | "firstTextMs"> = { firstTokenMs: null, firstTextMs: null };
  let firstPartMs: number | null = null;
  let unplayable: Unplayable[] = [];
  const replies: string[] = [];

  for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    // Only the first attempt streams parts out: a repair replaces the whole score.
    const parts = attempt === 1
      ? new PartStream((part) => {
          const at = Date.now() - started;
          firstPartMs ??= at;
          options.onPart?.(part, at);
        })
      : null;
    const completion = await backend.complete(SYSTEM_PROMPT, turns, {
      ...(parts ? { onText: (d: string) => parts.push(d) } : {}),
      ...(options.signal ? { signal: options.signal } : {}),
    });
    if (parts) unplayable = parts.unplayable;
    const { text } = completion;
    replies.push(text);
    if (attempt === 1) first = { firstTokenMs: completion.firstTokenMs, firstTextMs: completion.firstTextMs };
    usage.inputTokens += completion.inputTokens;
    usage.outputTokens += completion.outputTokens;
    usage.cacheReadTokens += completion.cacheReadTokens;

    let score: Score | null = null;
    try {
      const parsed = Score.safeParse(extractJson(text));
      if (parsed.success) score = parsed.data;
      else lastErrors = parsed.error.issues.map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`);
    } catch (err) {
      lastErrors = [`response was not valid JSON: ${String(err)}`];
    }
    if (score) {
      lastErrors = [...validateScore(score), ...constraintMismatches(score, req)];
      if (lastErrors.length === 0 || attempt === MAX_ATTEMPTS)
        return { score, attempts: attempt, validationErrors: lastErrors, usage, latencyMs: Date.now() - started, ...first, firstPartMs, unplayable, replies };
    } else if (attempt === MAX_ATTEMPTS) {
      throw new Error(`planner output invalid after ${attempt} attempts: ${lastErrors.slice(0, 5).join("; ")}`);
    }

    turns.push({ role: "assistant", text });
    turns.push({
      role: "user",
      text: `The score has problems. Return the complete corrected score as a single JSON object, keeping everything else the same:\n- ${lastErrors.slice(0, 40).join("\n- ")}`,
    });
  }
  throw new Error("unreachable");
}

function constraintMismatches(score: Score, req: PlanRequest): string[] {
  const c = req.controls;
  const ctx = score.context;
  const errors: string[] = [];
  if (score.ir !== IR_ID) errors.push(`ir must be ${IR_ID}`);
  if (ctx.bars !== c.bars) errors.push(`context.bars must be ${c.bars}`);
  if (ctx.tonic !== c.tonic || ctx.mode !== c.mode) errors.push(`key must be ${c.tonic} ${c.mode}`);
  if (ctx.meterNumerator !== c.meterNumerator || ctx.meterDenominator !== c.meterDenominator)
    errors.push(`meter must be ${c.meterNumerator}/${c.meterDenominator}`);
  const roles = new Set(score.parts.map((p) => p.role));
  for (const lane of c.lanes) if (!roles.has(lane as never)) errors.push(`missing requested lane '${lane}'`);
  return errors;
}
