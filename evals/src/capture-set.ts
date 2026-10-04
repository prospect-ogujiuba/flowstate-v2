// Builds the capture prompt set (P1-20, "Use what I just played"): riffs as a producer plays them, read by core's
// analyzer exactly as the plugin reads a capture, and one PlanRequest per case.
//
//   npm run -w evals capture:build      (needs build:core for fs-analyze)
//
// Riffs are GodFlow's played library clips (chords, bass) and riffs written here with human timing and dynamics
// (melodies, drums), saved to evals/capture/riffs/*.mid. Each is analyzed with `fs-analyze --literal`. When core
// isn't sure of the key, the riff's session key is used, as the plugin falls back to the current idea's key (each
// riff is played over a session in its key); drums have no key and take the session's.
// The requests follow Controller::useCapture: the riff's key, tempo and meter; harmonize, add bass and add drums
// at the riff's length with the intent's lane (less the riff's own); continue and answer at the session's 4 bars.
// Output: evals/prompts/capture-phase1.json (committed; the planner run needs no core).
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { writeSmf } from "./smf.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..", "..");
const riffDir = path.join(repo, "evals", "capture", "riffs");
const analyzer = path.join(repo, "build", "core", process.platform === "win32" ? "fs-analyze.exe" : "fs-analyze");

type Intent = "continue" | "harmonize" | "add_bass" | "add_drums" | "answer";
type Role = "chords" | "bass" | "melody" | "drums";

// ---- Riffs -----------------------------------------------------------------------------------------

/** [start beat (0-based), length in beats, pitch (name or GM number), velocity] */
type Played = [number, number, string | number, number];

interface WrittenRiff { id: string; tempo: number; bars: number; drums?: boolean; key: string; notes: Played[] }
interface LibraryRiff { id: string; library: string }

const hats = (bars: number, vel = 70): Played[] => Array.from({ length: bars * 8 }, (_, i) => [i * 0.5, 0.25, 42, vel + (i % 2 ? -12 : 0)]);

const WRITTEN: WrittenRiff[] = [
  { id: "lofi-melody", tempo: 78, bars: 4, key: "Eb major", notes: [
    [0, 0.75, "Bb4", 78], [0.75, 0.25, "C5", 64], [1, 1, "Bb4", 72], [2, 0.5, "G4", 70], [2.5, 1.5, "F4", 66],
    [4, 0.75, "G4", 76], [4.75, 0.25, "Ab4", 62], [5, 1, "G4", 70], [6, 0.5, "Eb4", 68], [6.5, 1.5, "C4", 64],
    [8, 0.75, "Bb4", 80], [8.75, 0.25, "C5", 66], [9, 1, "Eb5", 84], [10, 0.5, "D5", 72], [10.5, 1.5, "Bb4", 68],
    [12, 1, "G4", 74], [13, 0.5, "F4", 66], [13.5, 0.5, "G4", 70], [14, 2, "Eb4", 72]] },
  { id: "pop-hook", tempo: 110, bars: 2, key: "C major", notes: [
    [0, 0.5, "E5", 96], [0.5, 0.5, "D5", 84], [1, 0.5, "C5", 88], [1.5, 1, "G4", 80], [2.5, 0.5, "A4", 82], [3, 1, "C5", 90],
    [4, 0.5, "E5", 98], [4.5, 0.5, "F5", 92], [5, 0.5, "E5", 86], [5.5, 0.5, "D5", 84], [6, 2, "C5", 88]] },
  { id: "afro-lead", tempo: 104, bars: 2, key: "A minor", notes: [
    [0, 0.5, "A4", 92], [0.75, 0.25, "C5", 78], [1.5, 0.5, "E5", 88], [2, 0.5, "D5", 84], [2.75, 0.75, "C5", 80], [3.5, 0.5, "A4", 76],
    [4, 0.5, "G4", 90], [4.75, 0.25, "A4", 74], [5.5, 0.5, "C5", 86], [6, 0.75, "D5", 88], [6.75, 1.25, "A4", 80]] },
  { id: "trap-bells", tempo: 140, bars: 2, key: "F minor", notes: [
    [0, 0.5, "F5", 90], [0.5, 0.5, "Ab5", 80], [1, 0.5, "C6", 86], [1.5, 0.5, "Ab5", 76], [2, 0.5, "G5", 82], [2.5, 0.5, "Eb5", 74], [3, 1, "F5", 84],
    [4, 0.5, "F5", 90], [4.5, 0.5, "Ab5", 80], [5, 0.5, "Db6", 88], [5.5, 0.5, "C6", 78], [6, 0.5, "Bb5", 82], [6.5, 0.5, "G5", 74], [7, 1, "Ab5", 84]] },
  { id: "jazz-line", tempo: 120, bars: 2, key: "D dorian", notes: [
    [0, 0.66, "D5", 86], [0.66, 0.34, "E5", 70], [1, 0.66, "F5", 84], [1.66, 0.34, "A5", 72], [2, 0.66, "C6", 88], [2.66, 0.34, "B5", 70], [3, 1, "A5", 82],
    [4, 0.66, "G5", 84], [4.66, 0.34, "F5", 70], [5, 0.66, "E5", 82], [5.66, 0.34, "D5", 68], [6, 0.66, "C5", 80], [6.66, 0.34, "E5", 72], [7, 1, "D5", 84]] },
  { id: "boom-bap-drums", tempo: 90, bars: 2, drums: true, key: "C minor", notes: [
    ...hats(2),
    [0, 0.25, 36, 110], [1.75, 0.25, 36, 92], [2.5, 0.25, 36, 100], [4, 0.25, 36, 110], [5.75, 0.25, 36, 90], [6.25, 0.25, 36, 96],
    [1, 0.25, 38, 112], [3, 0.25, 38, 114], [5, 0.25, 38, 110], [7, 0.25, 38, 116]] },
  { id: "house-drums", tempo: 124, bars: 2, drums: true, key: "A minor", notes: [
    ...Array.from({ length: 8 }, (_, i): Played => [i, 0.25, 36, 112]),
    ...[1, 3, 5, 7].map((b): Played => [b, 0.25, 39, 100]),
    ...Array.from({ length: 8 }, (_, i): Played => [i + 0.5, 0.25, 46, 84])] },
  { id: "dembow-drums", tempo: 95, bars: 2, drums: true, key: "D minor", notes: [
    ...Array.from({ length: 8 }, (_, i): Played => [i, 0.25, 36, 108]),
    ...[0.75, 1.5, 2.75, 3.5, 4.75, 5.5, 6.75, 7.5].map((b): Played => [b, 0.25, 38, 96]),
    ...hats(2, 60)] },
];

