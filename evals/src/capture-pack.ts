// Builds the capture listening pack (P1-20, "Use what I just played"): per case of the capture set, the riff as
// it was played and what Flowstate made from it, for the owner to judge whether the new parts fit the riff.
// Usage: tsx src/capture-pack.ts --results results/p1-20/deepseek-flash-1 --name capture-r1
//          [--prompts prompts/capture-phase1.json] [--force]
//        tsx src/capture-pack.ts --results results/p1-20/feel-1 --against results/p1-20/deepseek-flash-1 --name capture-ab1 --seed 7
//
// With --against, a blind A/B pack instead (a generators pack, P0-5's format): per case the two runs' results in a
// seeded random order, `--results` as A (the candidate) and `--against` as B; the key says which is which.
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
import { hashSeed, mulberry32 } from "./stats.ts";

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
  const againstDir = args.flags.against ? path.resolve(args.flags.against) : null;
  const rand = mulberry32(hashSeed(`capture-ab:${args.flags.seed ?? "1"}`));
  const cases = JSON.parse(readFileSync(path.resolve(args.flags.prompts ?? path.join(evalsRoot, "prompts", "capture-phase1.json")), "utf8")) as CaptureCase[];
  const packsRoot = path.resolve(args.flags["packs-dir"] ?? path.join(evalsRoot, "ab", "packs"));
  const packDir = path.join(packsRoot, name);
  if (existsSync(packDir) && readdirSync(packDir).length && args.flags.force !== "true") throw new Error(`${packDir} exists; pass --force to overwrite`);
  mkdirSync(packDir, { recursive: true });

  const entries: { promptId: string; folder: string; intent: string; riffRole: string; v2Option?: 1 | 2 }[] = [];
  const skipped: { promptId: string; reason: string }[] = [];
  const rows = [againstDir ? SHEET_HEADER.generators : SHEET_HEADER.capture];
  const notes = (file: string) => roleSmf(JSON.parse(readFileSync(file, "utf8")) as NotesFile);
  for (const c of cases) {
    const resultFile = path.join(resultsDir, `${c.id}.notes.json`);
    const otherFile = againstDir ? path.join(againstDir, `${c.id}.notes.json`) : null;
    const coin = rand(); // drawn for every case, so the order doesn't shift when one is skipped
    if (!existsSync(resultFile) || (otherFile && !existsSync(otherFile))) { skipped.push({ promptId: c.id, reason: "no result (the plan failed)" }); continue; }
    const folder = `${String(entries.length + 1).padStart(2, "0")}-${c.id}`;
    const dir = path.join(packDir, folder);
    mkdirSync(dir, { recursive: true });
    writeFileSync(path.join(dir, "prompt.txt"), capturePromptTxt(c));
    const entry: (typeof entries)[number] = { promptId: c.id, folder, intent: c.request.reference.intent, riffRole: c.riffRole };
    if (otherFile) {
      entry.v2Option = coin < 0.5 ? 1 : 2;
      writeFileSync(path.join(dir, "option-1.mid"), notes(entry.v2Option === 1 ? resultFile : otherFile));
      writeFileSync(path.join(dir, "option-2.mid"), notes(entry.v2Option === 1 ? otherFile : resultFile));
      writeFileSync(path.join(dir, "riff.mid"), roleSmf(realizeRiff(c.request.reference.score)));
      rows.push(`${c.id},,,,,,`);
    } else {
      writeFileSync(path.join(dir, "option-1.mid"), roleSmf(realizeRiff(c.request.reference.score)));
      writeFileSync(path.join(dir, "option-2.mid"), notes(resultFile));
      rows.push(`${c.id},,,`);
    }
    entries.push(entry);
  }

  writeFileSync(path.join(packDir, "scoresheet.csv"), rows.join("\n") + "\n");
  if (againstDir) writeFileSync(path.join(packDir, "README.txt"), `Flowstate capture A/B pack: ${name}

"Use what I just played", two versions of Flowstate answering the same riff. Each numbered folder holds the riff
(riff.mid) and two results, option-1.mid and option-2.mid, in a random order that changes from folder to folder.
prompt.txt says which riff and what was asked.

For each folder (player.html does all of this in the browser, if the pack was rendered):
  1. Listen to the riff, then loop both options. Switch back and forth.
  2. Fill in one row of scoresheet.csv: preferred (1, 2 or tie), musicality_1 and musicality_2 (1-5),
     fits_prompt_1 and fits_prompt_2 (1-5: how well each fits, continues or answers the riff), notes.

Do not look for or open the answer key.
`);
  else writeFileSync(path.join(packDir, "README.txt"), `Flowstate capture listening pack: ${name}

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
    pack: name, kind: againstDir ? "generators" : "capture", a: { label: "candidate", dir: resultsDir }, b: againstDir ? { label: "baseline", dir: againstDir } : undefined, createdAt: new Date().toISOString(), results: resultsDir, entries, skipped,
  }, null, 2));
  console.log(`pack: ${packDir} (${entries.length} cases)`);
  if (skipped.length) console.log(`skipped: ${skipped.map((s) => `${s.promptId} (${s.reason})`).join(", ")}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
