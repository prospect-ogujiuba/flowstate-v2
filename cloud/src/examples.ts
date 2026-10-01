// Style examples: library clips the planner shows a model next to the IR spec, so it hears the idiom of
// a style in the same language it writes. Picked by metadata only (lane, genre, meter, tempo); no theory.
// Off unless asked for (`--examples N` on the CLI); whether it helps is for evals and blind listening.
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { LibraryCatalog, type LibraryClip } from "@flowstate/schema";

const here = path.dirname(fileURLToPath(import.meta.url));
export const catalogPath = path.join(here, "..", "..", "library", "catalog", "catalog.json");

let cached: LibraryCatalog | undefined;

export function loadCatalog(file = catalogPath): LibraryCatalog {
  if (file === catalogPath && cached) return cached;
  const catalog = LibraryCatalog.parse(JSON.parse(readFileSync(file, "utf8")));
  if (file === catalogPath) cached = catalog;
  return catalog;
}

export interface ExampleRequest {
  style: string[];
  lanes: string[];
  tempo: number;
  meterNumerator: number;
  meterDenominator: number;
}

/**
 * Up to `count` clips whose license allows style examples, in a requested lane and the request's meter,
 * sharing at least one style tag. Ranked by shared tags, then tempo fit; one clip per lane before a second.
 */
export function pickExamples(catalog: LibraryCatalog, req: ExampleRequest, count: number): LibraryClip[] {
  if (count <= 0) return [];
  const style = new Set(req.style.map((s) => s.toLowerCase()));
  const ranked = catalog.clips
    .filter((c) => c.entry.credit?.allowsStyleExamples)
    .filter((c) => c.entry.roles.some((r) => req.lanes.includes(r)))
    .filter((c) => c.entry.meterNumerator === req.meterNumerator && c.entry.meterDenominator === req.meterDenominator)
    .map((c) => {
      const shared = c.entry.genres.filter((g) => style.has(g)).length;
      const inTempo = req.tempo >= c.entry.tempoMin && req.tempo <= c.entry.tempoMax ? 1 : 0;
      return { c, shared, score: shared * 2 + inTempo };
    })
    .filter((x) => x.shared > 0)
    .sort((a, b) => b.score - a.score || a.c.entry.id.localeCompare(b.c.entry.id));
  const picked: LibraryClip[] = [];
  const lanes = new Set<string>();
  for (const pass of [true, false])
    for (const { c } of ranked) {
      if (picked.length >= count) return picked;
      if (picked.includes(c) || (pass && lanes.has(c.entry.roles[0]!))) continue;
      picked.push(c);
      lanes.add(c.entry.roles[0]!);
    }
  return picked;
}

/** The examples as a section of the planner's request: credit, then each clip's harmony and part. */
export function examplesText(clips: LibraryClip[]): string {
  if (clips.length === 0) return "";
  const credits = [...new Set(clips.map((c) => c.entry.credit!.text))].join(" ");
  const lines = [
    `Style examples from Flowstate's built-in library (${credits}) They show how the style sits in this IR.`,
    "Take the idiom (rhythm, voicing, harmonic colour), not the notes, and keep to this request's key, meter and length.",
  ];
  for (const c of clips) {
    const e = c.entry;
    const key = e.tonic && e.mode ? `${e.tonic} ${e.mode}` : "key not fixed";
    lines.push(`- "${e.title}" (${e.roles.join(", ")}; ${e.genres.join(", ")}; ${e.tempo} BPM; ${e.bars} bars; ${key})`);
    if (typeof c.score.harmony === "string" && c.score.harmony) lines.push(`  harmony: ${JSON.stringify(c.score.harmony)}`);
    for (const p of c.score.parts) lines.push(`  part: ${JSON.stringify(p)}`);
  }
  return lines.join("\n");
}
