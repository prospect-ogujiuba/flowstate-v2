// Editor: the current score + a request -> a patch from the model -> the edited score (POST /v1/edit).
// Three kinds share the path: `edit` changes the score as asked, `vary` rewrites one part keeping its idea, and
// `addPart` writes one new part. The model replies with a patch, not a whole score:
//   {"message"?, "title"?, "harmony"?, "motifs"?, "remove"?, "parts"?}  (parts last, so they stream)
// The service applies it to the request's score and enforces the locks: only the request's parts may change,
// harmony only when no locked part plays from it, and a motif a locked part uses never. The context is the
// session's and never changes. Every reply is a whole patch against the original score; a bad one gets a repair
// request with the problems, through the same conversation loop as plans (backends.ts `converse`).
import { IR_ID, Score, type Part } from "@flowstate/schema";
import type { HistoryStep } from "@flowstate/schema";
import { converse, type Backend, type Completion, type Turn } from "./backends.ts";
import type { CapabilityContext } from "./capabilities/api.ts";
import { createLoadout } from "./capabilities/registry.ts";
import { FlowstateError } from "./errors.ts";
import { PartStream, partLabel, type Head, type Role, type Unplayable } from "./part-stream.ts";
import { defaultBackend, defaultHost, extractJson, MAX_ATTEMPTS, SYSTEM_PROMPT } from "./planner.ts";
import { validateScore } from "./validate.ts";

export type EditKind = "edit" | "vary" | "addPart";

export interface EditRequest {
  kind: EditKind;
  prompt: string;
  score: Score;
  /** Parts the edit may change; null = every part. vary: exactly one; addPart: none. */
  partIds: string[] | null;
  /** addPart: the new part's role. */
  role: Role | null;
  /** The lineage path to the current node, oldest first (EditRequest.history). */
  history?: HistoryStep[];
  tools?: boolean;
}

/** Steps of an idea's history the model sees: enough for "less than that" or "back toward the first one". */
export const HISTORY_STEPS = 8;

/**
 * The lineage path as earlier turns of the conversation: what was asked at each step, and the note on what came
 * of it. The scores aren't repeated; the current one is in the request.
 */
export function historyTurns(steps: HistoryStep[], score?: Score): Turn[] {
  const name = (id: string) => {
    const part = score?.parts.find((p) => p.id === id);
    return part ? `${id} (${part.role})` : `${id} (since removed)`;
  };
  return steps.slice(-HISTORY_STEPS).flatMap((step): Turn[] => {
    const words = step.prompt.trim();
    const asked =
      step.kind === "initial" || step.kind === "regenerate" ? `New idea: ${words || "(no words: surprise me)"}`
      : step.kind === "edit" ? `Edit: ${words || "(no words)"}`
      : step.kind === "vary" ? `A variation of one part${words ? `: ${words}` : ""}`
      : step.kind === "sketch" ? "An instant sketch from the session's context"
      : step.kind === "library" ? `Started from a library clip${words ? `: ${words}` : ""}`
      : `Changed by hand in the plugin (${step.kind})${words ? `: ${words}` : ""}`;
    const note = step.note?.trim() || "(done)";
    const changed = step.changed.length ? ` [changed: ${step.changed.map(name).join(", ")}]` : "";
    return [{ role: "user", text: asked }, { role: "assistant", text: `${note}${changed}` }];
  });
}

export interface EditResult {
  /** The edited score, or null when the model answered with text only. */
  score: Score | null;
  message: string | null;
  /** Ids of the parts the edit replaced or added, and of those it removed. */
  changed: string[];
  removed: string[];
  /** Head changes: the harmony, and the ids of motifs replaced or added. */
  harmonyChanged: boolean;
  motifsChanged: string[];
  attempts: number;
  validationErrors: string[];
  usage: { inputTokens: number; outputTokens: number; cacheReadTokens: number };
  latencyMs: number;
  firstTokenMs: number | null;
  firstTextMs: number | null;
  firstPartMs: number | null;
  unplayable: Unplayable[];
  replies: string[];
  toolCalls: string[];
}

