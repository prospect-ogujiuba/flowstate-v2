import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { writeSmf } from "./smf.ts";
import { readSmf } from "./smf-read.ts";

describe("readSmf", () => {
  it("reads back what writeSmf wrote", () => {
    const tracks = [
      { name: "Chords", channel: 0, notes: [{ tick: 0, dur: 960, pitch: 60, vel: 80 }, { tick: 0, dur: 960, pitch: 64, vel: 80 }] },
      { name: "Drums", channel: 9, notes: [{ tick: 0, dur: 120, pitch: 36, vel: 110 }, { tick: 480, dur: 120, pitch: 36, vel: 100 }] },
    ];
    const song = readSmf(writeSmf({ ppq: 960, tempo: 92, meter: [6, 8], lengthTicks: 3840, tracks }));
    assert.equal(song.ppq, 960);
    assert.ok(Math.abs(song.tempo - 92) < 0.01);
    assert.deepEqual(song.meter, [6, 8]);
    assert.equal(song.lengthTicks, 3840);
    assert.deepEqual(song.tracks, tracks);
  });
});
