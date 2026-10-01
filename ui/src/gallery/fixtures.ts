// Hand-written sample notes for the gallery (beats, MIDI pitch). Display data only: nothing here
// is generated, so no theory lives in the UI.
import type { RollNote } from "../components/index.ts";

const n = (pitch: number, start: number, length: number, velocity = 96): RollNote => ({ pitch, start, length, velocity });

export const chords: RollNote[] = [
  ...[57, 60, 64].map((p) => n(p, 0, 4, 84)),
  ...[53, 57, 60].map((p) => n(p, 4, 4, 84)),
  ...[55, 59, 62].map((p) => n(p, 8, 4, 84)),
  ...[52, 55, 59].map((p) => n(p, 12, 4, 84)),
];

export const bass: RollNote[] = [
  n(45, 0, 1.5), n(45, 1.5, 0.5, 70), n(45, 2, 1), n(52, 3, 1, 80),
  n(41, 4, 1.5), n(41, 5.5, 0.5, 70), n(41, 6, 2),
  n(43, 8, 1.5), n(43, 9.5, 0.5, 70), n(43, 10, 1), n(50, 11, 1, 80),
  n(40, 12, 3), n(47, 15, 1, 80),
];

export const melody: RollNote[] = [
  n(76, 0.5, 0.5), n(74, 1, 0.5), n(72, 1.5, 1.5, 110), n(69, 3, 1),
  n(72, 4.5, 0.5), n(74, 5, 1), n(72, 6, 2, 80),
  n(71, 8.5, 0.5), n(72, 9, 0.5), n(74, 9.5, 1.5, 110), n(79, 11, 1),
  n(76, 12, 2), n(74, 14, 1), n(72, 15, 1, 80),
];

const hits = (pitch: number, beats: number[], velocity = 100) => beats.map((b) => n(pitch, b, 0.25, velocity));
const every = (from: number, step: number, to: number) => Array.from({ length: Math.floor((to - from) / step) }, (_, i) => from + i * step);

export const drumSublanes = [
  { id: "kick", name: "Kick", notes: hits(36, [0, 2.5, 4, 6.5, 8, 10.5, 12, 14.5]), muted: false },
  { id: "snare", name: "Snare", notes: hits(38, [1, 3, 5, 7, 9, 11, 13, 15]), muted: false },
  { id: "hat", name: "Closed hat", notes: hits(42, every(0, 0.5, 16), 70), muted: false },
  { id: "open", name: "Open hat", notes: hits(46, [3.5, 7.5, 11.5, 15.5], 80), muted: true },
];

export const drums: RollNote[] = drumSublanes.flatMap((s) => s.notes);