export interface EditOptions {
  /** The edited head (a score with no parts) once it passes the checks, before any part. */
  onHead?: (score: Score) => void;
  onPartStarted?: (partId: string, role: Role) => void;
  /** Each changed or new part that is playable as it lands. */
  onPart?: (part: Part, atMs: number) => void;
  signal?: AbortSignal;
  host?: Pick<CapabilityContext, "catalog" | "analyzeClip">;
}

// Roles whose notes come from the harmony. Only drums don't, so harmony may change only when every locked part
// is drums.
const HARMONY_FREE_ROLES = new Set<Role>(["drums"]);

interface Rules {
  editable: Set<string>;
  locked: Part[];
  canAdd: boolean;
  canRemove: boolean;
  harmonyFree: boolean;
  /** Motifs a locked part plays, by id: they can't change. */
  lockedMotifs: Set<string>;
}

/** The request's rules, or `bad_request` for a request that can't be served. */
export function rulesFor(req: EditRequest): Rules {
  const ids = req.score.parts.map((p) => p.id);
  const unknown = (req.partIds ?? []).filter((id) => !ids.includes(id));
  if (unknown.length) throw new FlowstateError("bad_request", `partIds not in the score: ${unknown.join(", ")}`);
  if (req.kind === "vary" && req.partIds?.length !== 1) throw new FlowstateError("bad_request", "vary needs exactly one part in partIds");
  if (req.kind === "addPart" && !req.role) throw new FlowstateError("bad_request", "addPart needs a role");
  if (req.kind !== "addPart" && req.role) throw new FlowstateError("bad_request", "role is only for addPart");
  if (req.kind === "addPart" && req.partIds?.length) throw new FlowstateError("bad_request", "addPart changes no existing part: partIds must be empty");
  const editable = new Set(req.kind === "addPart" ? [] : (req.partIds ?? ids));
  if (req.kind === "edit" && editable.size === 0 && req.score.parts.length > 0 && req.partIds !== null)
    throw new FlowstateError("bad_request", "every part is locked: unlock one to edit");
  const locked = req.score.parts.filter((p) => !editable.has(p.id));
  return {
    editable,
    locked,
    canAdd: req.kind !== "vary",
    canRemove: req.kind === "edit",
    harmonyFree: req.kind === "edit" && locked.every((p) => HARMONY_FREE_ROLES.has(p.role)),
    lockedMotifs: new Set(locked.flatMap((p) => p.blocks.flatMap((b) => ("motif" in b && typeof b.motif === "string" ? [b.motif] : [])))),
  };
}

const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

/** The edited head: the original's, with the patch's title, harmony and motifs (replaced or added by id). */
function headFrom(raw: Record<string, unknown>, original: Score): Head {
  const { parts: _, ...head } = original;
  const motifs = [...head.motifs];
  for (const m of Array.isArray(raw.motifs) ? raw.motifs : []) {
    const i = motifs.findIndex((x) => x.id === (m as { id?: unknown })?.id);
    if (i >= 0) motifs[i] = m;
    else motifs.push(m);
  }
  return {
    ...head,
    ir: IR_ID,
    title: typeof raw.title === "string" && raw.title.trim() ? raw.title : head.title,
    harmony: raw.harmony === undefined ? head.harmony : (raw.harmony as Head["harmony"]),
    motifs,
  };
}

function headErrors(head: Head, original: Score, rules: Rules): string[] {
  const errors: string[] = [];
  if (!same(head.harmony, original.harmony) && !rules.harmonyFree) {
    const playing = rules.locked.filter((p) => !HARMONY_FREE_ROLES.has(p.role)).map((p) => p.id);
    errors.push(`harmony is fixed: leave it out (locked parts ${playing.join(", ")} play from it)`);
  }
  for (const id of rules.lockedMotifs) {
    const before = original.motifs.find((m) => m.id === id);
    const after = head.motifs.find((m) => m.id === id);
    if (!same(before, after)) errors.push(`motif ${id} is fixed: a locked part plays it; add a new motif id instead`);
  }
  return errors;
}