const LIBRARY: LibraryRiff[] = [
  { id: "rnb-chords-01", library: "rnb-jazz-chords-01" },
  { id: "rnb-chords-07", library: "rnb-jazz-chords-07" },
  { id: "pop-chords-02", library: "pop-chords-02" },
  { id: "world-chords-03", library: "world-chords-03" },
  { id: "bass-02", library: "bass-02" },
  { id: "bass-05", library: "bass-05" },
];

// ---- Cases -----------------------------------------------------------------------------------------

interface Case { id: string; riff: string; intent: Intent; prompt?: string; roles?: Role[] }

const CASES: Case[] = [
  { id: "rnb-chords-add-bass", riff: "rnb-chords-01", intent: "add_bass" },
  { id: "pop-chords-add-bass", riff: "pop-chords-02", intent: "add_bass", prompt: "bouncy" },
  { id: "rnb-chords-add-drums", riff: "rnb-chords-07", intent: "add_drums", prompt: "neo soul pocket" },
  { id: "world-chords-add-drums", riff: "world-chords-03", intent: "add_drums" },
  { id: "pop-chords-continue", riff: "pop-chords-02", intent: "continue" },
  { id: "rnb-chords-answer", riff: "rnb-chords-01", intent: "answer", prompt: "a soft melody" },
  { id: "bass-add-drums", riff: "bass-02", intent: "add_drums", prompt: "boom bap" },
  { id: "bass-harmonize", riff: "bass-05", intent: "harmonize" },
  { id: "bass-continue", riff: "bass-02", intent: "continue" },
  { id: "lofi-harmonize", riff: "lofi-melody", intent: "harmonize", prompt: "lofi, jazzy chords" },
  { id: "pop-hook-harmonize", riff: "pop-hook", intent: "harmonize" },
  { id: "afro-add-bass", riff: "afro-lead", intent: "add_bass" },
  { id: "trap-add-bass", riff: "trap-bells", intent: "add_bass", prompt: "808 slides" },
  { id: "jazz-harmonize", riff: "jazz-line", intent: "harmonize", prompt: "jazz" },
  { id: "afro-add-drums", riff: "afro-lead", intent: "add_drums", prompt: "afrobeats" },
  { id: "lofi-continue", riff: "lofi-melody", intent: "continue" },
  { id: "pop-hook-answer", riff: "pop-hook", intent: "answer" },
  { id: "trap-answer", riff: "trap-bells", intent: "answer", prompt: "dark" },
  { id: "boom-bap-add-bass", riff: "boom-bap-drums", intent: "add_bass" },
  { id: "house-continue", riff: "house-drums", intent: "continue", prompt: "deep house" },
  { id: "dembow-harmonize", riff: "dembow-drums", intent: "harmonize", prompt: "reggaeton" },
];

const INTENT_LANE: Partial<Record<Intent, Role>> = { harmonize: "chords", add_bass: "bass", add_drums: "drums" };
const SESSION_BARS = 4;

// ---- Build -----------------------------------------------------------------------------------------

