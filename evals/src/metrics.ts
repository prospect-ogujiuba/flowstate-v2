// Computes symbolic metrics for a directory of *.notes.json files (core's note-list format).
// Usage: tsx src/metrics.ts <dir> [--prompts prompts/phase0.json] [--out <dir>/metrics.json] [--quiet]
//
// Per file and aggregate (mean over files). Out-of-key share needs tonic/mode, which comes from the
// prompts file, matched on the filename id (<id>.notes.json).
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadPrompts, parseArgs, scalePitchClasses, type Note, type NotesFile, type Part, type PromptEntry } from "./common.ts";

const PITCHED_MONO = new Set(["bass", "melody", "counter"]);
const CHORDAL = new Set(["chords", "pad"]);
/** Drum hits at or below this velocity count as ghost notes. */
export const GHOST_VELOCITY = 55;

export interface PartMetrics {
  id: string;
  role: string;
  notes: number;
  notesPerBar: number;
  distinctPitches: number;
  pitchClassEntropy: number | null;
  onsetEntropy: number;
  repetition: number | null;
  stepShare?: number | null;
  leapShare?: number | null;
  meanInterval?: number | null;
  voiceMovement?: number | null;
  drumVelocityRange?: number | null;
  ghostShare?: number | null;
}

export interface FileMetrics {
  id: string;
  bars: number;
  meter: string;
  parts: PartMetrics[];
  /** Summary values used by the table and the aggregate. null = not applicable. */
  summary: Record<string, number | null>;
}

function entropy(counts: Iterable<number>): number {
  const values = [...counts].filter((c) => c > 0);
  const total = values.reduce((a, b) => a + b, 0);
  if (total === 0) return 0;
  return -values.reduce((h, c) => h + (c / total) * Math.log2(c / total), 0);
}

function countBy<T>(items: T[], key: (item: T) => string | number): Map<string | number, number> {
  const m = new Map<string | number, number>();
  for (const item of items) m.set(key(item), (m.get(key(item)) ?? 0) + 1);
  return m;
}

const mean = (xs: number[]): number | null => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);

/** Groups a part's notes by onset tick (chords/simultaneities), sorted by time. */
function onsetGroups(notes: Note[]): { tick: number; pitches: number[] }[] {
  const m = new Map<number, number[]>();
  for (const n of notes) { const g = m.get(n.tick) ?? []; g.push(n.pitch); m.set(n.tick, g); }
  return [...m.entries()].sort((a, b) => a[0] - b[0]).map(([tick, pitches]) => ({ tick, pitches: pitches.sort((a, b) => a - b) }));
}

/**
 * Share of non-empty bars whose content is identical to another bar's. Content = (onset-in-bar, pitch),
 * with onsets snapped to a 1/12-quarter grid so humanization jitter does not hide repeated patterns.
 */
function repetitionScore(part: Part, doc: NotesFile): number | null {
  const snap = doc.ppq / 12;
  const sigs: string[] = [];
  for (let bar = 0; bar < doc.bars; bar++) {
    const start = bar * doc.ticksPerBar, end = start + doc.ticksPerBar;
    const inBar = part.notes.filter((n) => n.tick >= start && n.tick < end)
      .map((n) => `${Math.round((n.tick - start) / snap)}:${n.pitch}`).sort();
    if (inBar.length) sigs.push(inBar.join("|"));
  }
  if (sigs.length < 2) return null;
  const counts = countBy(sigs, (s) => s);
  return sigs.filter((s) => (counts.get(s) ?? 0) > 1).length / sigs.length;
}

export function partMetrics(part: Part, doc: NotesFile): PartMetrics {
  const isDrums = part.role === "drums" || part.channel === 10;
  const sixteenth = doc.ppq / 4;
  const onsets = [...new Set(part.notes.map((n) => n.tick))];
  const onsetBins = countBy(onsets, (t) => Math.round((t % doc.ticksPerBar) / sixteenth) % Math.max(1, Math.round(doc.ticksPerBar / sixteenth)));
  const m: PartMetrics = {
    id: part.id,
    role: part.role,
    notes: part.notes.length,
    notesPerBar: part.notes.length / doc.bars,
    distinctPitches: new Set(part.notes.map((n) => n.pitch)).size,
    pitchClassEntropy: isDrums ? null : entropy(countBy(part.notes, (n) => n.pitch % 12).values()),
    onsetEntropy: entropy(onsetBins.values()),
    repetition: repetitionScore(part, doc),
  };
  if (!isDrums && PITCHED_MONO.has(part.role)) {
    const line = onsetGroups(part.notes).map((g) => g.pitches[g.pitches.length - 1] ?? 0); // top note if doubled
    const intervals = line.slice(1).map((p, i) => Math.abs(p - (line[i] ?? p)));
    m.stepShare = intervals.length ? intervals.filter((x) => x <= 2).length / intervals.length : null;
    m.leapShare = intervals.length ? intervals.filter((x) => x > 7).length / intervals.length : null;
    m.meanInterval = mean(intervals);
  }
  if (!isDrums && CHORDAL.has(part.role)) {
    const groups = onsetGroups(part.notes);
    const moves: number[] = [];
    for (let i = 1; i < groups.length; i++) {
      const prev = groups[i - 1]?.pitches ?? [], cur = groups[i]?.pitches ?? [];
      if (!prev.length || !cur.length) continue;
      moves.push(cur.reduce((sum, p) => sum + Math.min(...prev.map((q) => Math.abs(p - q))), 0));
    }
    m.voiceMovement = mean(moves);
  }
  if (isDrums) {
    const vels = part.notes.map((n) => n.vel);
    m.drumVelocityRange = vels.length ? Math.max(...vels) - Math.min(...vels) : null;
    m.ghostShare = vels.length ? vels.filter((v) => v <= GHOST_VELOCITY).length / vels.length : null;
  }
  return m;
}

