// Library pack manifest v2 — the authoring format for a pack of MIDI clips that ships with Flowstate.
// Semantics: docs/library.md. `library/` validates packs against it and builds the LibraryCatalog
// (schema/src/bridge.ts) from them. v1 was v1's `flowstate.partnerMidiPack.v1`; v2 keeps its fields
// where they still make sense (producer, pack, license and attribution, lane, genre, key, tempo range,
// tags, feel, notes, relative asset paths) and drops what core now measures (meter, bars, energy,
// density, complexity).
// Conventions (as in score.ts): fixed keys, every key required, nullable instead of optional.
import { z } from "zod";
import { Mode, Tonic } from "./score.ts";

export const LIBRARY_PACK_ID = "flowstate.libraryPack.v2";

const Slug = z.string().regex(/^[a-z0-9]+(-[a-z0-9]+)*$/, "lowercase kebab-case");

export const LibraryLane = z.enum(["chords", "bass", "melody", "drums"]);

export const PackLicense = z.object({
  id: Slug,
  name: z.string().min(1),
  allowsPreview: z.boolean(),
  allowsExport: z.boolean().describe("Drag-out and export of the clip"),
  allowsStyleExamples: z.boolean().describe("The planner may show the clip to a model as a style example"),
});

export const PackEntry = z.object({
  id: Slug.describe("Unique in the pack; the catalog id is lib:<pack>/<id>"),
  file: z.string().describe("Relative path of the .mid inside the pack: forward slashes, no '..', no absolute prefix"),
  lane: LibraryLane.describe("The clip's lane; the analyzer fails the entry if the content clearly says otherwise"),
  title: z.string().min(1),
  genres: z.array(Slug).min(1).describe("Style tags in the planner's vocabulary: hip-hop, rnb, neo-soul, pop, ..."),
  tags: z.array(Slug),
  feel: z.string().nullable(),
  tempoMin: z.number().min(20).max(400).nullable().describe("null = the file's tempo minus 8"),
  tempoMax: z.number().min(20).max(400).nullable().describe("null = the file's tempo plus 8"),
  key: z.object({ tonic: Tonic, mode: Mode }).nullable().describe("A key the producer knows; wins over detection. null = detect"),
  notes: z.string().nullable().describe("Musical note for people and models; no file paths or source names"),
});

export const LibraryPackManifest = z.object({
  schema: z.literal(LIBRARY_PACK_ID),
  id: Slug.describe("Equals the pack's folder name"),
  title: z.string().min(1),
  producer: z.string().min(1),
  credit: z.string().min(1).describe("Attribution text, shown with every clip"),
  creditFile: z.string().describe("Relative path of the pack's credit and license note"),
  license: PackLicense,
  laneNotes: z.array(z.string()).describe("Why a lane has no clips, so nothing fake fills it"),
  omitted: z.array(z.object({
    file: z.string(),
    reason: z.string().min(1),
  })).describe("Source files deliberately left out, and why"),
  entries: z.array(PackEntry).min(1),
});

export type LibraryLane = z.infer<typeof LibraryLane>;
export type PackEntry = z.infer<typeof PackEntry>;
export type LibraryPackManifest = z.infer<typeof LibraryPackManifest>;
