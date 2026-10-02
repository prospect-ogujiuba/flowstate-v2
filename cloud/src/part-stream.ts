// Streams parts out of a score while the model is still writing it.
// The model writes one JSON object with "parts" last (the IR's field order). This scanner follows the text as it
// arrives: when the "parts" key appears, everything before it is the head; each element of the parts array is
// handed out as soon as its closing brace lands. Layout doesn't matter: one line, pretty-printed or fenced.
import { Part, Role as RoleEnum, Score } from "@flowstate/schema";
import { validateScore } from "./validate.ts";

export type Head = Omit<Score, "parts">;
export type Role = Part["role"];
export interface Unplayable { part: string; errors: string[] }

const issues = (r: { error: { issues: { path: PropertyKey[]; message: string }[] } }) =>
  r.error.issues.slice(0, 5).map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`);

/** A part's label for messages: its id, else its role. */
export const partLabel = (value: unknown) => {
  const v = (value ?? {}) as { id?: unknown; role?: unknown };
  return String(v.id ?? v.role ?? "?");
};

/** Checks one part against the head: the schema, then the semantic checks on the head plus this part alone. */
export function checkPart(head: Head | null, value: unknown): { part: Part } | { errors: string[] } {
  if (!head) return { errors: ["the head before parts is missing or invalid"] };
  const part = Part.safeParse(value);
  if (!part.success) return { errors: issues(part) };
  const partial = Score.safeParse({ ...head, parts: [part.data] });
  if (!partial.success) return { errors: issues(partial) };
  const errors = validateScore(partial.data);
  return errors.length > 0 ? { errors: errors.slice(0, 5) } : { part: part.data };
}

export class PartStream {
  /** Parts that passed the schema and the semantic checks against the head, in order. */
  readonly playable: Part[] = [];
  /** Parts that could not be played as they landed, and why. */
  readonly unplayable: Unplayable[] = [];
  head: Head | null = null;

  private text = "";
  private i = 0;
  private start = -1; // the score's opening brace
  private depth = 0; // {} and [] nesting; the score object is depth 1
  private inString = false;
  private escaped = false;
  private stringStart = -1;
  private lastKey: { name: string; at: number } | null = null; // the last depth-1 string, a key candidate
  private inParts = false;
  private partStart = -1;
  private partAnnounced = false;
  private headTried = false;

  /**
   * With a known head (a parts-only repair reply), parts are checked against it instead of the reply's own.
   * normalizeHead completes the head first; headCheck adds the request's hard constraints: a head that breaks
   * them plays nothing.
   */
  constructor(
    private readonly onPart?: (part: Part) => void,
    private readonly options: {
      knownHead?: Head;
      /** Completes the head the model wrote (e.g. the context fields the session fixes) before it is checked. */
      normalizeHead?: (head: Record<string, unknown>) => Head;
      headCheck?: (head: Head) => string[];
      /** The reply's head once it passed the checks (not called for a known head), as a score with no parts. */
      onHead?: (score: Score) => void;
      /**
       * A part has started: its id and role are written, the rest isn't. It may still turn out unplayable, so a
       * start isn't always followed by the part.
       */
      onPartStarted?: (partId: string, role: Role) => void;
    } = {},
  ) {
    if (options.knownHead) this.head = options.knownHead;
  }

  push(delta: string) {
    this.text += delta;
    for (; this.i < this.text.length; this.i++) this.step(this.text[this.i]!, this.i);
    if (this.partStart >= 0 && !this.partAnnounced) this.announcePart();
  }

  // The IR writes a part's id and role first, so both show up long before the part is complete. Keys nested
  // in blocks can't match: a part's blocks come after its id and role, and the first match wins.
  private announcePart() {
    if (!this.options.onPartStarted || !this.head) return;
    const text = this.text.slice(this.partStart);
    const id = text.match(/"id"\s*:\s*"((?:[^"\\]|\\.)*)"/)?.[1];
    const role = RoleEnum.safeParse(text.match(/"role"\s*:\s*"([a-z]+)"/)?.[1]);
    if (id === undefined || !role.success) return;
    this.partAnnounced = true;
    this.options.onPartStarted(id, role.data);
  }

  private step(ch: string, at: number) {
    if (this.start < 0) {
      if (ch === "{") {
        this.start = at;
        this.depth = 1;
      }
      return;
    }
    if (this.inString) {
      if (this.escaped) this.escaped = false;
      else if (ch === "\\") this.escaped = true;
      else if (ch === "\"") {
        this.inString = false;
        if (this.depth === 1) this.lastKey = { name: this.text.slice(this.stringStart + 1, at), at: this.stringStart };
      }
      return;
    }
    switch (ch) {
      case "\"":
        this.inString = true;
        this.stringStart = at;
        return;
      case "{":
      case "[":
        this.depth++;
        if (ch === "[" && this.depth === 2 && this.lastKey?.name === "parts") {
          this.inParts = true;
          this.readHead(this.lastKey.at);
        } else if (ch === "{" && this.inParts && this.depth === 3) {
          this.partStart = at;
          this.partAnnounced = false;
        }
        return;
      case "}":
      case "]":
        if (ch === "}" && this.inParts && this.depth === 3 && this.partStart >= 0) {
          if (!this.partAnnounced) this.announcePart();
          this.readPart(this.text.slice(this.partStart, at + 1));
          this.partStart = -1;
        }
        if (ch === "]" && this.inParts && this.depth === 2) this.inParts = false;
        this.depth--;
        return;
    }
  }

  // The head is the object up to the "parts" key, closed off.
  private readHead(partsKeyAt: number) {
    if (this.headTried || this.options.knownHead) return;
    this.headTried = true;
    const body = this.text.slice(this.start, partsKeyAt).trimEnd().replace(/,$/, "");
    try {
      const raw = JSON.parse(`${body}}`) as Record<string, unknown>;
      const head = this.options.normalizeHead ? this.options.normalizeHead(raw) : (raw as Head);
      const bare = Score.safeParse({ ...head, parts: [] });
      this.head = bare.success && (this.options.headCheck?.(bare.data) ?? []).length === 0 ? head : null;
      if (this.head && bare.success) this.options.onHead?.(bare.data);
    } catch {
      this.head = null;
    }
  }

  private readPart(text: string) {
    let value: unknown;
    try {
      value = JSON.parse(text);
    } catch (err) {
      return void this.unplayable.push({ part: "?", errors: [`not JSON: ${String(err)}`] });
    }
    const checked = checkPart(this.head, value);
    if ("errors" in checked) return void this.unplayable.push({ part: partLabel(value), errors: checked.errors });
    this.playable.push(checked.part);
    this.onPart?.(checked.part);
  }
}
