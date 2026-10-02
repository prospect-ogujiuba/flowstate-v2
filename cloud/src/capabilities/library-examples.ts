// Library examples: clips from Flowstate's built-in library, shown to the model in its own IR so it hears a
// style's idiom in the language it writes. Picked by metadata only (lane, genre, meter, tempo); no theory.
// Two ways in: the planner's `--examples N` puts them in the request, and the `library_examples` tool lets
// the model ask for them. Both read the catalog the service hands in as data.
import type { LibraryCatalog, LibraryClip } from "@flowstate/schema";
import { Type } from "typebox";
import type { CapabilityFactory } from "./api.ts";

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
    lines.push(`- "${e.title}" [${e.id}] (${e.roles.join(", ")}; ${e.genres.join(", ")}; ${e.tempo} BPM; ${e.bars} bars; ${key})`);
    if (typeof c.score.harmony === "string" && c.score.harmony) lines.push(`  harmony: ${JSON.stringify(c.score.harmony)}`);
    for (const p of c.score.parts) lines.push(`  part: ${JSON.stringify(p)}`);
  }
  return lines.join("\n");
}

const Params = Type.Object({
  lane: Type.Optional(Type.String({ description: "One of the request's lanes, e.g. chords or bass. Default: any requested lane." })),
  style: Type.Optional(Type.Array(Type.String(), { maxItems: 8, description: "Style tags to match. Default: the request's style tags." })),
  count: Type.Optional(Type.Integer({ minimum: 1, maximum: 3, description: "How many clips, 1 to 3. Default 2." })),
});

export const libraryExamples: CapabilityFactory = (pi) => {
  pi.registerTool({
    name: "library_examples",
    label: "Library examples",
    description:
      "Clips from Flowstate's built-in MIDI library in this score IR, matched to the request's meter and tempo, as style references. " +
      "Read-only. Returns each clip's id, credit, harmony and parts.",
    promptGuidelines: ["Call library_examples at most once, before writing the score, and only when the style's idiom is unclear to you."],
    parameters: Params,
    async execute(_id, params, _signal, _onUpdate, ctx) {
      if (!ctx.catalog) throw new Error("the library isn't available on this server");
      const r = ctx.request;
      const lanes = params.lane ? [params.lane].filter((l) => r.lanes.includes(l)) : r.lanes;
      if (lanes.length === 0) throw new Error(`lane must be one of the request's lanes: ${r.lanes.join(", ")}`);
      const style = params.style?.length ? params.style : r.style;
      const clips = pickExamples(ctx.catalog, { style, lanes, tempo: r.tempo, meterNumerator: r.meterNumerator, meterDenominator: r.meterDenominator }, params.count ?? 2);
      const text = clips.length
        ? examplesText(clips)
        : `No library clips match ${style.join(", ") || "these styles"} in ${lanes.join(", ")} at ${r.meterNumerator}/${r.meterDenominator}.`;
      return { content: [{ type: "text", text }], details: { entryIds: clips.map((c) => c.entry.id) } };
    },
  });
};
