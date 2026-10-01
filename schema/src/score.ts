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

/**
 * Named drum grooves in core's library (core/src/grooves.cpp): idiomatic patterns a drums block calls by name.
 * Each is for one meter. A test in core checks this table against core's.
 */
export const GROOVES = {
  four_on_floor: { meter: [4, 4], description: "house: kick on every beat, clap on 2 and 4, off-beat open hats, 16th shaker" },
  tech_house: { meter: [4, 4], description: "tech house: four-on-the-floor kick, clap on 2 and 4, rolling 16th hats, syncopated rim" },
  boom_bap: { meter: [4, 4], description: "boom bap: swung kick pattern, laid-back snare on 2 and 4, accented 8th hats" },
  lofi: { meter: [4, 4], description: "lofi: sparse, lazy boom bap; soft kick, late snare, ghosted hats" },
  trap: { meter: [4, 4], description: "trap: half-time snare on 3, sparse kicks, 8th hats with 16th rolls" },
  drill: { meter: [4, 4], description: "UK drill: half-time snare on 3 with a late extra snare, sliding kicks, skippy hats" },
  dembow: { meter: [4, 4], description: "reggaeton dembow: kick on every beat, snare on the 3-3-2 dembow accents, 8th hats" },
  afrobeats: { meter: [4, 4], description: "afrobeats: tresillo kick, rim clave, soft clap on 2 and 4, busy 16th shaker, conga-style toms" },
  funk: { meter: [4, 4], description: "funk: syncopated kick, backbeat snare with ghost notes, 16th hats, open hat on the and of 4" },
  rock: { meter: [4, 4], description: "rock: kick on 1 and 3 with pushes, snare on 2 and 4, 8th hats, crash on the first downbeat" },
  pop: { meter: [4, 4], description: "pop: kick on 1 and the and of 2 and 3, clap and tambourine on 2 and 4, 8th hats" },
  ballad: { meter: [4, 4], description: "ballad: soft kick on 1 and 3, cross-stick on 2 and 4, gentle 8th hats" },
  neo_soul: { meter: [4, 4], description: "neo-soul: behind-the-beat pocket, late snare and hats, ghosted 16ths" },
  dnb: { meter: [4, 4], description: "drum and bass: two-step kick and snare with a ghost, 8th hats, 16th shaker" },
  half_time: { meter: [4, 4], description: "half-time: kick on 1, snare on 3, 8th hats; slow hip hop and builds" },
  cinematic_toms: { meter: [4, 4], description: "cinematic: big low-tom ostinato over kick on 1 and 3, crash on the first downbeat" },
  sparse_pulse: { meter: [4, 4], description: "ambient: a soft pulse; kick on 1, ride quarter notes, light shaker" },
  jazz_swing: { meter: [4, 4], description: "jazz swing (triplet grid): spang-a-lang ride, hi-hat foot on 2 and 4, feathered kick" },
  brush_swing: { meter: [4, 4], description: "brushes (triplet grid): swirling brush snare, feathered kick, hi-hat foot on 2 and 4" },
  jazz_waltz: { meter: [3, 4], description: "jazz waltz (triplet grid): skipping ride, hi-hat foot on 2 and 3, feathered kick" },
  waltz: { meter: [3, 4], description: "waltz: kick on 1, cross-stick on 2 and 3, 8th hats" },
  six_eight: { meter: [6, 8], description: "6/8: kick on 1, snare on 4, every eighth on the hats; ballads and folk" },
  gospel_shuffle: { meter: [6, 8], description: "gospel 6/8: kick on 1 with a push, snare backbeat on 4 with a ghost, accented hats" },
} as const satisfies Record<string, { meter: readonly [number, number]; description: string }>;

export const GrooveName = z.enum(Object.keys(GROOVES) as [keyof typeof GROOVES, ...(keyof typeof GROOVES)[]]).meta({
  description: "Named drum groove (docs/ir-spec.md, Grooves)",
  grooves: GROOVES,
});

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
  rhythm: z.string().nullable().optional().describe("Step string; tokens depend on role (see spec)"),
  voicing: Voicing.nullable().optional(),
  arpPattern: ArpPattern.nullable().optional(),
  motif: z.string().nullable().optional().describe("Motif id for melody/counter"),
  transforms: z.array(z.string()).nullable().optional().describe("e.g. transpose:+2, invert, retrograde, displace:+0.5, augment, diminish, octave:+1"),
  repeatEvery: z.number().nullable().optional().describe("Motif repeat period in beats; 0 = once"),
  drums: z.array(DrumLane).nullable().optional(),
  groove: GrooveName.nullable().optional().describe("Drums: a named groove; lanes in `drums` only add voices it doesn't have"),
  fill: Fill.nullable().optional(),
  notes: z.array(LiteralNote).nullable().optional(),
  articulation: Articulation.nullable().optional(),
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
  harmony: z.union([
    z.string().describe('Compact form: "<chord>:<beats>" tokens from bar 1 beat 1 in sequence, "r:<beats>" for no chord, e.g. "Dm9:4 | G13:4 | Em7:2 A7:2"'),
    z.array(Chord).describe("Chord-object form (older scores)"),
  ]),
  motifs: z.array(Motif),
  parts: z.array(Part),
});

export type Score = z.infer<typeof Score>;
export type Part = z.infer<typeof Part>;
export type Block = z.infer<typeof Block>;
