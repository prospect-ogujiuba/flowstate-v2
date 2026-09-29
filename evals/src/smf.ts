// Minimal Standard MIDI File (type 1) writer: conductor track (tempo + time signature) plus one track per part.
import type { NotesFile } from "./common.ts";

export interface SmfTrack {
  name: string;
  /** 0-based MIDI channel (drums = 9). */
  channel: number;
  notes: { tick: number; dur: number; pitch: number; vel: number }[];
}

export interface SmfSong {
  ppq: number;
  tempo: number;
  meter: [number, number];
  /** Clip end in ticks; every track's end-of-track is placed here (or after its last event). */
  lengthTicks: number;
  tracks: SmfTrack[];
}

export function vlq(value: number): number[] {
  if (!Number.isInteger(value) || value < 0 || value > 0x0fffffff) throw new Error(`vlq out of range: ${value}`);
  const bytes = [value & 0x7f];
  for (let v = value >>> 7; v > 0; v >>>= 7) bytes.unshift((v & 0x7f) | 0x80);
  return bytes;
}

function u32(n: number): number[] { return [(n >>> 24) & 0xff, (n >>> 16) & 0xff, (n >>> 8) & 0xff, n & 0xff]; }
function u16(n: number): number[] { return [(n >>> 8) & 0xff, n & 0xff]; }
function ascii(s: string): number[] { return [...Buffer.from(s, "utf8")]; }

function chunk(type: string, body: number[]): number[] { return [...ascii(type), ...u32(body.length), ...body]; }

interface TimedEvent { tick: number; order: number; bytes: number[] }

function encodeTrack(events: TimedEvent[], endTick: number): number[] {
  events.sort((a, b) => a.tick - b.tick || a.order - b.order);
  const body: number[] = [];
  let last = 0;
  for (const e of events) { body.push(...vlq(e.tick - last), ...e.bytes); last = e.tick; }
  body.push(...vlq(Math.max(0, endTick - last)), 0xff, 0x2f, 0x00);
  return chunk("MTrk", body);
}

function metaText(type: number, text: string): number[] { const t = ascii(text); return [0xff, type, ...vlq(t.length), ...t]; }

export function writeSmf(song: SmfSong): Uint8Array {
  const [num, den] = song.meter;
  const denPow = Math.round(Math.log2(den));
  if (2 ** denPow !== den) throw new Error(`meter denominator must be a power of two: ${den}`);
  const usPerQuarter = Math.round(60_000_000 / song.tempo);
  const conductor: TimedEvent[] = [
    { tick: 0, order: 0, bytes: metaText(0x03, "Flowstate") },
    { tick: 0, order: 1, bytes: [0xff, 0x51, 0x03, (usPerQuarter >>> 16) & 0xff, (usPerQuarter >>> 8) & 0xff, usPerQuarter & 0xff] },
    // clocks per metronome click: one click per meter unit (24 MIDI clocks per quarter).
    { tick: 0, order: 2, bytes: [0xff, 0x58, 0x04, num, denPow, Math.max(1, Math.round((24 * 4) / den)), 8] },
  ];
  const out: number[] = [...chunk("MThd", [...u16(1), ...u16(song.tracks.length + 1), ...u16(song.ppq)])];
  out.push(...encodeTrack(conductor, song.lengthTicks));
  for (const track of song.tracks) {
    const ch = track.channel & 0x0f;
    const events: TimedEvent[] = [{ tick: 0, order: 0, bytes: metaText(0x03, track.name) }];
    let end = song.lengthTicks;
    for (const n of track.notes) {
      const pitch = Math.max(0, Math.min(127, Math.round(n.pitch)));
      const vel = Math.max(1, Math.min(127, Math.round(n.vel)));
      const on = Math.max(0, Math.round(n.tick));
      const off = on + Math.max(1, Math.round(n.dur));
      end = Math.max(end, off);
      // order: note-offs (1) before note-ons (2) at the same tick so repeated pitches retrigger cleanly.
      events.push({ tick: on, order: 2, bytes: [0x90 | ch, pitch, vel] });
      events.push({ tick: off, order: 1, bytes: [0x80 | ch, pitch, 0] });
    }
    out.push(...encodeTrack(events, end));
  }
  return Uint8Array.from(out);
}

/** Converts a notes.json document to an SMF (channels in notes.json are 1-based). */
export function notesFileToSmf(doc: NotesFile): Uint8Array {
  return writeSmf({
    ppq: doc.ppq,
    tempo: doc.tempo,
    meter: doc.meter,
    lengthTicks: doc.bars * doc.ticksPerBar,
    tracks: doc.parts.map((p) => ({ name: p.name || p.id, channel: Math.max(0, p.channel - 1), notes: p.notes })),
  });
}
