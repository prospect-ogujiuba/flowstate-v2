import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { GROOVES } from "./score.ts";

const spec = readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "docs", "ir-spec.md"), "utf8");

describe("grooves", () => {
  it("are all in the spec's groove table, with their meter", () => {
    for (const [name, g] of Object.entries(GROOVES))
      assert.ok(spec.includes(`| \`${name}\` | ${g.meter[0]}/${g.meter[1]} |`), `${name} missing from docs/ir-spec.md`);
  });
});
