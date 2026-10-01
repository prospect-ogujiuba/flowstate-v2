// Sound templates for rendered listening: which General MIDI instrument plays each role, and which GS drum
// kit plays the drums, by genre family. The same template is used for both options of a prompt, so only the
// notes differ. Edit freely: change a program number, or add a family or keyword.
//
// Programs are 0-based General MIDI numbers (0 = Acoustic Grand Piano). Drum kits are GS kit programs on
// channel 10. GeneralUser GS (see evals/README.md) has all of them.

export type Role = "chords" | "pad" | "arp" | "bass" | "melody" | "counter" | "drums";

export interface Template {
  family: string;
  /** GM program per pitched role. */
  programs: Record<Exclude<Role, "drums">, number>;
  /** GS drum kit program. */
  kit: number;
  /** Mix: CC7 volume per role (0-127). */
  volume: Record<Role, number>;
  /** CC91 reverb send for every channel. */
  reverb: number;
}

export const GM = {
  piano: 0, brightPiano: 1, ep1: 4, ep2: 5, clav: 7, celesta: 8, glockenspiel: 9, musicBox: 10, vibes: 11, marimba: 12,
  tubularBells: 14, organ: 16, percOrgan: 17, rockOrgan: 18, churchOrgan: 19, nylonGuitar: 24, steelGuitar: 25,
  cleanGuitar: 27, mutedGuitar: 28, overdriveGuitar: 29, distGuitar: 30, acousticBass: 32, fingerBass: 33,
  pickBass: 34, fretlessBass: 35, synthBass1: 38, synthBass2: 39, strings: 48, slowStrings: 49, synthStrings: 50,
  choir: 52, trumpet: 56, trombone: 57, frenchHorn: 60, brass: 61, altoSax: 65, tenorSax: 66, flute: 73,
  panFlute: 75, squareLead: 80, sawLead: 81, warmPad: 89, polyPad: 90, haloPad: 94, sweepPad: 95,
} as const;

export const KIT = { standard: 0, room: 8, power: 16, electronic: 24, tr808: 25, jazz: 32, brush: 40, orchestra: 48 } as const;

const MIX: Template["volume"] = { chords: 92, pad: 88, arp: 84, bass: 105, melody: 100, counter: 90, drums: 110 };

const base = (family: string, chords: number, bass: number, melody: number, kit: number, reverb = 40): Template => ({
  family,
  programs: { chords, pad: GM.warmPad, arp: chords, bass, melody, counter: melody },
  kit,
  volume: MIX,
  reverb,
});

/** Families, matched by the first style tag that names one of their styles. */
export const FAMILIES: { styles: string[]; template: Template }[] = [
  { styles: ["trap", "drill", "uk-drill", "reggaeton", "dembow", "latin"], template: base("808", GM.piano, GM.synthBass1, GM.musicBox, KIT.tr808, 30) },
  { styles: ["house", "deep-house", "tech-house", "techno", "edm", "dance", "drum-and-bass", "dnb", "liquid"], template: base("909", GM.ep1, GM.synthBass2, GM.squareLead, KIT.electronic, 35) },
  { styles: ["lofi", "lo-fi", "hip-hop", "boom-bap"], template: base("dusty", GM.ep1, GM.fingerBass, GM.vibes, KIT.room, 35) },
  { styles: ["jazz", "swing", "bossa"], template: base("brushes", GM.piano, GM.acousticBass, GM.vibes, KIT.brush, 45) },
  { styles: ["cinematic", "orchestral", "ambient", "folk", "film"], template: base("cinematic", GM.strings, GM.fingerBass, GM.flute, KIT.orchestra, 70) },
  { styles: ["afrobeats", "afro", "amapiano", "dancehall"], template: base("afro", GM.cleanGuitar, GM.synthBass1, GM.squareLead, KIT.standard, 30) },
  { styles: ["rock", "metal", "punk"], template: base("rock", GM.overdriveGuitar, GM.pickBass, GM.sawLead, KIT.power, 30) },
  { styles: ["neo-soul", "rnb", "soul", "gospel", "funk", "pop", "ballad"], template: base("acoustic", GM.ep1, GM.fingerBass, GM.vibes, KIT.standard, 40) },
];

const FALLBACK = base("acoustic", GM.ep1, GM.fingerBass, GM.vibes, KIT.standard, 40);

/** Instrument words in a prompt clause, and the program they stand for. First match in a clause wins. */
export const KEYWORDS: [RegExp, number][] = [
  [/rhodes|electric piano|wurli/, GM.ep1],
  [/clav/, GM.clav],
  [/organ/, GM.percOrgan],
  [/piano/, GM.piano],
  [/bells?\b|chime/, GM.tubularBells],
  [/music box|celesta/, GM.musicBox],
  [/vibes|vibraphone|mallet/, GM.vibes],
  [/marimba|kalimba/, GM.marimba],
  [/french.horn|horns?\b/, GM.frenchHorn],
  [/brass|stabs? of brass/, GM.brass],
  [/sax/, GM.altoSax],
  [/trumpet/, GM.trumpet],
  [/flute|pan pipe/, GM.flute],
  [/choir|vocal|voices/, GM.choir],
  [/string/, GM.strings],
  [/guitar|pluck/, GM.cleanGuitar],
  [/synth lead|lead synth/, GM.sawLead],
  [/pad/, GM.warmPad],
];

const ROLE_WORDS: [RegExp, Role[]][] = [
  [/melod|lead|hook|topline|call|riff|line\b/, ["melody", "counter"]],
  [/chord|stab|progression|comp|vamp|harmon/, ["chords", "arp"]],
  [/pad/, ["pad"]],
];

/**
 * The template for a prompt: the family from its style tags, then instrument words in the prompt text
 * applied to the role named in the same clause ("an eerie bell melody" sets the melody to bells).
 */
export function templateFor(style: string[], prompt: string): Template {
  const tags = style.map((s) => s.toLowerCase());
  const found = FAMILIES.find((f) => tags.some((t) => f.styles.includes(t)))?.template ?? FALLBACK;
  const t: Template = { ...found, programs: { ...found.programs } };
  for (const clause of prompt.toLowerCase().split(/,|;| and | with /)) {
    const program = KEYWORDS.find(([re]) => re.test(clause))?.[1];
    if (program === undefined) continue;
    const roles = ROLE_WORDS.find(([re]) => re.test(clause))?.[1];
    for (const role of roles ?? []) t.programs[role as Exclude<Role, "drums">] = program;
  }
  return t;
}
