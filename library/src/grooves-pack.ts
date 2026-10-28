// Writes the Flowstate Grooves pack (packs/flowstate-grooves): every named groove in core's library as a
// 4-bar drum clip, so the Library can preview and drag them and the planner can show them as style examples.
//   npm run -w library grooves      (needs build:core for fs-realize), then npm run -w library build
//
// The clips are core's own realization of each groove at the middle of its tempo range, with the swing it
// pairs with (GROOVES in the IR schema), so they change only when a groove does. Deterministic: seed 1.
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { GROOVES } from "@flowstate/schema";
import { packsRoot } from "./catalog.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const realizer = path.join(here, "..", "..", "build", "core", process.platform === "win32" ? "fs-realize.exe" : "fs-realize");
const packId = "flowstate-grooves";
const credit = "Grooves by Flowstate.";

type GrooveName = keyof typeof GROOVES;

/** The style tag a groove is for: its description's lead ("uk garage: ..." -> "uk-garage"). */
export function genreOf(name: string, description: string): string {
  const lead = description.split(/[:(]/)[0]!.trim().toLowerCase();
  return lead.replace(/[^a-z0-9&]+/g, "-").replace(/&/g, "and").replace(/^-|-$/g, "") || name.split("_")[0]!;
}

/** "afrobeats_afrofusion" -> "Afrobeats afrofusion". */
export function titleOf(name: string): string {
  const words = name.replace(/_/g, " ");
  return words[0]!.toUpperCase() + words.slice(1);
}

function scoreFor(name: GrooveName) {
  const g = GROOVES[name];
  const tempo = Math.round((g.tempo[0] + g.tempo[1]) / 2);
  return {
    ir: "flowstate.score.v0", title: titleOf(name),
    context: { tempo, meterNumerator: g.meter[0], meterDenominator: g.meter[1], tonic: "C", mode: "minor", bars: 4, swing: g.swing, style: [] },
    form: [], harmony: [], motifs: [],
    parts: [{ id: "drums", role: "drums", name: "Drums", low: "C1", high: "C6", grid: 4, velocity: 100,
      blocks: [{ startBar: 1, endBar: 4, groove: name }] }],
  };
}

function main() {
  if (!existsSync(realizer)) throw new Error(`realizer not built at ${realizer}; run npm run build:core`);
  const packDir = path.join(packsRoot, packId);
  rmSync(path.join(packDir, "midi"), { recursive: true, force: true });
  const tmp = mkdtempSync(path.join(os.tmpdir(), "flowstate-grooves-"));
  const entries = [];
  try {
    for (const name of Object.keys(GROOVES) as GrooveName[]) {
      const g = GROOVES[name];
      const genre = genreOf(name, g.description);
      const file = `midi/drums/${genre}/${name}.mid`;
      mkdirSync(path.join(packDir, path.dirname(file)), { recursive: true });
      writeFileSync(path.join(tmp, "score.json"), JSON.stringify(scoreFor(name)));
      execFileSync(realizer, ["--in", path.join(tmp, "score.json"), "--seed", "1", "--out-mid", path.join(packDir, file)]);
      entries.push({
        id: name.replace(/_/g, "-"), file, lane: "drums", title: titleOf(name), genres: [genre], tags: ["groove", name.replace(/_/g, "-")],
        // Feel is search text: the validator refuses a "/" anywhere in it (no paths leak).
        feel: g.description.split(":").slice(1).join(":").split(" (")[0]!.trim().replace(/\//g, "-") || null,
        tempoMin: g.tempo[0], tempoMax: g.tempo[1], key: null,
        notes: `Core's ${name} groove${g.swing ? `, with swing ${g.swing}` : ""}. Pairs with a drums block's "groove": "${name}".`,
      });
    }
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
  const manifest = {
    schema: "flowstate.libraryPack.v2", id: packId, title: "Flowstate Grooves", producer: "Flowstate", credit, creditFile: "CREDITS.md",
    license: { id: "flowstate-built-in", name: "Flowstate built-in content", allowsPreview: true, allowsExport: true, allowsStyleExamples: true },
    laneNotes: ["Drums only: the grooves are rhythm patterns. Chords, bass and melodies come from the planner and the other packs."],
    omitted: [], entries,
  };
  writeFileSync(path.join(packDir, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  writeFileSync(path.join(packDir, "CREDITS.md"), `# Flowstate Grooves — credits and license

${credit}

The clips in this folder are core's realization of Flowstate's named drum grooves (core/src/grooves.cpp), written by
\`npm run -w library grooves\`. The patterns follow producer tutorials and references, listed per groove in
evals/results/p1-3-grooves/research.json; rhythms like these are common practice in their styles.

License: built-in Flowstate content. In Flowstate the clips can be previewed, dragged or exported into a user's
project, and shown to a model as style examples by the planner.

Flowstate shows the credit line above wherever a clip appears. Keep this file, the \`credit\` field in
\`manifest.json\` and that line in step.
`);
  console.log(`${entries.length} grooves -> ${path.relative(process.cwd(), packDir)}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
