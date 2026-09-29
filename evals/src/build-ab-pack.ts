// Builds a blind A/B listening pack from two realized output dirs.
// Usage: tsx src/build-ab-pack.ts --a out/v2 --b out/v1 --name phase0-r1 --seed 42 [--prompts prompts/phase0.json]
//
// A = v2 candidates, B = v1 baseline. Per prompt, the two MIDI files are copied as option-1/option-2 in a
// seeded random order. The key (which option is v2) is written OUTSIDE the pack folder:
//   ab/packs/<name>/                 <- give this folder to listeners
//   ab/packs/<name>.key.json         <- keep this hidden until scoring
import { copyFileSync, existsSync, mkdirSync, readdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadPrompts, parseArgs } from "./common.ts";
import { hashSeed, mulberry32 } from "./stats.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const evalsRoot = path.join(here, "..");
const args = parseArgs(process.argv.slice(2));
const need = (k: string): string => { const v = args.flags[k]; if (!v) { console.error(`missing --${k}\nusage: tsx src/build-ab-pack.ts --a <v2 dir> --b <v1 dir> --name <pack> --seed <n> [--prompts file]`); process.exit(2); } return v; };
const dirA = path.resolve(need("a"));
const dirB = path.resolve(need("b"));
const name = need("name");
if (!/^[A-Za-z0-9._-]+$/.test(name)) throw new Error("--name must be a simple folder name");
const seedText = need("seed");
const promptsFile = path.resolve(args.flags.prompts ?? path.join(evalsRoot, "prompts", "phase0.json"));
const packsRoot = path.resolve(args.flags["packs-dir"] ?? path.join(evalsRoot, "ab", "packs"));
const packDir = path.join(packsRoot, name);
const keyFile = path.join(packsRoot, `${name}.key.json`);
if (existsSync(packDir) && readdirSync(packDir).length && args.flags.force !== "true") throw new Error(`${packDir} exists; pass --force to overwrite`);

const prompts = loadPrompts(promptsFile);
const rand = mulberry32(hashSeed(`ab:${seedText}`));
mkdirSync(packDir, { recursive: true });

const entries: { promptId: string; folder: string; v2Option: 1 | 2; v2File: string; v1File: string }[] = [];
const skipped: { promptId: string; reason: string }[] = [];
const csvRows = ["prompt_id,preferred,musicality_1,musicality_2,fits_prompt_1,fits_prompt_2,notes"];

prompts.forEach((p) => {
  const v2File = path.join(dirA, `${p.id}.mid`), v1File = path.join(dirB, `${p.id}.mid`);
  const coin = rand(); // drawn for every prompt so assignments do not shift when one prompt is skipped
  if (!existsSync(v2File) || !existsSync(v1File)) {
    skipped.push({ promptId: p.id, reason: `missing ${!existsSync(v2File) ? "A(v2)" : ""}${!existsSync(v1File) ? " B(v1)" : ""}`.trim() });
    return;
  }
  const v2Option: 1 | 2 = coin < 0.5 ? 1 : 2;
  const folder = `${String(entries.length + 1).padStart(2, "0")}-${p.id}`;
  const dir = path.join(packDir, folder);
  mkdirSync(dir, { recursive: true });
  copyFileSync(v2Option === 1 ? v2File : v1File, path.join(dir, "option-1.mid"));
  copyFileSync(v2Option === 1 ? v1File : v2File, path.join(dir, "option-2.mid"));
  const c = p.controls;
  writeFileSync(path.join(dir, "prompt.txt"), [
    p.prompt, "",
    `Tempo: ${c.tempo} BPM`, `Meter: ${c.meterNumerator}/${c.meterDenominator}`, `Key: ${c.tonic} ${c.mode.replace("_", " ")}`,
    `Length: ${c.bars} bars`, `Style: ${c.style.join(", ")}`, "",
  ].join("\n"));
  entries.push({ promptId: p.id, folder, v2Option, v2File, v1File });
  csvRows.push(`${p.id},,,,,,`);
});

writeFileSync(path.join(packDir, "scoresheet.csv"), csvRows.join("\n") + "\n");
writeFileSync(path.join(packDir, "README.txt"), `Flowstate blind A/B listening pack: ${name}

Each numbered folder holds one prompt (prompt.txt) and two MIDI clips, option-1.mid and option-2.mid.
The two options come from different generators, in a random order that changes from folder to folder.

For each folder:
  1. Read prompt.txt.
  2. Set your DAW to the tempo (and meter) in prompt.txt.
  3. Import BOTH MIDI files into the SAME instrument setup: same instrument per track role
     (chords, bass, melody, drums on channel 10), same mix and effects. Only the notes should differ.
  4. Loop each option several times. Switch back and forth.
  5. Fill in one row of scoresheet.csv:
       preferred      1, 2 or tie
       musicality_1   1-5  (how good option 1 sounds as music)
       musicality_2   1-5
       fits_prompt_1  1-5  (how well option 1 matches the prompt)
       fits_prompt_2  1-5
       notes          optional free text (avoid commas, or wrap the text in double quotes)

Rules:
  - Do not look for, open or ask about the answer key. Do not compare notes with other listeners
    until everyone has returned a sheet.
  - Judge the music, not the file names or folder order.
  - Save your copy as scoresheet-<your-name>.csv and send it back.
`);
writeFileSync(keyFile, JSON.stringify({
  pack: name, seed: seedText, createdAt: new Date().toISOString(), prompts: promptsFile,
  a: { label: "v2", dir: dirA }, b: { label: "v1", dir: dirB }, entries, skipped,
}, null, 2));

console.log(`pack: ${packDir} (${entries.length} prompts)`);
console.log(`key:  ${keyFile}  (keep hidden from listeners)`);
if (skipped.length) console.log(`skipped: ${skipped.map((s) => `${s.promptId} (${s.reason})`).join(", ")}`);
