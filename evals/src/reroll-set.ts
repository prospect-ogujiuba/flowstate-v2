// Builds the re-roll listening set (P1-22): each score realized as written, and re-rolled.
// Usage: tsx src/reroll-set.ts [--scores results/phase0/v2-r1] [--out out/reroll] [--round 1]
//
// Writes <out>/written/<id>.{score,notes,report}.json (+ .mid) and the same in <out>/rerolled/. In
// the re-rolled score every part has its own seed, as if each lane's Re-roll had been pressed once;
// `--round` picks another set of seeds. Both realize with the clip seed the planner used (cli-plan).
// Then: npm run -w evals ab -- --kind reroll --a out/reroll/rerolled --b out/reroll/written --name <pack> --seed <n>
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "./common.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const evalsRoot = path.join(here, "..");
const repoRoot = path.join(evalsRoot, "..");
const realizer = path.join(repoRoot, "build", "core", process.platform === "win32" ? "fs-realize.exe" : "fs-realize");
if (!existsSync(realizer)) throw new Error(`realizer not built at ${realizer}; run npm run build:core`);

const args = parseArgs(process.argv.slice(2));
const scoresDir = path.resolve(evalsRoot, args.flags.scores ?? "results/phase0/v2-r1");
const outDir = path.resolve(evalsRoot, args.flags.out ?? "out/reroll");
const round = args.flags.round ?? "1";

/** FNV-1a, as cloud/src/cli-plan.ts seeds each prompt's clip. */
export function fnv(text: string): number {
  let h = 2166136261;
  for (const ch of text) h = Math.imul(h ^ ch.charCodeAt(0), 16777619) >>> 0;
  return h;
}

type Score = { parts: { id: string; seed?: number | null }[] };

function realize(score: Score, seed: number, dir: string, id: string): void {
  mkdirSync(dir, { recursive: true });
  const base = path.join(dir, id);
  writeFileSync(`${base}.score.json`, JSON.stringify(score, null, 2));
  execFileSync(realizer, [
    "--in", `${base}.score.json`, "--seed", String(seed),
    "--out-mid", `${base}.mid`, "--out-notes", `${base}.notes.json`, "--out-report", `${base}.report.json`,
  ]);
}

const ids = readdirSync(scoresDir).filter((f) => f.endsWith(".score.json")).map((f) => f.slice(0, -".score.json".length)).sort();
for (const id of ids) {
  const score = JSON.parse(readFileSync(path.join(scoresDir, `${id}.score.json`), "utf8")) as Score;
  realize(score, fnv(id), path.join(outDir, "written"), id);
  const rolled: Score = { ...score, parts: score.parts.map((p) => ({ ...p, seed: fnv(`reroll:${round}:${id}:${p.id}`) })) };
  realize(rolled, fnv(id), path.join(outDir, "rerolled"), id);
}
console.log(`${ids.length} ideas, written and re-rolled (round ${round}): ${outDir}`);
