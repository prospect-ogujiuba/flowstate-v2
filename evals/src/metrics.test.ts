import assert from "node:assert/strict";
import { describe, it } from "node:test";
import type { NotesFile } from "./common.ts";
import { fileMetrics } from "./metrics.ts";

// Two C-major chords then an F-major chord, each humanized a few ticks apart.
const chord = (tick: number, pitches: number[]) => pitches.map((p, i) => ({ tick: tick + i * 3, dur: 900, pitch: p, vel: 80 }));
const doc: NotesFile = {
  ppq: 960, bars: 1, ticksPerBar: 3840, tempo: 100, meter: [4, 4],
  parts: [{ id: "keys", role: "chords", name: "keys", channel: 1, notes: [...chord(0, [60, 64, 67]), ...chord(960, [60, 64, 67]), ...chord(1920, [60, 65, 69])] }],
};

describe("voice movement", () => {
  it("treats a humanized chord as one chord and skips re-strikes", () => {
    const m = fileMetrics("t", doc, undefined).parts[0]!;
    // Only the C to F change counts: C stays, E to F is 1, G to A is 2.
    assert.equal(m.voiceMovement, 3);
  });
});
