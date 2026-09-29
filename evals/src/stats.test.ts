import assert from "node:assert/strict";
import { test } from "node:test";
import { mulberry32, signTest } from "./stats.ts";

const close = (a: number, b: number) => assert.ok(Math.abs(a - b) < 1e-9, `${a} != ${b}`);

test("sign test matches exact binomial values", () => {
  close(signTest(10, 10).twoSided, 2 / 1024);
  close(signTest(10, 10).oneSidedGreater, 1 / 1024);
  close(signTest(0, 10).twoSided, 2 / 1024);
  close(signTest(5, 10).twoSided, 1);
  // P(X >= 8 | n=10) = (45 + 10 + 1) / 1024
  close(signTest(8, 10).oneSidedGreater, 56 / 1024);
  close(signTest(8, 10).twoSided, 112 / 1024);
  // 14 of 20: two-sided p = 0.11532...
  assert.ok(Math.abs(signTest(14, 20).twoSided - 0.115318) < 1e-5);
  // 15 of 20 is significant
  assert.ok(signTest(15, 20).twoSided < 0.05);
  close(signTest(0, 0).twoSided, 1);
});

test("mulberry32 is reproducible", () => {
  const a = mulberry32(42), b = mulberry32(42);
  for (let i = 0; i < 5; i++) assert.equal(a(), b());
});