/** Why this part can't be in the patch, if it can't. */
function partErrors(part: Part, req: EditRequest, rules: Rules): string[] {
  const existing = req.score.parts.find((p) => p.id === part.id);
  if (existing) {
    if (!rules.editable.has(part.id)) return [`part ${part.id} is locked or not part of this request: leave it out`];
    if (req.kind === "vary" && part.role !== existing.role) return [`part ${part.id} must keep its role ${existing.role}`];
    return [];
  }
  if (!rules.canAdd) return [`part ${part.id} isn't in the score, and a variation adds no parts`];
  if (req.kind === "addPart" && part.role !== req.role) return [`the new part must have role ${req.role}`];
  return [];
}

interface Applied {
  score: Score | null;
  message: string | null;
  changed: string[];
  removed: string[];
  harmonyChanged: boolean;
  motifsChanged: string[];
  errors: string[];
}

/** Applies a patch to the request's score and checks it. A patch that changes nothing and has a message is an answer. */
export function applyPatch(text: string, req: EditRequest, rules: Rules): Applied {
  const raw = extractJson(text);
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) throw new SyntaxError("the reply is not a JSON object");
  const patch = raw as Record<string, unknown>;
  const message = typeof patch.message === "string" && patch.message.trim() ? patch.message.trim() : null;
  const head = headFrom(patch, req.score);
  const errors = headErrors(head, req.score, rules);
  const harmonyChanged = !same(head.harmony, req.score.harmony);
  const motifsChanged = head.motifs.filter((m) => !same(m, req.score.motifs.find((x) => x.id === m.id))).map((m) => m.id);

  const parts = [...req.score.parts];
  const changed: string[] = [];
  const added: Part[] = [];
  for (const value of Array.isArray(patch.parts) ? patch.parts : []) {
    const parsed = Score.shape.parts.element.safeParse(value);
    if (!parsed.success) {
      errors.push(`part ${partLabel(value)}: ${parsed.error.issues.slice(0, 3).map((i) => `${i.path.join(".")}: ${i.message}`).join("; ")}`);
      continue;
    }
    const part = parsed.data;
    const refused = partErrors(part, req, rules);
    if (refused.length) {
      errors.push(...refused);
      continue;
    }
    const i = parts.findIndex((p) => p.id === part.id);
    if (i >= 0) parts[i] = part;
    else {
      parts.push(part);
      added.push(part);
    }
    if (!changed.includes(part.id)) changed.push(part.id);
  }

  const removed: string[] = [];
  for (const id of Array.isArray(patch.remove) ? patch.remove : []) {
    if (typeof id !== "string" || !parts.some((p) => p.id === id)) continue;
    if (!rules.canRemove || !rules.editable.has(id)) {
      errors.push(`part ${id} can't be removed by this request`);
      continue;
    }
    parts.splice(parts.findIndex((p) => p.id === id), 1);
    removed.push(id);
  }

  if (req.kind === "addPart" && added.length !== 1) errors.push(`add exactly one new ${req.role} part (with a new id)`);
  if (req.kind === "vary" && !changed.includes(req.partIds![0]!)) errors.push(`return the varied part ${req.partIds![0]}, complete, with the same id`);

  const headChanged = !same(head, headFrom({}, req.score));
  if (!headChanged && changed.length === 0 && removed.length === 0 && errors.length === 0) {
    if (message && req.kind === "edit") return { score: null, message, changed, removed, harmonyChanged, motifsChanged, errors: [] };
    return { score: null, message, changed, removed, harmonyChanged, motifsChanged, errors: ["the patch changes nothing: return the changed parts"] };
  }
  if (parts.length === 0) errors.push("the edit would leave no parts");

  const score = Score.safeParse({ ...head, parts });
  if (!score.success) {
    errors.push(...score.error.issues.slice(0, 5).map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`));
    return { score: null, message, changed, removed, harmonyChanged, motifsChanged, errors };
  }
  errors.push(...validateScore(score.data));
  return { score: score.data, message, changed, removed, harmonyChanged, motifsChanged, errors };
}

const PATCH_FORMAT = `<edit_mode>
When the request gives you a current score to change, reply with a patch, not a whole score: one JSON object with only these keys, in this order, each left out when you don't change it:
{"message": "<one short sentence on what you changed>", "title": "<new title>", "harmony": <the whole new harmony>, "motifs": [<new or changed motifs, complete>], "remove": ["<part id>"], "parts": [<each changed or new part, complete>]}
- Change as little as the request needs. A follow-up that points back ("that", "it", "the last one", "the same idea", "even more", "go back") is about what the earlier step changed: the same parts and the same kind of change. Leave everything else out of the patch.
- A changed part keeps its id and is written whole. A new part gets a new id.
- Don't repeat what stays the same, and never touch parts the request says are locked.
- The session fixes key, meter, tempo and length: never write "context".
- If the request is a question rather than a change, reply {"message": "<the answer>"} only.
Reply with the JSON object and nothing else.
</edit_mode>`;

function userMessage(req: EditRequest, rules: Rules): string {
  const c = req.score.context;
  const locked = rules.locked.map((p) => `${p.id} (${p.role})`);
  const editable = req.score.parts.filter((p) => rules.editable.has(p.id)).map((p) => `${p.id} (${p.role})`);
  const task =
    req.kind === "vary"
      ? [
          `Write a variation of part ${req.partIds![0]}. Keep its idea: role, register, feel and its motif or groove, so it is recognisably the same part.`,
          "Change the details: rhythm placement, fills, density, voicing or motif transforms. Return only that part.",
          ...(req.prompt.trim() ? [`The producer adds: ${req.prompt.trim()}`] : []),
        ]
      : req.kind === "addPart"
        ? [
            `Add one new ${req.role} part that fits the parts already there and leaves them room. Give it a new id.`,
            ...(req.prompt.trim() ? [`The producer asks: ${req.prompt.trim()}`] : []),
          ]
        : [`Request: ${req.prompt}`];
  return [
    ...task,
    "",
    "Session context (fixed):",
    `- key: ${c.tonic} ${c.mode}; meter: ${c.meterNumerator}/${c.meterDenominator}, tempo ${c.tempo} BPM; length: ${c.bars} bars`,
    `- one bar = ${c.meterNumerator} beats, so every step-string bar has ${c.meterNumerator} × grid steps ` +
      `(grid 2: ${c.meterNumerator * 2}, grid 3: ${c.meterNumerator * 3}, grid 4: ${c.meterNumerator * 4})`,
    `- parts you may change: ${editable.join(", ") || "none (add only)"}`,
    `- locked, keep as they are: ${locked.join(", ") || "none"}`,
    `- harmony: ${rules.harmonyFree ? "you may change it" : "fixed, leave it out"}` +
      (rules.lockedMotifs.size ? `; motifs ${[...rules.lockedMotifs].join(", ")} are fixed` : ""),
    "",
    ...(req.history?.length
      ? ["The conversation so far is this idea's history, oldest first. The current score is where it led; read the request against it.", ""]
      : []),
    "The current score:",
    JSON.stringify(req.score),
    "",
    "Reply with the patch (see edit_mode).",
  ].join("\n");
}

function repairRequest(errors: string[]): string {
  return [
    "The patch has problems. Return the corrected patch as one JSON object, complete and against the current score as given:",
    `- ${errors.slice(0, 40).join("\n- ")}`,
  ].join("\n");
}

export async function editScore(req: EditRequest, backend: Backend = defaultBackend(), options: EditOptions = {}): Promise<EditResult> {
  const started = Date.now();
  const rules = rulesFor(req);
  const usage = { inputTokens: 0, outputTokens: 0, cacheReadTokens: 0 };
  let first: Pick<Completion, "firstTokenMs" | "firstTextMs"> = { firstTokenMs: null, firstTextMs: null };
  let firstPartMs: number | null = null;
  let unplayable: Unplayable[] = [];
  const replies: string[] = [];
  const toolCalls: string[] = [];
  let attempt = 0;
  let stream: PartStream | null = null;
  let result: EditResult | undefined;

  const reportPart = (part: Part) => {
    const at = Date.now() - started;
    firstPartMs ??= at;
    options.onPart?.(part, at);
  };
  // Every reply is a whole patch, so a later one may change the head: it streams only while nothing has played.
  const openStream = (): PartStream | null => {
    if (firstPartMs !== null) return null;
    const { onHead, onPartStarted } = options;
    return new PartStream(reportPart, {
      normalizeHead: (raw) => headFrom(raw, req.score),
      headCheck: (head) => headErrors(head, req.score, rules),
      partCheck: (part) => partErrors(part, req, rules),
      ...(onHead ? { onHead } : {}),
      ...(onPartStarted ? { onPartStarted } : {}),
    });
  };

  const reviewReply = (completion: Completion): string | null => {
    attempt++;
    replies.push(completion.text);
    toolCalls.push(...(completion.toolCalls ?? []));
    if (attempt === 1) {
      first = { firstTokenMs: completion.firstTokenMs, firstTextMs: completion.firstTextMs };
      unplayable = stream?.unplayable ?? [];
    }
    usage.inputTokens += completion.inputTokens;
    usage.outputTokens += completion.outputTokens;
    usage.cacheReadTokens += completion.cacheReadTokens;

    let applied: Applied;
    try {
      applied = applyPatch(completion.text, req, rules);
    } catch (err) {
      const errors = [`response was not valid JSON: ${String(err)}`];
      if (attempt === MAX_ATTEMPTS) throw new FlowstateError("invalid_score", `edit invalid after ${attempt} attempts: ${errors.join("; ")}`);
      return repairRequest(errors);
    }
    if (applied.errors.length === 0 || attempt === MAX_ATTEMPTS) {
      if (applied.errors.length > 0 && !applied.score)
        throw new FlowstateError("invalid_score", `edit invalid after ${attempt} attempts: ${applied.errors.slice(0, 5).join("; ")}`);
      result = {
        score: applied.score, message: applied.message, changed: applied.changed, removed: applied.removed,
        harmonyChanged: applied.harmonyChanged, motifsChanged: applied.motifsChanged,
        attempts: attempt, validationErrors: applied.errors, usage, latencyMs: Date.now() - started,
        ...first, firstPartMs, unplayable, replies, toolCalls,
      };
      return null;
    }
    return repairRequest(applied.errors);
  };

  const c = req.score.context;
  const lanes = req.kind === "addPart" ? [req.role!] : req.score.parts.filter((p) => rules.editable.has(p.id)).map((p) => p.role);
  const loadout = await createLoadout({
    prompt: req.prompt,
    ctx: {
      request: {
        tonic: c.tonic, mode: c.mode, bars: c.bars, tempo: c.tempo,
        meterNumerator: c.meterNumerator, meterDenominator: c.meterDenominator, style: c.style, lanes,
      },
      ...(options.host ?? defaultHost()),
    },
    tools: (req.tools ?? false) && backend.converse !== undefined,
  });
  await converse(
    backend,
    {
      system: SYSTEM_PROMPT,
      sections: { ...loadout.sections, edit_mode: PATCH_FORMAT },
      tools: loadout.tools,
      beforeToolCall: loadout.beforeToolCall,
    },
    userMessage(req, rules),
    {
      onReplyStart: () => void (stream = openStream()),
      onText: (d) => stream?.push(d),
      review: reviewReply,
      ...(options.signal ? { signal: options.signal } : {}),
    },
    historyTurns(req.history ?? [], req.score),
  );
  if (!result) throw new Error("edit conversation ended without a result");
  return result;
}
