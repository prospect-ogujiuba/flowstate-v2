// Builds the capture listening pack (P1-20, "Use what I just played"): per case of the capture set, the riff as
// it was played and what Flowstate made from it, for the owner to judge whether the new parts fit the riff.
// Usage: tsx src/capture-pack.ts --results results/p1-20/deepseek-flash-1 --name capture-r1
//          [--prompts prompts/capture-phase1.json] [--force]
//
// Each NN-<case> folder gets option-1.mid (the riff alone, realized from the request's reference) and
// option-2.mid (the planned result, with the riff where the intent keeps it), and a prompt.txt naming the riff and
// the intent. Not blind: the order is always riff, then result. Then `npm run -w evals render -- ab/packs/<name>`
// for audio and player.html, and `npm run -w evals ab:score -- --key ab/packs/<name>.key.json <sheet>` scores it.
// Needs build:core (fs-realize).
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs, type NotesFile } from "./common.ts";
import { SHEET_HEADER } from "./player.ts";
import { roleSmf } from "./smf.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const evalsRoot = path.join(here, "..");
const realizer = path.join(evalsRoot, "..", "build", "core", process.platform === "win32" ? "fs-realize.exe" : "fs-realize");

export interface CaptureCase {
  id: string;
  riff: string;
  riffRole: string;
  request: {
    prompt: string;
    context: { tempo: number; meterNumerator: number; meterDenominator: number; tonic: string; mode: string; bars: number; style: string[] };
    reference: { score: unknown; intent: string };
  };
}

const INTENT_LABEL: Record<string, string> = {
  continue: "Continue", harmonize: "Harmonize", add_bass: "Add bass", add_drums: "Add drums", answer: "Answer",
};

/** prompt.txt for a case, in the format render-pack reads: a first line, a blank line, then "Key: value" lines. */
export function capturePromptTxt(c: CaptureCase): string {
  const ctx = c.request.context;
  const asked = INTENT_LABEL[c.request.reference.intent] ?? c.request.reference.intent;
  return [
    `Your ${c.riffRole} riff (${c.riff}), then ${asked}${c.request.prompt ? `: "${c.request.prompt}"` : ""}`, "",
    `Tempo: ${ctx.tempo} BPM`, `Meter: ${ctx.meterNumerator}/${ctx.meterDenominator}`, `Key: ${ctx.tonic} ${ctx.mode.replace("_", " ")}`,
    `Length: ${ctx.bars} bars`, `Style: ${ctx.style.join(", ")}`, `Intent: ${asked}`, "",
  ].join("\n");
}

function realizeRiff(score: unknown): NotesFile {
  const dir = mkdtempSync(path.join(os.tmpdir(), "flowstate-riff-"));
  try {
    writeFileSync(path.join(dir, "riff.score.json"), JSON.stringify(score));
    execFileSync(realizer, ["--in", path.join(dir, "riff.score.json"), "--seed", "1", "--out-notes", path.join(dir, "riff.notes.json")]);
    return JSON.parse(readFileSync(path.join(dir, "riff.notes.json"), "utf8")) as NotesFile;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const results = args.flags.results, name = args.flags.name;
  if (!results || !name) {
    console.error("usage: tsx src/capture-pack.ts --results <results/p1-20/run> --name <pack> [--prompts file] [--force]");
    process.exit(2);
  }
  if (!/^[A-Za-z0-9._-]+$/.test(name)) throw new Error("--name must be a simple folder name");
  if (!existsSync(realizer)) throw new Error(`realizer not built at ${realizer}; run npm run build:core`);
  const resultsDir = path.resolve(results);
  const cases = JSON.parse(readFileSync(path.resolve(args.flags.prompts ?? path.join(evalsRoot, "prompts", "capture-phase1.json")), "utf8")) as CaptureCase[];
  const packsRoot = path.resolve(args.flags["packs-dir"] ?? path.join(evalsRoot, "ab", "packs"));
  const packDir = path.join(packsRoot, name);
  if (existsSync(packDir) && readdirSync(packDir).length && args.flags.force !== "true") throw new Error(`${packDir} exists; pass --force to overwrite`);
  mkdirSync(packDir, { recursive: true });

  const entries: { promptId: string; folder: string; intent: string; riffRole: string }[] = [];
  const skipped: { promptId: string; reason: string }[] = [];
  const rows = [SHEET_HEADER.capture];
  for (const c of cases) {
    const resultFile = path.join(resultsDir, `${c.id}.notes.json`);
    if (!existsSync(resultFile)) { skipped.push({ promptId: c.id, reason: "no result (the plan failed)" }); continue; }
    const folder = `${String(entries.length + 1).padStart(2, "0")}-${c.id}`;
    const dir = path.join(packDir, folder);
    mkdirSync(dir, { recursive: true });
    writeFileSync(path.join(dir, "option-1.mid"), roleSmf(realizeRiff(c.request.reference.score)));
    writeFileSync(path.join(dir, "option-2.mid"), roleSmf(JSON.parse(readFileSync(resultFile, "utf8")) as NotesFile));
    writeFileSync(path.join(dir, "prompt.txt"), capturePromptTxt(c));
    entries.push({ promptId: c.id, folder, intent: c.request.reference.intent, riffRole: c.riffRole });
    rows.push(`${c.id},,,`);
  }

  writeFileSync(path.join(packDir, "scoresheet.csv"), rows.join("\n") + "\n");
  writeFileSync(path.join(packDir, "README.txt"), `Flowstate capture listening pack: ${name}

"Use what I just played": each numbered folder holds a riff as a producer played it (option-1.mid) and what
Flowstate made from it (option-2.mid). prompt.txt says which riff and what was asked: harmonize, add bass and
add drums keep the riff and add a part to it; continue and answer write a new idea from the riff.

For each folder (player.html does all of this in the browser, if the pack was rendered):
  1. Read prompt.txt, and set your DAW to its tempo and meter.
  2. Import both MIDI files into the same instrument setup. Loop the riff, then the result.
  3. Fill in one row of scoresheet.csv:
       fits        y or n  (does Flowstate's part fit your riff, or answer or continue it?)
       musicality  1-5     (how good the result sounds as music)
       notes       optional free text (avoid commas, or wrap the text in double quotes)
`);
  writeFileSync(path.join(packsRoot, `${name}.key.json`), JSON.stringify({
    pack: name, kind: "capture", createdAt: new Date().toISOString(), results: resultsDir, entries, skipped,
  }, null, 2));
  console.log(`pack: ${packDir} (${entries.length} cases)`);
  if (skipped.length) console.log(`skipped: ${skipped.map((s) => `${s.promptId} (${s.reason})`).join(", ")}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
