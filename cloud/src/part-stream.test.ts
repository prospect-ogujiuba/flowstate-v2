import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import type { Score } from "@flowstate/schema";
import { PartStream } from "./part-stream.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const score: Score = JSON.parse(readFileSync(
  path.join(here, "..", "..", "evals", "results", "p1-1", "openrouter-gpt-5.5", "lofi-rainy-study.score.json"), "utf8"));
const ids = score.parts.map((p) => p.id);

// Feeds the text in chunks of `size` characters and returns the ids in the order they were reported.
function feed(text: string, size: number) {
  const seen: string[] = [];
  const stream = new PartStream((p) => seen.push(p.id));
  for (let i = 0; i < text.length; i += size) stream.push(text.slice(i, i + size));
  return { seen, stream };
}

describe("PartStream", () => {
  it("reports each part as its closing brace lands, whatever the layout", () => {
    for (const text of [JSON.stringify(score), JSON.stringify(score, null, 2), "Here it is:\n```json\n" + JSON.stringify(score, null, 2) + "\n```\n"]) {
      for (const size of [1, 7, 4096]) {
        const { seen, stream } = feed(text, size);
        assert.deepEqual(seen, ids);
        assert.deepEqual(stream.unplayable, []);
      }
    }
  });

  it("reports a part before the rest of the reply arrives", () => {
    const text = JSON.stringify(score);
    const firstEnd = text.indexOf(JSON.stringify(score.parts[0])) + JSON.stringify(score.parts[0]).length;
    const { seen } = feed(text.slice(0, firstEnd), 13);
    assert.deepEqual(seen, [ids[0]]);
  });

  it("is not fooled by braces, brackets or a \"parts\" key inside strings", () => {
    const tricky = structuredClone(score);
    tricky.title = 'say "parts": [{ ]} \\ done';
    tricky.parts[0]!.name = "}{][";
    const { seen } = feed(JSON.stringify(tricky), 5);
    assert.deepEqual(seen, ids);
  });

  it("explains parts it cannot play", () => {
    const bad = structuredClone(score);
    bad.parts[1]!.blocks[0]!.endBar = 99;
    const { seen, stream } = feed(JSON.stringify(bad), 64);
    assert.deepEqual(seen, ids.filter((_, i) => i !== 1));
    assert.equal(stream.unplayable[0]!.part, ids[1]);
  });

  it("plays nothing when the head is broken", () => {
    const { parts, ...head } = score;
    const text = JSON.stringify({ ...head, context: { ...head.context, bars: "four" }, parts });
    const { seen, stream } = feed(text, 64);
    assert.deepEqual(seen, []);
    assert.equal(stream.unplayable.length, parts.length);
  });
});