const NAMES: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
function midi(pitch: string | number): number {
  if (typeof pitch === "number") return pitch;
  const m = /^([A-G])([#b]?)(-?\d)$/.exec(pitch);
  if (!m) throw new Error(`bad pitch ${pitch}`);
  return 12 * (Number(m[3]) + 1) + NAMES[m[1]!]! + (m[2] === "#" ? 1 : m[2] === "b" ? -1 : 0);
}

// Played, not programmed: each onset a few ticks off the grid and each note a little short, the same every build.
function humanTicks(i: number): number { return Math.round(Math.sin(i * 12.9898) * 43758.5453 % 1 * 18); }

function writeRiff(r: WrittenRiff): string {
  const ppq = 960;
  const notes = r.notes.map(([beat, beats, pitch, vel], i) => ({
    tick: Math.max(0, Math.round(beat * ppq) + (i === 0 ? 0 : humanTicks(i))), dur: Math.round(beats * ppq * 0.9), pitch: midi(pitch), vel,
  }));
  const file = path.join(riffDir, `${r.id}.mid`);
  writeFileSync(file, writeSmf({ ppq, tempo: r.tempo, meter: [4, 4], lengthTicks: r.bars * 4 * ppq, tracks: [{ name: "Played", channel: r.drums ? 9 : 0, notes }] }));
  return file;
}

interface Analysis { ok: boolean; error?: string; role: Role; bars: number; tempo: number; key: { reliable: boolean; reason: string }; score: Record<string, unknown> }

function analyze(file: string, key?: string): Analysis {
  const args = ["--in", file, "--literal", "--title", "What I played", "--name", "Played", ...(key ? ["--key", key] : [])];
  let out: string;
  try {
    out = execFileSync(analyzer, args, { encoding: "utf8" });
  } catch (err) {
    out = (err as { stdout?: string }).stdout ?? "";
  }
  return JSON.parse(out) as Analysis;
}

// The library's own key reading: the tonic it detected, or the first candidate it names when it was unsure.
function libraryKey(id: string): string {
  const catalog = JSON.parse(readFileSync(path.join(repo, "library", "catalog", "catalog.json"), "utf8"));
  const clip = catalog.clips.find((c: { entry: { id: string } }) => c.entry.id === `lib:godflow/${id}`).entry;
  if (clip.tonic && clip.mode) return `${clip.tonic} ${clip.mode}`;
  const m = /between (\S+ \S+) \(/.exec(clip.keyNote ?? "");
  return m ? m[1]! : "C major";
}

mkdirSync(riffDir, { recursive: true });
const manifest = JSON.parse(readFileSync(path.join(repo, "library", "packs", "godflow", "manifest.json"), "utf8"));
const riffs = new Map<string, { analysis: Analysis; source: string; keyFrom: "detected" | "session" }>();
for (const r of [...WRITTEN, ...LIBRARY]) {
  const written = "notes" in r;
  const file = written ? writeRiff(r) : path.join(repo, "library", "packs", "godflow", manifest.entries.find((e: { id: string }) => e.id === r.library).file);
  let analysis = analyze(file);
  let keyFrom: "detected" | "session" = "detected";
  if (!analysis.ok) throw new Error(`${r.id}: ${analysis.error}`);
  if (!analysis.key.reliable || analysis.role === "drums") {
    analysis = analyze(file, written ? r.key : libraryKey(r.library));
    keyFrom = "session";
  }
  riffs.set(r.id, { analysis, source: path.relative(repo, file), keyFrom });
}

const out = CASES.map((c) => {
  const riff = riffs.get(c.riff);
  if (!riff) throw new Error(`${c.id}: no riff ${c.riff}`);
  const score = riff.analysis.score as { context: { tonic: string; mode: string; tempo: number; meterNumerator: number; meterDenominator: number } };
  const lane = INTENT_LANE[c.intent];
  const roles = lane ? (c.roles ?? [lane]).filter((r) => r !== riff.analysis.role) : (c.roles ?? null);
  if (roles && roles.length === 0) throw new Error(`${c.id}: the riff is already every lane asked for`);
  const cx = score.context;
  return {
    id: c.id,
    riff: c.riff,
    source: riff.source,
    riffRole: riff.analysis.role,
    keyFrom: riff.keyFrom,
    request: {
      protocol: "flowstate.bridge.v0",
      prompt: c.prompt ?? "",
      context: { tempo: cx.tempo, meterNumerator: cx.meterNumerator, meterDenominator: cx.meterDenominator, tonic: cx.tonic, mode: cx.mode,
        bars: lane ? riff.analysis.bars : SESSION_BARS, swing: 0, style: [] },
      roles,
      keep: null,
      reference: { score, intent: c.intent },
      provider: null,
    },
  };
});
const target = path.join(repo, "evals", "prompts", "capture-phase1.json");
writeFileSync(target, JSON.stringify(out, null, 2) + "\n");
for (const [id, r] of riffs)
  console.log(`${id.padEnd(16)} ${r.analysis.role.padEnd(7)} ${String(r.analysis.bars).padStart(2)} bars  ${(r.analysis.score as { context: { tonic: string; mode: string } }).context.tonic} ${(r.analysis.score as { context: { mode: string } }).context.mode} (${r.keyFrom})`);
console.log(`${out.length} cases -> ${path.relative(repo, target)}`);
