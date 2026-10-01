// Renders a blind A/B pack to audio and adds a listening player, so a pack can be judged without a DAW setup.
// Usage: tsx src/render-pack.ts ab/packs/<name> [--soundfont soundfonts/GeneralUser-GS.sf2] [--loops 2]
//
// Each NN-<prompt> folder gets option-1.mp3 and option-2.mp3: the blind MIDI files played through the same
// sound template (render-templates.ts, chosen from prompt.txt's style and wording), two loops plus a tail,
// loudness-matched so neither option wins by being louder. The pack root gets player.html. Needs ffmpeg.
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { audioToWav, SoundBankLoader, SpessaSynthProcessor } from "spessasynth_core";
import { parseArgs } from "./common.ts";
import { playerHtml, type PlayerEntry } from "./player.ts";
import { templateFor, type Role, type Template } from "./render-templates.ts";
import { readSmf, type ReadSong } from "./smf-read.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const evalsRoot = path.join(here, "..");
const SAMPLE_RATE = 44_100;
const TAIL_SECONDS = 1.5;
const BLOCK = 128;

/** prompt.txt as written by build-ab-pack: the prompt, a blank line, then "Key: value" lines. */
export function readPromptTxt(text: string): { prompt: string; fields: Record<string, string> } {
  const [first, ...rest] = text.split(/\r?\n/);
  const fields: Record<string, string> = {};
  for (const line of rest) {
    const m = /^([A-Za-z ]+):\s*(.*)$/.exec(line);
    if (m) fields[m[1]!.trim().toLowerCase()] = m[2]!.trim();
  }
  return { prompt: (first ?? "").trim(), fields };
}

const roleOf = (trackName: string): Role | null => {
  const r = trackName.trim().toLowerCase();
  return (["chords", "pad", "arp", "bass", "melody", "counter", "drums"] as const).find((x) => x === r) ?? null;
};

interface Ev { sample: number; order: number; apply: (s: SpessaSynthProcessor) => void }

function renderSong(soundBank: ArrayBuffer, song: ReadSong, t: Template, loops: number): Float32Array[] {
  const synth = new SpessaSynthProcessor(SAMPLE_RATE, { eventsEnabled: false });
  synth.soundBankManager.addSoundBank(SoundBankLoader.fromArrayBuffer(soundBank), "main");
  synth.setSystemParameter("autoAllocateVoices", true);

  const secondsPerTick = 60 / song.tempo / song.ppq;
  const loopTicks = song.lengthTicks;
  const toSample = (tick: number) => Math.round(tick * secondsPerTick * SAMPLE_RATE);
  const events: Ev[] = [];
  for (const track of song.tracks) {
    const role = roleOf(track.name);
    const ch = track.channel;
    if (ch < 0 || !role) continue;
    const isDrums = role === "drums";
    events.push({
      sample: 0, order: 0, apply: (s) => {
        s.programChange(ch, isDrums ? t.kit : t.programs[role as Exclude<Role, "drums">]);
        s.controllerChange(ch, 7, t.volume[role]);
        s.controllerChange(ch, 91, t.reverb);
      },
    });
    for (let loop = 0; loop < loops; loop++) {
      const offset = loop * loopTicks;
      for (const n of track.notes) {
        events.push({ sample: toSample(offset + n.tick), order: 2, apply: (s) => s.noteOn(ch, n.pitch, n.vel) });
        events.push({ sample: toSample(offset + n.tick + n.dur), order: 1, apply: (s) => s.noteOff(ch, n.pitch) });
      }
    }
  }
  events.sort((a, b) => a.sample - b.sample || a.order - b.order);

  const total = toSample(loops * loopTicks) + Math.round(TAIL_SECONDS * SAMPLE_RATE);
  const left = new Float32Array(total);
  const right = new Float32Array(total);
  let filled = 0;
  let next = 0;
  while (filled < total) {
    while (next < events.length && events[next]!.sample <= filled) events[next++]!.apply(synth);
    const until = next < events.length ? Math.min(events[next]!.sample, total) : total;
    const size = Math.max(1, Math.min(BLOCK, until - filled, total - filled));
    synth.process(left, right, filled, size);
    filled += size;
  }
  return [left, right];
}

function encode(wav: ArrayBuffer, out: string) {
  const dir = mkdtempSync(path.join(os.tmpdir(), "flowstate-render-"));
  try {
    const tmp = path.join(dir, "in.wav");
    writeFileSync(tmp, new Uint8Array(wav));
    // Same integrated loudness for every clip, so the louder option doesn't win by being louder.
    execFileSync("ffmpeg", ["-v", "error", "-y", "-i", tmp, "-af", "loudnorm=I=-16:TP=-1.5:LRA=11", "-ar", String(SAMPLE_RATE), "-b:a", "192k", out]);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const packDir = path.resolve(args._[0] ?? "");
  if (!args._[0] || !existsSync(packDir)) throw new Error("usage: tsx src/render-pack.ts <pack dir> [--soundfont file] [--loops n]");
  const sfPath = path.resolve(args.flags.soundfont ?? path.join(evalsRoot, "soundfonts", "GeneralUser-GS.sf2"));
  if (!existsSync(sfPath)) throw new Error(`no SoundFont at ${sfPath}; run npm run -w evals soundfont`);
  const loops = Number(args.flags.loops ?? 2);
  const sf = readFileSync(sfPath);
  const soundBank = sf.buffer.slice(sf.byteOffset, sf.byteOffset + sf.byteLength) as ArrayBuffer;

  const entries: PlayerEntry[] = [];
  const folders = readdirSync(packDir, { withFileTypes: true }).filter((d) => d.isDirectory() && /^\d+-/.test(d.name)).map((d) => d.name).sort();
  for (const folder of folders) {
    const dir = path.join(packDir, folder);
    const { prompt, fields } = readPromptTxt(readFileSync(path.join(dir, "prompt.txt"), "utf8"));
    const template = templateFor((fields.style ?? "").split(",").map((s) => s.trim()).filter(Boolean), prompt);
    for (const option of [1, 2]) {
      const song = readSmf(readFileSync(path.join(dir, `option-${option}.mid`)));
      const audio = renderSong(soundBank, song, template, loops);
      encode(audioToWav(audio, SAMPLE_RATE), path.join(dir, `option-${option}.mp3`));
    }
    entries.push({ folder, promptId: folder.replace(/^\d+-/, ""), prompt, fields, sound: template.family });
    console.log(`ok   ${folder}  (${template.family})`);
  }
  writeFileSync(path.join(packDir, "player.html"), playerHtml(path.basename(packDir), entries));
  console.log(`player: ${path.join(packDir, "player.html")}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();
