// Builds the groove listening pack (P1-3's genre library): one folder per researched groove, playing the clip the
// Library ships (library/packs/flowstate-grooves), weakest-sourced first, for the owner to say whether each sounds
// like its style. Usage: tsx src/groove-pack.ts --name grooves-r1 [--research results/p1-3-grooves/research.json]
// Then `npm run -w evals render -- ab/packs/<name>` and `npm run -w evals ab:score -- --key ab/packs/<name>.key.json <sheet>`.
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { GROOVES } from "@flowstate/schema";
import { parseArgs } from "./common.ts";
import { SHEET_HEADER } from "./player.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const evalsRoot = path.join(here, "..");
const packMidi = path.join(evalsRoot, "..", "library", "packs", "flowstate-grooves", "midi", "drums");
const ORDER = ["low", "medium", "high"];

interface Researched { name: string; confidence: string; notes?: string }

function main() {
  const args = parseArgs(process.argv.slice(2));
  const name = args.flags.name;
  if (!name || !/^[A-Za-z0-9._-]+$/.test(name)) { console.error("usage: tsx src/groove-pack.ts --name <pack> [--research file]"); process.exit(2); }
  const research = (JSON.parse(readFileSync(path.resolve(args.flags.research ?? path.join(evalsRoot, "results", "p1-3-grooves", "research.json")), "utf8")) as { grooves: Researched[] }).grooves
    .filter((g) => g.name in GROOVES)
    .sort((a, b) => ORDER.indexOf(a.confidence) - ORDER.indexOf(b.confidence));
  const packsRoot = path.join(evalsRoot, "ab", "packs");
  const packDir = path.join(packsRoot, name);
  if (existsSync(packDir) && readdirSync(packDir).length && args.flags.force !== "true") throw new Error(`${packDir} exists; pass --force to overwrite`);
  const rows = [SHEET_HEADER.groove];
  const entries: { promptId: string; folder: string; confidence: string }[] = [];
  for (const g of research) {
    const groove = GROOVES[g.name as keyof typeof GROOVES];
    const genre = readdirSync(packMidi).find((d) => existsSync(path.join(packMidi, d, `${g.name}.mid`)));
    if (!genre) throw new Error(`no clip for ${g.name}: run npm run -w library grooves`);
    const folder = `${String(entries.length + 1).padStart(2, "0")}-${g.name}`;
    mkdirSync(path.join(packDir, folder), { recursive: true });
    copyFileSync(path.join(packMidi, genre, `${g.name}.mid`), path.join(packDir, folder, "option-1.mid"));
    const tempo = Math.round((groove.tempo[0] + groove.tempo[1]) / 2);
    writeFileSync(path.join(packDir, folder, "prompt.txt"), [
      `${g.name.replace(/_/g, " ")}: ${groove.description.split(":").slice(1).join(":").trim()}`, "",
      `Tempo: ${tempo} BPM`, `Meter: ${groove.meter[0]}/${groove.meter[1]}`, `Length: 4 bars`, `Style: ${genre.replace(/-/g, " ")}`, "",
    ].join("\n"));
    rows.push(`${g.name},,,`);
    entries.push({ promptId: g.name, folder, confidence: g.confidence });
  }
  writeFileSync(path.join(packDir, "scoresheet.csv"), rows.join("\n") + "\n");
  writeFileSync(path.join(packDir, "README.txt"), `Flowstate groove listening pack: ${name}

One drum groove per folder (option-1.mid), as the Library ships it, at its style's tempo. The ones the research
was least sure of come first. For each: does it sound like the style it's named for (authentic: y or n), and
how good is it (musicality: 1-5)? Notes help most on a "n": what's missing or wrong.
`);
  writeFileSync(path.join(packsRoot, `${name}.key.json`), JSON.stringify({ pack: name, kind: "groove", createdAt: new Date().toISOString(), entries }, null, 2));
  console.log(`pack: ${packDir} (${entries.length} grooves)`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
