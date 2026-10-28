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
  "conga_high", "conga_low", "conga_mute", "bongo_high", "bongo_low", "clave", "timbale_high", "timbale_low", "agogo_high", "agogo_low", "cabasa", "guiro", "woodblock", "triangle",
]);

export const Fill = z.enum(["none", "snare_roll", "tom_run", "kick_build", "crash_end", "half_time_break"]);

/**
 * Named drum grooves in core's library (core/src/grooves.cpp): idiomatic patterns a drums block calls by name.
 * Each is for one meter, with the tempo range (BPM) it suits and the context swing it pairs with (0 = straight).
 * A test in core checks the names, meters and descriptions against core's.
 */
export const GROOVES = {
  four_on_floor: { meter: [4, 4], tempo: [120, 128], swing: 0, description: "house: kick on every beat, clap on 2 and 4, off-beat open hats, rolling 16th shaker" },
  tech_house: { meter: [4, 4], tempo: [124, 128], swing: 0, description: "tech house: four-on-the-floor kick, clap on 2 and 4, off-beat pedal hat, ride on the beat, a pushed ghost snare" },
  boom_bap: { meter: [4, 4], tempo: [86, 96], swing: 0.15, description: "boom bap: syncopated kick, laid-back snare on 2 and 4, 8th hats (pair with swing around 0.15)" },
  lofi: { meter: [4, 4], tempo: [70, 90], swing: 0.2, description: "lofi: soft kick with an off-beat push, late snare and clap on 2 and 4, ghosted hats (pair with swing)" },
  trap: { meter: [4, 4], tempo: [130, 170], swing: 0, description: "trap (half-time at 130-170 BPM): sparse kicks, clap on 3, 8th hats with a 32nd roll before the bar line" },
  drill: { meter: [4, 4], tempo: [138, 146], swing: 0, description: "UK drill: half-time snare on 3 then on 4 in bar 2, sliding kicks off the downbeat, tresillo hats" },
  dembow: { meter: [4, 4], tempo: [88, 100], swing: 0, description: "reggaeton dembow: kick on every beat, snare on the 3-3-2 dembow accents (soft, then strong), light 8th hats" },
  funk: { meter: [4, 4], tempo: [95, 110], swing: 0.1, description: "funk (Funky Drummer): syncopated kick, backbeat snare with ghost notes, accented 16th hats opening on the e of 2 and 4" },
  rock: { meter: [4, 4], tempo: [110, 140], swing: 0, description: "rock: kick on 1 and 3 with pushes, snare on 2 and 4, 8th hats, crash on the first downbeat" },
  pop: { meter: [4, 4], tempo: [100, 124], swing: 0, description: "pop: kick on 1 and the and of 2 and 3, clap and tambourine on 2 and 4, 8th hats" },
  ballad: { meter: [4, 4], tempo: [60, 80], swing: 0, description: "ballad: soft kick on 1 and 3, cross-stick on 2 and 4, gentle 8th hats" },
  neo_soul: { meter: [4, 4], tempo: [75, 92], swing: 0.15, description: "neo-soul: behind-the-beat pocket, late snare and hats, ghosted 16ths" },
  dnb: { meter: [4, 4], tempo: [170, 176], swing: 0, description: "drum and bass: two-step kick and snare with ghost snares, ride 8ths, off-beat hats" },
  half_time: { meter: [4, 4], tempo: [60, 80], swing: 0, description: "half-time: kick on 1, snare on 3, 8th hats; slow hip hop and builds" },
  cinematic_toms: { meter: [4, 4], tempo: [80, 110], swing: 0, description: "cinematic: big low-tom ostinato over kick on 1 and 3, crash on the first downbeat" },
  sparse_pulse: { meter: [4, 4], tempo: [60, 90], swing: 0, description: "ambient: a soft pulse; kick on 1, ride quarter notes, light shaker" },
  afrobeats: { meter: [4, 4], tempo: [100, 115], swing: 0.15, description: "afrobeats: Naija afropop pocket; kick on 1, the and of 2 and 3 (a push on the and of 4 in bar 2), rim on the 3-2 clave skips, clap on 4, galloping hats, swung 16th shaker, conga answers (100-115 BPM, pair with swing around 0.15)" },
  afrobeats_bounce: { meter: [4, 4], tempo: [105, 118], swing: 0.18, description: "afrobeats: bouncy percussion-led afropop; 3-3-2 kick, rim answering on the e and a, clap on 2 and 4, agogo bell line, rolling 16th shaker, interlocking congas and bongos (105-118 BPM, pair with swing around 0.18)" },
  afrobeats_afrofusion: { meter: [4, 4], tempo: [85, 100], swing: 0.22, description: "afrobeats: slow afro-fusion / afro-R&B; soft kick on 1, the a of 2 and 3, late rim on the clave skips, snap-clap on 4, laid-back hats with ghosts, light shaker and conga (85-100 BPM, pair with swing around 0.22)" },
  afroswing: { meter: [4, 4], tempo: [95, 110], swing: 0.25, description: "afroswing: UK afroswing (J Hus, Kojo Funds); dancehall-style 3-3-2 kick, clap on 2 and 4, rim skip on the a of 2 and the and of 4, swung galloping hats and shaker (95-110 BPM, pair with swing around 0.25)" },
  amapiano: { meter: [4, 4], tempo: [110, 115], swing: 0.2, description: "amapiano: classic piano; soft four-on-the-floor kick, quiet clap on 2 and 4, off-beat open hat, swung 16th shaker, syncopated rim and conga clave around the log drum (110-115 BPM, pair with swing around 0.2)" },
  amapiano_private_school: { meter: [4, 4], tempo: [108, 113], swing: 0.25, description: "amapiano: private school; soft kick on 1 and 3, late rim and shaker, off-beat open hat, brushed snare ghosts, lots of space for keys and log drum (108-113 BPM, pair with swing around 0.25)" },
  amapiano_bacardi: { meter: [4, 4], tempo: [112, 118], swing: 0.15, description: "amapiano: bacardi / sgija; busy syncopated kick, clipped snare on 2 and 4, rattling accented 16th shaker, syncopated low and mid toms, off-beat open hat, clap pickups (112-118 BPM, pair with swing around 0.15)" },
  afro_house: { meter: [4, 4], tempo: [118, 124], swing: 0.12, description: "afro house: four-on-the-floor kick, clap on 2 and 4, off-beat open hat, 16th shaker, rim on the 3-2 clave, conga and bongo interplay (118-124 BPM, pair with swing around 0.12)" },
  afro_house_tribal: { meter: [4, 4], tempo: [118, 124], swing: 0.1, description: "afro house: tribal / percussive; four-on-the-floor kick, no clap, toms carrying the 3-3-2, 16th shaker, clave alternating 3-2 and 2-3, woodblock and off-beat cowbell (118-124 BPM, pair with swing around 0.1)" },
  gqom: { meter: [4, 4], tempo: [120, 128], swing: 0, description: "gqom: Durban broken beat; no four-on-the-floor, kick on 1 and the 3-3-2 skips with gaps, heavy toms answering, snare on 3, sparse off-beat shaker, rim stabs (120-128 BPM)" },
  dancehall: { meter: [4, 4], tempo: [90, 105], swing: 0.08, description: "dancehall: modern bounce; kick on 1 and the a of 1 in each half bar, snare on the and of 2 and the and of 4 (together the 3-3-2), rim on 2 and 4, 8th hats, 16th shaker (90-105 BPM, pair with swing around 0.08)" },
  dancehall_steppers: { meter: [4, 4], tempo: [95, 110], swing: 0, description: "dancehall: digital steppers; four-on-the-floor kick under a snare punching the full 3-3-2 twice a bar, closed hats around an off-beat open hat (95-110 BPM)" },
  reggae_one_drop: { meter: [4, 4], tempo: [65, 85], swing: 0, description: "reggae one drop (triplet grid): kick and cross-stick together on beat 3 only, nothing on 1, shuffled triplet hats, hat foot on 2 and 4 (65-85 BPM)" },
  reggaeton_perreo: { meter: [4, 4], tempo: [88, 98], swing: 0, description: "reggaeton: modern perreo; four-on-the-floor kick, dembow snare with clap on the strong hits, a pickup in bar 2, accented 16th hats, open hat and timbale fill at the end of bar 2 (88-98 BPM)" },
  moombahton: { meter: [4, 4], tempo: [105, 112], swing: 0, description: "moombahton: Dutch-house reggaeton; four-on-the-floor kick, clap on the dembow accents, rolling 16th hats, off-beat open hat, tom pickup in bar 2 (105-112 BPM)" },
  baile_funk: { meter: [4, 4], tempo: [125, 135], swing: 0, description: "baile funk: tamborz\u00e3o (funk carioca); heavy kick cell on 1, the a of 1, the and of 2, 3 and the e of 4, conga/atabaque answers, clap on 2 and 4, dry rim (125-135 BPM)" },
  baile_funk_mandelao: { meter: [4, 4], tempo: [130, 150], swing: 0, description: "baile funk: mandel\u00e3o / funk automotivo (S\u00e3o Paulo); fast, kick on the 3-3-2 every half bar, clap on 2 and 4, off-beat rim, cowbell stabs (130-150 BPM)" },
  trap_triplet: { meter: [4, 4], tempo: [130, 160], swing: 0, description: "trap: triplet-hat flow (16th-triplet grid); kick on 1 and pushes, clap on 3, hats in 8th triplets with a 16th-triplet roll at the end of bar 2, open hat answer (130-160 BPM)" },
  trap_rage: { meter: [4, 4], tempo: [145, 170], swing: 0, description: "trap: rage (half-time); hard kick on 1 and stuttered pushes, clap on 3, snare pickup at the end of bar 2, plain 8th hats, open hat on the and of 4, crash every 2 bars (145-170 BPM)" },
  trap_plugg: { meter: [4, 4], tempo: [140, 160], swing: 0, description: "trap: plugg / pluggnb (half-time); bouncy kick on 1, the a of 2 and the and of 3, clap on 3, 8th hats with occasional 16th doubles, rim on the a of 4, soft open hat (140-160 BPM)" },
  trap_soul: { meter: [4, 4], tempo: [120, 150], swing: 0.1, description: "trap soul (half-time): Bryson Tiller-style; sparse kick, late clap on 3, 8th hats with 16th flourishes, ghosted rim, soft shaker (120-150 BPM, pair with swing around 0.1)" },
  drill_ny: { meter: [4, 4], tempo: [140, 145], swing: 0, description: "drill: NY / Brooklyn drill (Pop Smoke, 808Melo); half-time snare on 3 with a ghost before it and a pickup in bar 2, bouncy syncopated hats, kick on 1 and the a of 2, perc rim stabs (140-145 BPM)" },
  phonk_drift: { meter: [4, 4], tempo: [120, 140], swing: 0, description: "phonk: drift phonk; hard kick on 1 and the a of 2, clap on 2 and 4, driving cowbell on the 3-3-2, 16th hats, open hat on the and of 4 (120-140 BPM)" },
  phonk_memphis: { meter: [4, 4], tempo: [130, 150], swing: 0, description: "phonk: Memphis / classic phonk (half-time); 808 kick on 1 and pushes, clap and snare on 3, 16th 808 hats, cowbell off-beats (130-150 BPM)" },
  jersey_club: { meter: [4, 4], tempo: [130, 145], swing: 0, description: "jersey club: the five-kick bounce on 1, 2, 3, the a of 3 and the and of 4; clap on 2 and 4, 8th hats, rim on the and (130-145 BPM)" },
  jersey_club_bounce: { meter: [4, 4], tempo: [135, 150], swing: 0, description: "jersey club: kick-roll variant; five-kick pattern in bar 1, bar 2 rolls 1-3-5-9-11-13-15, clap on 2 and 4 with a pickup on the a of 4, 16th hats (135-150 BPM)" },
  uk_garage: { meter: [4, 4], tempo: [130, 138], swing: 0.45, description: "uk garage: 2-step; kick on 1 and the and of 3 (skipping beat 3), snare on 2 and 4, shuffled off-beat hats with ghosts, syncopated rim, swung shaker (130-138 BPM, pair with swing around 0.45)" },
  uk_garage_shuffle: { meter: [4, 4], tempo: [120, 128], swing: 0.45, description: "uk garage: 4x4 garage shuffle; kick on 1 with soft off-beat kicks, snare on 2 and 4 with a ghost at the bar end, low clap under, swung ghosted hats and shaker (120-128 BPM, pair with swing around 0.45)" },
  uk_funky: { meter: [4, 4], tempo: [128, 132], swing: 0.3, description: "uk funky: tribal syncopated kick, clap on 2 and 4, rim on the 3-3-2 skips, congas, off-beat open hat, swung 16th shaker (128-132 BPM, pair with swing around 0.3)" },
  jungle: { meter: [4, 4], tempo: [160, 175], swing: 0, description: "jungle: chopped Amen break; kick on 1, the and of 1 and the and/a of 3, snares on 2 and 4 with the Amen's skipping ghosts, 8th ride, crash in bar 2 (160-175 BPM)" },
  bossa_nova: { meter: [4, 4], tempo: [120, 145], swing: 0, description: "bossa nova: cross-stick on the bossa clave over two bars, kick on 1 and 3 with pickups on the and of 2 and 4, 8th hats, hat foot on 2 and 4 (120-145 BPM)" },
  samba: { meter: [4, 4], tempo: [90, 110], swing: 0, description: "samba: surdo kick on every beat with pickups and accents on 2 and 4, 16th ganz\u00e1 shaker accented on the a, tamborim rim figure over two bars, two-tone agogo (90-110 BPM)" },
  cumbia: { meter: [4, 4], tempo: [85, 100], swing: 0, description: "cumbia: kick on 1 and 3, guiro scraping an 8th and two 16ths, cowbell on the off-beats, conga mutes on the beat with open tones on the and of 2 and 4, cross-stick on 2 and 4 (85-100 BPM)" },
  salsa: { meter: [4, 4], tempo: [85, 105], swing: 0, description: "salsa: son clave 2-3 (one 4/4 bar = one cut-time clave cycle); campana bell on the beats, conga tumbao with a slap and open tones on 4 and the and of 4, light kick on the bass anticipations (85-105 BPM)" },
  rnb: { meter: [4, 4], tempo: [60, 80], swing: 0.12, description: "r&b: modern R&B (SZA, Summer Walker); sparse kick on 1 and the a of 2, late snap-clap on 2 and 4, 16th hats with rolls, ghost rim, light shaker (60-80 BPM, pair with swing around 0.12)" },
  rnb_new_jack_swing: { meter: [4, 4], tempo: [100, 115], swing: 0.45, description: "r&b: 90s new jack swing; punchy kick on 1, the and of 2 and the and of 3, snare and clap on 2 and 4, heavily swung 16th hats, tambourine on 2 and 4 (100-115 BPM, pair with swing around 0.45)" },
  gfunk: { meter: [4, 4], tempo: [88, 98], swing: 0.12, description: "g-funk: West Coast; laid-back kick on 1, the a of 2 and 3, snare and clap on 2 and 4, accented 16th hats, 8th shaker, open hat on the and of 4 (88-98 BPM, pair with swing around 0.12)" },
  disco: { meter: [4, 4], tempo: [110, 125], swing: 0, description: "disco: four-on-the-floor kick, snare on 2 and 4, open hat on every off-beat with closed hats around it, tambourine on 2 and 4 (110-125 BPM)" },
  jazz_swing: { meter: [4, 4], tempo: [120, 180], swing: 0, description: "jazz swing (triplet grid): spang-a-lang ride, hi-hat foot on 2 and 4, feathered kick, cross-stick on 4" },
  brush_swing: { meter: [4, 4], tempo: [90, 130], swing: 0, description: "brushes (triplet grid): swirling brush snare, feathered kick, hi-hat foot on 2 and 4" },
  jazz_waltz: { meter: [3, 4], tempo: [120, 180], swing: 0, description: "jazz waltz (triplet grid): skipping ride, hi-hat foot on 2 and 3, feathered kick" },
  waltz: { meter: [3, 4], tempo: [80, 100], swing: 0, description: "waltz: kick on 1, cross-stick on 2 and 3, 8th hats" },
  six_eight: { meter: [6, 8], tempo: [60, 80], swing: 0, description: "6/8: kick on 1, snare on 4, every eighth on the hats with accents on 1 and 4; ballads and folk" },
  gospel_shuffle: { meter: [6, 8], tempo: [65, 85], swing: 0, description: "gospel 6/8: kick on 1 with a pickup on 6, snare on 4 with a ghost, accented hats" },
} as const satisfies Record<string, { meter: readonly [number, number]; tempo: readonly [number, number]; swing: number; description: string }>;

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

export const NoteRemoval = z.object({
  bar: z.number().int(),
  beat: z.number(),
  pitch: NoteName,
});

export const Touch = z.object({
  remove: z.array(NoteRemoval).describe("Realized notes taken out: the note of this pitch nearest this onset"),
  add: z.array(LiteralNote).describe("Notes added, played exactly as written"),
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
  density: z.number().min(0).max(1).nullable().optional().describe("Set by simplify/intensify: 0..1, 0.5 or absent = as written"),
  humanize: z.number().min(0).max(1).nullable().optional().describe("Set by the humanize tweak: 0 = on the grid, absent = the default 0.3, 1 = loose"),
  seed: z.number().int().min(0).max(4294967295).nullable().optional().describe("Set by re-roll: this part's own seed for seeded realization choices"),
  touch: Touch.nullable().optional().describe("Set by the note editor: note edits applied after realization"),
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
