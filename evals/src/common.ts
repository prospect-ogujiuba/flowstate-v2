// Shared types and helpers for the Phase 0 eval tooling.
import { readFileSync } from "node:fs";

/** One note in core's notes.json format. Ticks at `ppq`; `vel` 1-127; `pitch` MIDI number. */
export interface Note { tick: number; dur: number; pitch: number; vel: number }

export interface Part {
  id: string;
  role: string;
  name: string;
  /** 1-based MIDI channel (drums = 10). */
  channel: number;
  notes: Note[];
}

/** The note-list format `core` emits (`fs-realize --out-notes`) and baseline-v1 mirrors. */
export interface NotesFile {
  ppq: number;
  bars: number;
  ticksPerBar: number;
  tempo: number;
  meter: [number, number];
  parts: Part[];
}

export interface PromptControls {
  tonic: string;
  mode: string;
  bars: number;
  tempo: number;
  meterNumerator: number;
  meterDenominator: number;
  style: string[];
  lanes: string[];
}

export interface PromptEntry { id: string; prompt: string; controls: PromptControls }

export function loadPrompts(file: string): PromptEntry[] {
  return JSON.parse(readFileSync(file, "utf8")) as PromptEntry[];
}

const LETTER: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

export function tonicPitchClass(tonic: string): number {
  const m = /^([A-G])([#b]?)$/.exec(tonic);
  if (!m || !m[1]) throw new Error(`bad tonic ${tonic}`);
  const base = LETTER[m[1]] ?? 0;
  return (base + (m[2] === "#" ? 1 : m[2] === "b" ? 11 : 0)) % 12;
}

/** Scale intervals for every score-IR mode (docs/ir-spec.md). */
export const MODE_INTERVALS: Record<string, number[]> = {
  major: [0, 2, 4, 5, 7, 9, 11],
  minor: [0, 2, 3, 5, 7, 8, 10],
  dorian: [0, 2, 3, 5, 7, 9, 10],
  phrygian: [0, 1, 3, 5, 7, 8, 10],
  lydian: [0, 2, 4, 6, 7, 9, 11],
  mixolydian: [0, 2, 4, 5, 7, 9, 10],
  locrian: [0, 1, 3, 5, 6, 8, 10],
  harmonic_minor: [0, 2, 3, 5, 7, 8, 11],
  melodic_minor: [0, 2, 3, 5, 7, 9, 11],
  major_pentatonic: [0, 2, 4, 7, 9],
  minor_pentatonic: [0, 3, 5, 7, 10],
  blues: [0, 3, 5, 6, 7, 10],
};

export function scalePitchClasses(tonic: string, mode: string): Set<number> {
  const intervals = MODE_INTERVALS[mode];
  if (!intervals) throw new Error(`unknown mode ${mode}`);
  const t = tonicPitchClass(tonic);
  return new Set(intervals.map((i) => (t + i) % 12));
}

/** Parses `--name value` style flags; positional args are returned in `_`. */
export function parseArgs(argv: string[]): { _: string[]; flags: Record<string, string> } {
  const out: { _: string[]; flags: Record<string, string> } = { _: [], flags: {} };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i] ?? "";
    if (a.startsWith("--")) {
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("--")) { out.flags[a.slice(2)] = next; i++; }
      else out.flags[a.slice(2)] = "true";
    } else out._.push(a);
  }
  return out;
}
