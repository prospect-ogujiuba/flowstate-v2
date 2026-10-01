// Streams parts out of a score while the model is still writing it.
// The model writes one JSON object with "parts" last (the IR's field order). This scanner follows the text as it
// arrives: when the "parts" key appears, everything before it is the head; each element of the parts array is
// handed out as soon as its closing brace lands. Layout doesn't matter: one line, pretty-printed or fenced.
import { Part, Score } from "@flowstate/schema";
import { validateScore } from "./validate.ts";

export type Head = Omit<Score, "parts">;
export interface Unplayable { part: string; errors: string[] }

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
  private headTried = false;

  constructor(private readonly onPart?: (part: Part) => void) {}

  push(delta: string) {
    this.text += delta;
    for (; this.i < this.text.length; this.i++) this.step(this.text[this.i]!, this.i);
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
        } else if (ch === "{" && this.inParts && this.depth === 3) this.partStart = at;
        return;
      case "}":
      case "]":
        if (ch === "}" && this.inParts && this.depth === 3 && this.partStart >= 0) {
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
    if (this.headTried) return;
    this.headTried = true;
    const body = this.text.slice(this.start, partsKeyAt).trimEnd().replace(/,$/, "");
    try {
      this.head = JSON.parse(`${body}}`) as Head;
    } catch {
      this.head = null;
    }
  }

  private readPart(text: string) {
    let value: { id?: unknown; role?: unknown };
    try {
      value = JSON.parse(text);
    } catch (err) {
      return void this.unplayable.push({ part: "?", errors: [`not JSON: ${String(err)}`] });
    }
    const label = String(value.id ?? value.role ?? "?");
    const issues = (r: { error: { issues: { path: PropertyKey[]; message: string }[] } }) =>
      r.error.issues.slice(0, 5).map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`);
    if (!this.head) return void this.unplayable.push({ part: label, errors: ["the head before parts is missing or invalid"] });
    const part = Part.safeParse(value);
    if (!part.success) return void this.unplayable.push({ part: label, errors: issues(part) });
    const partial = Score.safeParse({ ...this.head, parts: [part.data] });
    if (!partial.success) return void this.unplayable.push({ part: label, errors: issues(partial) });
    const errors = validateScore(partial.data);
    if (errors.length > 0) return void this.unplayable.push({ part: label, errors: errors.slice(0, 5) });
    this.playable.push(part.data);
    this.onPart?.(part.data);
  }
}
