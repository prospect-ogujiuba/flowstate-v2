import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { createModels, fauxAssistantMessage, fauxProvider } from "@earendil-works/pi-ai";
import type { Part, Score } from "@flowstate/schema";
import { backendFor } from "./backends.ts";
import { planScore, type PlanRequest } from "./planner.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..", "..");
const ID = "lofi-rainy-study";
const request: PlanRequest = JSON.parse(readFileSync(path.join(repo, "evals", "prompts", "phase0.json"), "utf8"))
  .find((p: { id: string }) => p.id === ID);
// A valid score from the P1-1 eval run.
const score: Score = JSON.parse(readFileSync(path.join(repo, "evals", "results", "p1-1", "openrouter-gpt-5.5", `${ID}.score.json`), "utf8"));

function setup(...replies: string[]) {
  // Small tokens so the reply streams in many deltas, as from a real provider.
  const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model" }], tokenSize: { min: 8, max: 64 } });
  const models = createModels();
  models.setProvider(faux.provider);
  // The last user message of each request, i.e. what the planner asked for.
  const requests: string[] = [];
  faux.setResponses(replies.map((text) => (ctx) => {
    const last = ctx.messages.at(-1);
    requests.push(typeof last?.content === "string" ? last.content : JSON.stringify(last?.content));
    return fauxAssistantMessage(text);
  }));
  return { backend: backendFor({ provider: "faux", model: "faux-model", credential: { kind: "managed" } }, models), requests };
}

describe("planScore", () => {
  it("plans a score and streams its parts out before the reply is done", async () => {
    const { backend } = setup("```json\n" + JSON.stringify(score, null, 2) + "\n```");
    const seen: Part[] = [];
    const result = await planScore(request, backend, { onPart: (p) => seen.push(p) });
    assert.deepEqual(result.validationErrors, []);
    assert.deepEqual(result.score, score);
    assert.deepEqual(seen.map((p) => p.id), score.parts.map((p) => p.id));
    assert.ok(result.firstTextMs !== null && result.firstPartMs !== null && result.firstPartMs <= result.latencyMs);
    assert.deepEqual(result.unplayable, []);
  });

  it("repairs only the broken part, and streams the fix", async () => {
    const bad = structuredClone(score);
    bad.parts[0]!.blocks[0]!.startBar = 99;
    const { backend, requests } = setup(JSON.stringify(bad), JSON.stringify({ parts: [score.parts[0]] }));
    const seen: string[] = [];
    const result = await planScore(request, backend, { onPart: (p) => seen.push(p.id) });
    assert.deepEqual(seen, [...score.parts.slice(1).map((p) => p.id), score.parts[0]!.id]);
    assert.deepEqual(result.unplayable.map((u) => u.part), [score.parts[0]!.id]);
    assert.match(result.unplayable[0]!.errors.join(), /bars 99/);
    assert.match(requests[1]!, /Return only the corrected or missing parts/);
    assert.equal(result.attempts, 2);
    assert.deepEqual(result.validationErrors, []);
    assert.deepEqual(result.score, score);
  });

  it("asks for a missing lane as a part", async () => {
    const short = { ...score, parts: score.parts.slice(0, -1) };
    const missing = score.parts.at(-1)!;
    const { backend, requests } = setup(JSON.stringify(short), JSON.stringify({ parts: [missing] }));
    const result = await planScore(request, backend);
    assert.match(requests[1]!, new RegExp(`missing requested lane '${missing.role}'`));
    assert.deepEqual(result.score, score);
  });

  it("rewrites the whole score when the head is wrong, and plays only the rewrite", async () => {
    const wrongKey = { ...score, context: { ...score.context, tonic: "C" } };
    const { backend, requests } = setup(JSON.stringify(wrongKey), JSON.stringify(score));
    const seen: string[] = [];
    const result = await planScore(request, backend, { onPart: (p) => seen.push(p.id) });
    assert.match(requests[1]!, /complete corrected score/);
    // A head in the wrong key plays nothing; nothing has played, so the rewrite streams.
    assert.deepEqual(seen, score.parts.map((p) => p.id));
    assert.deepEqual(result.score, score);
  });

  it("stops when the request is aborted", async () => {
    const { backend } = setup(JSON.stringify(score));
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(planScore(request, backend, { signal: controller.signal }));
  });
});
