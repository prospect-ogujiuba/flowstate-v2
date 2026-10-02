// Display mapping from the session's Clip (core's realization, in ticks) to what the lanes draw
// (beats). Unit conversion and labels only: no theory lives in the UI.

import type { Session } from "@flowstate/schema";
import type { PartColour, RollNote, Sublane } from "../components/index.ts";

export type Clip = NonNullable<Session["clip"]>;
export type ClipPart = Clip["parts"][number];
export type Role = ClipPart["role"];

export const rollNotes = (notes: ClipPart["notes"], ppq: number): RollNote[] =>
  notes.map((n) => ({ pitch: n.pitch, start: n.tick / ppq, length: n.dur / ppq, velocity: n.vel }));

export const clipBeats = (clip: Clip) => (clip.bars * clip.ticksPerBar) / clip.ppq;
export const beatsPerBar = (clip: Clip) => clip.ticksPerBar / clip.ppq;

const SUBLANE_NAMES: Record<string, string> = {
  kick: "Kick", snare: "Snare", clap_rim: "Clap / rim", hats: "Hats", toms: "Toms", cymbals: "Cymbals", aux_kit: "Aux kit",
};

/** Drum rows by v1 sublane, from the part's voice map (in the map's order). */
export function sublanes(part: ClipPart, ppq: number): Sublane[] | undefined {
  if (!part.voices || part.voices.length === 0) return undefined;
  const byPitch = new Map(part.voices.map((v) => [v.pitch, v.sublane]));
  const order = [...new Set(part.voices.map((v) => v.sublane))];
  return order.map((id) => ({
    id,
    name: SUBLANE_NAMES[id] ?? id,
    notes: rollNotes(part.notes.filter((n) => byPitch.get(n.pitch) === id), ppq),
    muted: false,
  }));
}

export const ROLE_LABELS: Record<Role, string> = {
  chords: "Chords", pad: "Pad", arp: "Arp", bass: "Bass", melody: "Melody", counter: "Counter-melody", drums: "Drums",
};

export function partColour(role: Role): PartColour {
  if (role === "chords" || role === "pad") return "chords";
  if (role === "bass") return "bass";
  if (role === "melody" || role === "counter") return "melody";
  if (role === "drums") return "drums";
  return "extra";
}

export const modeLabel = (mode: string) => mode.replace(/_/g, " ");

export const KIND_LABELS: Record<Session["nodes"][number]["kind"], string> = {
  sketch: "sketch", initial: "idea", regenerate: "re-roll", vary: "vary", edit: "edit", tweak: "tweak", touch: "touch", library: "library",
};

/** "just now", "3 min ago", "2 h ago" */
export function ago(ms: number, now = Date.now()) {
  const s = Math.max(0, Math.round((now - ms) / 1000));
  if (s < 45) return "just now";
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86_400) return `${Math.round(s / 3600)} h ago`;
  return new Date(ms).toLocaleDateString();
}