export function fileMetrics(id: string, doc: NotesFile, prompt: PromptEntry | undefined): FileMetrics {
  const parts = doc.parts.map((p) => partMetrics(p, doc));
  const pitched = doc.parts.filter((p) => !(p.role === "drums" || p.channel === 10));
  let outOfKey: number | null = null;
  if (prompt) {
    const scale = scalePitchClasses(prompt.controls.tonic, prompt.controls.mode);
    const all = pitched.flatMap((p) => p.notes);
    outOfKey = all.length ? all.filter((n) => !scale.has(((n.pitch % 12) + 12) % 12)).length / all.length : null;
  }
  const byRole = (role: string) => parts.filter((p) => p.role === role);
  const npb = (role: string) => { const ps = byRole(role); return ps.length ? ps.reduce((a, p) => a + p.notesPerBar, 0) : null; };
  const firstOf = (role: string, key: keyof PartMetrics) => { const v = byRole(role).map((p) => p[key]).find((x) => typeof x === "number"); return typeof v === "number" ? v : null; };
  const reps = parts.map((p) => p.repetition).filter((x): x is number => x !== null);
  const pitchedNotes = pitched.flatMap((p) => p.notes);
  const summary: Record<string, number | null> = {
    "chords/bar": npb("chords") ?? npb("pad"),
    "bass/bar": npb("bass"),
    "melody/bar": npb("melody"),
    "drums/bar": npb("drums"),
    pcEntropy: pitchedNotes.length ? entropy(countBy(pitchedNotes, (n) => n.pitch % 12).values()) : null,
    distinctPitches: pitchedNotes.length ? new Set(pitchedNotes.map((n) => n.pitch)).size : null,
    onsetEntropy: mean(parts.map((p) => p.onsetEntropy)),
    melStepShare: firstOf("melody", "stepShare"),
    melLeapShare: firstOf("melody", "leapShare"),
    voiceMove: firstOf("chords", "voiceMovement") ?? firstOf("pad", "voiceMovement"),
    drumVelRange: firstOf("drums", "drumVelocityRange"),
    ghostShare: firstOf("drums", "ghostShare"),
    repetition: mean(reps),
    outOfKey,
  };
  return { id, bars: doc.bars, meter: `${doc.meter[0]}/${doc.meter[1]}`, parts, summary };
}

export function aggregate(files: FileMetrics[]): Record<string, number | null> {
  const keys = new Set(files.flatMap((f) => Object.keys(f.summary)));
  const out: Record<string, number | null> = {};
  for (const k of keys) out[k] = mean(files.map((f) => f.summary[k]).filter((x): x is number => typeof x === "number"));
  return out;
}

const fmt = (v: number | null | undefined): string => (v === null || v === undefined ? "–" : Number.isInteger(v) ? String(v) : v.toFixed(2));

export function markdownTable(files: FileMetrics[], agg: Record<string, number | null>): string {
  const keys = Object.keys(agg);
  const lines = [
    `| id | meter | bars | ${keys.join(" | ")} |`,
    `|---|---|---|${keys.map(() => "---:").join("|")}|`,
    ...files.map((f) => `| ${f.id} | ${f.meter} | ${f.bars} | ${keys.map((k) => fmt(f.summary[k])).join(" | ")} |`),
    `| **mean** | | | ${keys.map((k) => `**${fmt(agg[k])}**`).join(" | ")} |`,
  ];
  return lines.join("\n");
}

export function computeDir(dir: string, prompts: PromptEntry[]): { files: FileMetrics[]; aggregate: Record<string, number | null> } {
  const byId = new Map(prompts.map((p) => [p.id, p]));
  const names = readdirSync(dir).filter((f) => f.endsWith(".notes.json")).sort();
  const files = names.map((name) => {
    const id = name.slice(0, -".notes.json".length);
    const doc = JSON.parse(readFileSync(path.join(dir, name), "utf8")) as NotesFile;
    return fileMetrics(id, doc, byId.get(id));
  });
  return { files, aggregate: aggregate(files) };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = parseArgs(process.argv.slice(2));
  const dir = args._[0];
  if (!dir) { console.error("usage: tsx src/metrics.ts <dir> [--prompts prompts/phase0.json] [--out file]"); process.exit(2); }
  const here = path.dirname(fileURLToPath(import.meta.url));
  const promptsFile = path.resolve(args.flags.prompts ?? path.join(here, "..", "prompts", "phase0.json"));
  const prompts = loadPrompts(promptsFile);
  const result = computeDir(path.resolve(dir), prompts);
  const missing = result.files.filter((f) => f.summary.outOfKey === null).map((f) => f.id);
  if (missing.length) console.error(`warning: no prompt/tonic for ${missing.join(", ")} (outOfKey = –)`);
  const outFile = path.resolve(args.flags.out ?? path.join(dir, "metrics.json"));
  writeFileSync(outFile, JSON.stringify({ dir: path.resolve(dir), prompts: promptsFile, ghostVelocity: GHOST_VELOCITY, generatedAt: new Date().toISOString(), aggregate: result.aggregate, files: result.files }, null, 2));
  console.log(markdownTable(result.files, result.aggregate));
  console.log(`\nwrote ${outFile}`);
}
