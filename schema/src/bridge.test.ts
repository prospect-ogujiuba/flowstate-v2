// Round-trips the shared bridge fixtures through Zod. schema/cpp/tests does the same in C++,
// against the same files, and must report the same error paths.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { z } from "zod";
import { bridgeRoots, type BridgeRoot } from "./bridge.ts";
import { Score } from "./score.ts";

const dir = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "fixtures", "bridge");
type Fixture = { valid: unknown[]; invalid: { why: string; value: unknown; path: string }[] };

const fixtures = readdirSync(dir)
  .filter((f) => f.endsWith(".json"))
  .map((f) => ({ root: path.basename(f, ".json"), ...(JSON.parse(readFileSync(path.join(dir, f), "utf8")) as Fixture) }));

// ["session", "parts", 2, "density"] -> "session.parts[2].density" (the C++ ParseError path format).
function formatPath(p: PropertyKey[]): string {
  return p.map((k, i) => (typeof k === "number" ? `[${k}]` : i === 0 ? String(k) : `.${String(k)}`)).join("");
}

test("every fixture file names a bridge root, and every root has one", () => {
  assert.deepEqual(fixtures.map((f) => f.root).sort(), Object.keys(bridgeRoots).sort());
});

for (const { root, valid, invalid } of fixtures) {
  const schema = bridgeRoots[root as BridgeRoot] as z.ZodType;

  test(`${root}: valid fixtures round-trip unchanged`, () => {
    for (const value of valid) {
      const parsed = schema.parse(value);
      assert.deepEqual(JSON.parse(JSON.stringify(parsed)), value);
    }
  });

  test(`${root}: invalid fixtures fail at the expected path`, () => {
    for (const { why, value, path: expected } of invalid) {
      const result = schema.safeParse(value);
      assert.equal(result.success, false, `${root}: accepted '${why}'`);
      const paths = result.error!.issues.map((i) => formatPath(i.path));
      assert.ok(paths.includes(expected), `${root} '${why}': expected an issue at ${expected}, got ${paths.join(", ")}`);
    }
  });
}

test("every union member has at least one valid fixture", () => {
  for (const root of ["Command", "PluginEvent", "ServiceEvent"] as const) {
    const union = bridgeRoots[root] as unknown as z.ZodDiscriminatedUnion<z.ZodObject[]>;
    const tags = union.options.map((o) => (o.shape.type as z.ZodLiteral<string>).value);
    const seen = new Set(fixtures.find((f) => f.root === root)!.valid.map((v) => (v as { type: string }).type));
    assert.deepEqual(tags.filter((t) => !seen.has(t)), [], `${root} members without a fixture`);
  }
});

test("scores in fixtures are valid Score IR", () => {
  const saved = fixtures.find((f) => f.root === "SavedSession")!.valid as { nodes: { score: unknown }[] }[];
  for (const s of saved) for (const n of s.nodes) Score.parse(n.score);
});
