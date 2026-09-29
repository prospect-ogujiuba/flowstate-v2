import assert from "node:assert/strict";
import { test } from "node:test";
import { vlq, writeSmf } from "./smf.ts";

test("vlq encodes standard MIDI examples", () => {
  assert.deepEqual(vlq(0), [0x00]);
  assert.deepEqual(vlq(0x7f), [0x7f]);
  assert.deepEqual(vlq(0x80), [0x81, 0x00]);
  assert.deepEqual(vlq(0x3fff), [0xff, 0x7f]);
  assert.deepEqual(vlq(0x200000), [0x81, 0x80, 0x80, 0x00]);
});

test("writeSmf emits a type-1 header with conductor + one track per part", () => {
  const bytes = writeSmf({
    ppq: 960, tempo: 120, meter: [6, 8], lengthTicks: 2880,
    tracks: [
      { name: "keys", channel: 0, notes: [{ tick: 0, dur: 960, pitch: 60, vel: 90 }] },
      { name: "drums", channel: 9, notes: [{ tick: 0, dur: 120, pitch: 36, vel: 100 }] },
    ],
  });
  const buf = Buffer.from(bytes);
  assert.equal(buf.toString("ascii", 0, 4), "MThd");
  assert.equal(buf.readUInt32BE(4), 6);
  assert.equal(buf.readUInt16BE(8), 1); // format 1
  assert.equal(buf.readUInt16BE(10), 3); // conductor + 2 parts
  assert.equal(buf.readUInt16BE(12), 960);
  // walk the track chunks
  let off = 14, tracks = 0;
  while (off < buf.length) {
    assert.equal(buf.toString("ascii", off, off + 4), "MTrk");
    const len = buf.readUInt32BE(off + 4);
    const body = buf.subarray(off + 8, off + 8 + len);
    assert.deepEqual([...body.subarray(body.length - 3)], [0xff, 0x2f, 0x00]); // end of track
    if (tracks === 0) {
      assert.ok(body.includes(Buffer.from([0xff, 0x51, 0x03, 0x07, 0xa1, 0x20]))); // 500000 us/quarter
      assert.ok(body.includes(Buffer.from([0xff, 0x58, 0x04, 6, 3]))); // 6/8
    }
    if (tracks === 2) assert.ok(body.includes(Buffer.from([0x99, 36, 100]))); // note-on channel 10
    off += 8 + len; tracks++;
  }
  assert.equal(tracks, 3);
});
