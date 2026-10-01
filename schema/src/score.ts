// Score IR v0 — source of truth. Semantics: docs/ir-spec.md.
// Structured-output friendly: fixed keys, every key required, nullable instead of optional.
import { z } from "zod";

export const IR_ID = "flowstate.score.v0";

export const Tonic = z.enum([
  "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B",
]);

export const Mode = z.enum([
  "major", "minor", "dorian", "phrygian", "lydian", "mixolydian", "locrian",
  "harmonic_minor", "melodic_minor", "major_pentatonic", "minor_pentatonic", "blues",
]);

export const Role = z.enum(["chords", "pad", "arp", "bass", "melody", "counter", "drums"]);

export const Voicing = z.enum([
  "close", "open", "drop2", "drop3", "rootless", "shell", "spread", "quartal", "power",
]);

export const ArpPattern = z.enum(["up", "down", "updown", "random", "chord_tones"]);

export const Articulation = z.enum(["legato", "normal", "staccato"]);

export const DrumVoice = z.enum([
  "kick", "snare", "clap", "rim", "closed_hat", "pedal_hat", "open_hat",
  "low_tom", "mid_tom", "high_tom", "crash", "ride", "ride_bell", "shaker", "tambourine", "cowbell",
]);

export const Fill = z.enum(["none", "snare_roll", "tom_run", "kick_build", "crash_end", "half_time_break"]);

// Note names like "C4", "F#2", "Bb5". C4 = MIDI 60.
export const NoteName = z.string().describe("Note name with octave, e.g. C4 (=MIDI 60), F#2, Bb5");

export const Context = z.object({
  tempo: z.number().describe("BPM"),
  meterNumerator: z.number().int(),
  meterDenominator: z.number().int(),
  tonic: Tonic,
  mode: Mode,
  bars: z.number().int().describe("Clip length in bars; authoritative"),
  swing: z.number().describe("0 straight .. 0.5 heavy swing on off-beat steps"),
  style: z.array(z.string()),
});

export const Section = z.object({
  name: z.string(),
  startBar: z.number().int(),
  bars: z.number().int(),
  energy: z.number().describe("0..1"),
});

export const Chord = z.object({
  bar: z.number().int().describe("1-based bar"),
  beat: z.number().describe("1-based beat within the bar"),
  beats: z.number().describe("Duration in beats"),
  symbol: z.string().describe("Chord symbol, e.g. Cm9, G13, F/A, Bb7#11"),
});

export const MotifNote = z.object({
  degree: z.number().int().describe("1-based scale degree of the mode; 8 = tonic an octave up"),
  octave: z.number().int().describe("Octave offset from the part's register centre"),
  alter: z.number().int().describe("-1, 0 or +1 semitone chromatic alteration"),
  beat: z.number().describe("Onset offset in beats from motif start (0-based)"),
  beats: z.number().describe("Duration in beats"),
  accent: z.boolean(),
});

export const Motif = z.object({
  id: z.string(),
  notes: z.union([
    z.string().describe('Compact form: "<pitch>:<beats>[!]" tokens played in sequence, e.g. "5:.75! 4:.25 b3:1 r:.5 1+:1/3"'),
    z.array(MotifNote).describe("Note-object form (older scores)"),
  ]),
});

export const DrumLane = z.object({
  voice: DrumVoice,
  steps: z.string().describe("Step string: x hit, X accent, g ghost, . rest; | separates bars"),
});

export const LiteralNote = z.object({
  bar: z.number().int(),
  beat: z.number(),
  beats: z.number(),
  pitch: NoteName,
  velocity: z.number().int(),
});

export const Block = z.object({
  startBar: z.number().int(),
  endBar: z.number().int().describe("Inclusive"),
  rhythm: z.string().nullable().describe("Step string; tokens depend on role (see spec)"),
  voicing: Voicing.nullable(),
  arpPattern: ArpPattern.nullable(),
  motif: z.string().nullable().describe("Motif id for melody/counter"),
  transforms: z.array(z.string()).nullable().describe("e.g. transpose:+2, invert, retrograde, displace:+0.5, augment, diminish, octave:+1"),
  repeatEvery: z.number().nullable().describe("Motif repeat period in beats; 0 = once"),
  drums: z.array(DrumLane).nullable(),
  fill: Fill.nullable(),
  notes: z.array(LiteralNote).nullable(),
  articulation: Articulation.nullable(),
});

export const Part = z.object({
  id: z.string(),
  role: Role,
  name: z.string(),
  low: NoteName,
  high: NoteName,
  grid: z.number().int().describe("Steps per beat for step strings (4 = 16ths in 4/4, 3 = triplets)"),
  velocity: z.number().int(),
  blocks: z.array(Block),
});

export const Score = z.object({
  ir: z.literal(IR_ID),
  title: z.string(),
  context: Context,
  form: z.array(Section),
  harmony: z.array(Chord),
  motifs: z.array(Motif),
  parts: z.array(Part),
});

export type Score = z.infer<typeof Score>;
export type Part = z.infer<typeof Part>;
export type Block = z.infer<typeof Block>;
