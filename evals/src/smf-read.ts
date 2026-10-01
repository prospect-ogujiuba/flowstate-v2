// Minimal Standard MIDI File reader for the files evals writes (smf.ts): notes per track, track names,
// the first tempo and time signature. Enough to render a pack; not a general-purpose parser.

export interface ReadNote { tick: number; dur: number; pitch: number; vel: number }
export interface ReadTrack { name: string; channel: number; notes: ReadNote[] }
export interface ReadSong { ppq: number; tempo: number; meter: [number, number]; tracks: ReadTrack[]; lengthTicks: number }

export function readSmf(bytes: Uint8Array): ReadSong {
  let pos = 0;
  const u8 = () => bytes[pos++]!;
  const u16 = () => (u8() << 8) | u8();
  const u32 = () => ((u8() << 24) | (u8() << 16) | (u8() << 8) | u8()) >>> 0;
  const tag = () => String.fromCharCode(u8(), u8(), u8(), u8());
  const vlq = () => {
    let v = 0;
    for (;;) {
      const b = u8();
      v = (v << 7) | (b & 0x7f);
      if (!(b & 0x80)) return v;
    }
  };

  if (tag() !== "MThd") throw new Error("not a MIDI file");
  const headerLen = u32();
  const headerEnd = pos + headerLen;
  u16(); // format
  const trackCount = u16();
  const division = u16();
  if (division & 0x8000) throw new Error("SMPTE time division is not supported");
  pos = headerEnd;

  let tempo = 120;
  let meter: [number, number] = [4, 4];
  let tempoSeen = false;
  let lengthTicks = 0;
  const tracks: ReadTrack[] = [];

  for (let t = 0; t < trackCount; t++) {
    if (tag() !== "MTrk") throw new Error(`track ${t}: missing MTrk`);
    const length = u32();
    const end = pos + length;
    let tick = 0;
    let status = 0;
    let name = "";
    let channel = -1;
    const open = new Map<number, { tick: number; vel: number }>();
    const notes: ReadNote[] = [];
    while (pos < end) {
      tick += vlq();
      let b = u8();
      if (b < 0x80) { pos--; b = status; } // running status
      if (b === 0xff) {
        const type = u8();
        const len = vlq();
        const data = bytes.subarray(pos, pos + len);
        pos += len;
        if (type === 0x03) name = new TextDecoder().decode(data);
        else if (type === 0x51 && !tempoSeen) { tempo = 60_000_000 / ((data[0]! << 16) | (data[1]! << 8) | data[2]!); tempoSeen = true; }
        else if (type === 0x58) meter = [data[0]!, 2 ** data[1]!];
        continue;
      }
      if (b === 0xf0 || b === 0xf7) { pos += vlq(); continue; }
      status = b;
      const kind = b & 0xf0;
      const ch = b & 0x0f;
      const d1 = u8();
      const d2 = kind === 0xc0 || kind === 0xd0 ? 0 : u8();
      if (kind === 0x90 && d2 > 0) {
        channel = ch;
        open.set(d1, { tick, vel: d2 });
      } else if (kind === 0x80 || (kind === 0x90 && d2 === 0)) {
        const on = open.get(d1);
        if (on) {
          notes.push({ tick: on.tick, dur: tick - on.tick, pitch: d1, vel: on.vel });
          open.delete(d1);
        }
      }
    }
    pos = end;
    lengthTicks = Math.max(lengthTicks, tick);
    if (notes.length || channel >= 0) tracks.push({ name, channel, notes: notes.sort((a, b) => a.tick - b.tick || a.pitch - b.pitch) });
  }
  return { ppq: division, tempo, meter, tracks, lengthTicks };
}
