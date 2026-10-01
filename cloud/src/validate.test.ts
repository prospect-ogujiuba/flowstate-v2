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
