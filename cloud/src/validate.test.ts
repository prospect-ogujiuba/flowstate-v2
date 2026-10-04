import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { Score } from "@flowstate/schema";
import { validateScore } from "./validate.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const score: Score = JSON.parse(readFileSync(
  path.join(here, "..", "..", "evals", "results", "p1-1", "openrouter-gpt-5.5", "lofi-rainy-study.score.json"), "utf8"));

const withMotif = (notes: string) => Score.parse({ ...score, motifs: [{ id: score.motifs[0]!.id, notes }, ...score.motifs.slice(1)] });

describe("validateScore motifs", () => {
  it("accepts compact motif strings", () => {
    assert.deepEqual(validateScore(withMotif("5:.75! 4:.25 b3:1 | r:.5 1+:1/3 #2-:1/3 r:1/3 8:1.5")), []);
  });

  it("still accepts the note-object form", () => {
    assert.deepEqual(validateScore(score), []);
  });

  it("names each bad token", () => {
    const errors = validateScore(withMotif("5:1 x:1 3 5:0 5++-:1 1:1/0"));
    assert.deepEqual(errors.map((e) => e.match(/'([^']*)'/)![1]), ["x:1", "3", "5:0", "5++-:1", "1:1/0"]);
  });
});

describe("validateScore blocks", () => {
  it("accepts blocks that leave out the fields that do not apply", () => {
    const strip = (v: unknown): unknown =>
      Array.isArray(v) ? v.map(strip)
        : v && typeof v === "object" ? Object.fromEntries(Object.entries(v).filter(([, x]) => x !== null).map(([k, x]) => [k, strip(x)]))
        : v;
    const sparse = { ...score, parts: strip(score.parts) };
    assert.notEqual(JSON.stringify(sparse).length, JSON.stringify(score).length);
    assert.deepEqual(validateScore(Score.parse(sparse)), []);
  });
});

describe("validateScore harmony", () => {
  const withHarmony = (harmony: string) => Score.parse({ ...score, harmony });
  const clip = score.context.bars * score.context.meterNumerator;

  it("accepts a compact harmony string that fits the clip", () => {
    assert.deepEqual(validateScore(withHarmony(`Ebmaj9:4 | Cm9:2 r:2 | Fm9/Ab:1/3 Fm9:${clip - 8 - 1 / 3}`)), []);
  });

  it("names bad tokens, and a harmony longer than the clip", () => {
    const errors = validateScore(withHarmony(`Cm:4 x:1 F7 Bb:0 Eb:${clip}`));
    assert.deepEqual(errors.filter((e) => e.includes("bad token")).map((e) => e.match(/'([^']*)'/)![1]), ["x:1", "F7", "Bb:0"]);
    assert.ok(errors.some((e) => e.includes(`lasts ${clip + 4} beats`)));
  });
});

describe("validateScore step strings", () => {
  const withRhythm = (rhythm: string, grid = 4) => {
    const s = structuredClone(score);
    const part = s.parts.find((p) => p.role === "chords")!;
    part.grid = grid;
    part.blocks = [{ startBar: 1, endBar: s.context.bars, rhythm }];
    return validateScore(Score.parse(s));
  };
  const perBar = score.context.meterNumerator * 4;

  it("accepts bars at another resolution, which core reads as written", () => {
    assert.deepEqual(withRhythm(`${"x...".repeat(perBar / 8)} | ${"x.".repeat(perBar)}`), []);
  });

  it("rejects bars whose length is not a resolution", () => {
    const errors = withRhythm(`${"x".repeat(perBar - 1)} | ${"x".repeat(perBar + 1)}`);
    assert.equal(errors.filter((e) => e.includes("steps, expected")).length, 2);
  });

  it("cuts strings without bar lines into bars, as core does", () => {
    assert.deepEqual(withRhythm("x...".repeat(perBar / 2)), []);
    assert.equal(withRhythm("x".repeat(perBar + 3)).length, 1);
  });

  it("treats - in a drum lane as a rest", () => {
    const s = structuredClone(score);
    const drums = s.parts.find((p) => p.role === "drums")!;
    drums.blocks = [{ startBar: 1, endBar: s.context.bars, drums: [{ voice: "kick", steps: `x---${".".repeat(drums.grid * s.context.meterNumerator - 4)}` }] }];
    assert.deepEqual(validateScore(Score.parse(s)), []);
  });
});

describe("validateScore grooves", () => {
  const withDrums = (block: Record<string, unknown>) => {
    const s = structuredClone(score);
    const drums = s.parts.find((p) => p.role === "drums")!;
    drums.blocks = [{ startBar: 1, endBar: s.context.bars, ...block }];
    return validateScore(Score.parse(s));
  };

  it("accepts a drums block with only a groove, or only literal notes (a played riff)", () => {
    assert.deepEqual(withDrums({ groove: "boom_bap" }), []);
    assert.deepEqual(withDrums({ notes: [{ bar: 1, beat: 1, beats: 0.25, pitch: "C2", velocity: 110 }] }), []);
  });

  it("rejects a groove for another meter, and a block with neither lanes nor groove", () => {
    assert.match(withDrums({ groove: "jazz_waltz" }).join(), /is for 3\/4/);
    assert.match(withDrums({}).join(), /no drum lanes, groove or literal notes/);
  });
});
