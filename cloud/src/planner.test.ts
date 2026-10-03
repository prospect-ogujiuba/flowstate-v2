import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { contentText, createModels, fauxAssistantMessage, fauxProvider, getCurrentSystemPrompt } from "@earendil-works/pi-ai";
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
  // The last user message of each request, i.e. what the planner asked for, and the system prompt it rendered.
  const requests: string[] = [];
  const systems: string[] = [];
  faux.setResponses(replies.map((text) => (ctx) => {
    const last = ctx.messages.at(-1);
    requests.push(last?.role === "user" ? contentText(last.content) : "");
    systems.push(getCurrentSystemPrompt(ctx.messages));
    return fauxAssistantMessage(text);
  }));
  return { backend: backendFor({ provider: "faux", model: "faux-model", credential: { kind: "managed" } }, models), requests, systems };
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

  it("takes the fixed context from the request, so a wrong key or length needs no repair", async () => {
    const { tempo, meterNumerator, meterDenominator, tonic, mode, bars, ...free } = score.context;
    const sparse = { ...score, context: free };
    const wrong = { ...score, context: { ...score.context, tonic: "C", bars: 99 } };
    for (const reply of [sparse, wrong]) {
      const { backend, requests } = setup(JSON.stringify(reply));
      const seen: string[] = [];
      const result = await planScore(request, backend, { onPart: (p) => seen.push(p.id) });
      assert.match(requests[0]!, /write only "swing" and "style"/);
      assert.match(requests[0]!, new RegExp(`grid 4: ${score.context.meterNumerator * 4}\\)`));
      assert.equal(result.attempts, 1);
      assert.deepEqual(result.score, score);
      assert.deepEqual(seen, score.parts.map((p) => p.id));
    }
  });

  it("rewrites the whole score when the head is wrong, and plays only the rewrite", async () => {
    const badForm = { ...score, form: [{ name: "A", startBar: 1, bars: score.context.bars + 4, energy: 0.5 }] };
    const { backend, requests } = setup(JSON.stringify(badForm), JSON.stringify(score));
    const seen: string[] = [];
    const result = await planScore(request, backend, { onPart: (p) => seen.push(p.id) });
    assert.match(requests[1]!, /complete corrected score/);
    // A broken head plays nothing; nothing has played, so the rewrite streams.
    assert.deepEqual(seen, score.parts.map((p) => p.id));
    assert.deepEqual(result.score, score);
  });

  it("names the grooves that match the style and meter, in the style pack section after the IR spec", async () => {
    const trap: PlanRequest = { ...request, controls: { ...request.controls, style: ["trap"], meterNumerator: 4, meterDenominator: 4 } };
    const { backend, systems } = setup(JSON.stringify(score));
    await planScore(trap, backend);
    assert.match(systems[0]!, /<style_pack>[\s\S]*drum grooves for these styles: trap \(see Grooves\)[\s\S]*<\/style_pack>$/);
    assert.ok(systems[0]!.indexOf("### Grooves") < systems[0]!.indexOf("<style_pack>"), "the fixed prompt stays a cacheable prefix");
    const waltz: PlanRequest = { ...request, controls: { ...request.controls, style: ["trap"], meterNumerator: 3, meterDenominator: 4 } };
    const second = setup(JSON.stringify(score));
    await planScore(waltz, second.backend).catch(() => {});
    assert.doesNotMatch(second.systems[0]!, /style_pack|drum grooves for/);
  });

  it("keeps locked parts verbatim, plays them first, and writes only the other lanes", async () => {
    const chords = score.parts.find((p) => p.role === "chords")!;
    const keep = { ...score, parts: [chords] };
    const others = score.parts.filter((p) => p !== chords);
    // The model also rewrites the chords; that part is dropped.
    const rewritten = { ...chords, id: "chords-2", name: "Different chords" };
    const { backend, requests } = setup(JSON.stringify({ parts: [rewritten, ...others] }));
    const order: string[] = [];
    const req = { ...request, keep, controls: { ...request.controls, lanes: ["bass", "melody", "drums"] } };
    const result = await planScore(req, backend, { onHead: () => order.push("head"), onPart: (p) => order.push(p.id) });
    assert.deepEqual(result.validationErrors, []);
    assert.deepEqual(result.score.parts[0], chords);
    assert.deepEqual(result.score.parts.map((p) => p.role).sort(), ["bass", "chords", "drums", "melody"]);
    assert.deepEqual(result.score.harmony, score.harmony);
    assert.deepEqual(order, ["head", chords.id, ...others.map((p) => p.id)]);
    assert.match(requests[0]!, /The kept score:/);
    assert.match(requests[0]!, /for these lanes only:\nbass, melody, drums/);
    assert.match(requests[0]!, /Reply with only the new parts/);
    assert.equal(result.attempts, 1);
  });

  it("repairs a new part around kept parts without touching them", async () => {
    const chords = score.parts.find((p) => p.role === "chords")!;
    const keep = { ...score, parts: [chords] };
    const others = score.parts.filter((p) => p !== chords);
    const broken = structuredClone(others[0]!);
    broken.blocks[0]!.startBar = 99;
    const { backend, requests } = setup(JSON.stringify({ parts: [broken, ...others.slice(1)] }), JSON.stringify({ parts: [others[0], { ...chords, name: "x" }] }));
    const req = { ...request, keep, controls: { ...request.controls, lanes: ["bass", "melody", "drums"] } };
    const result = await planScore(req, backend);
    assert.equal(result.attempts, 2);
    assert.match(requests[1]!, /Return only the corrected or missing parts/);
    assert.deepEqual(result.score.parts[0], chords);
    assert.deepEqual(result.validationErrors, []);
  });

  it("refuses kept parts that don't fit the request", async () => {
    const { backend } = setup("{}");
    const chords = score.parts.find((p) => p.role === "chords")!;
    await assert.rejects(
      planScore({ ...request, keep: { ...score, parts: [chords] } }, backend),
      (e: Error & { code?: string }) => e.code === "bad_request" && /lane 'chords' is kept/.test(e.message),
    );
  });

  describe("what the producer just played (P1-20)", () => {
    // A played melody, as core's analyzer writes a capture: literal notes, and no harmony read from it.
    const notes = ["Eb5", "G5", "Bb5", "G5", "F5", "Eb5", "C5", "Bb4"].map((pitch, i) => ({ bar: 1 + Math.floor(i / 4), beat: 1 + (i % 4), beats: 0.75, pitch, velocity: 80 + i }));
    const playedMelody: Part = { id: "melody", role: "melody", name: "Played", low: "Bb4", high: "Bb5", grid: 4, velocity: 84, blocks: [{ startBar: 1, endBar: 4, notes }] };
    const melodyRiff: Score = { ...score, harmony: "", motifs: [], parts: [playedMelody] };
    // Played chords: core read a harmony from them, so the riff keeps its head.
    const playedChords: Part = { ...playedMelody, id: "chords", role: "chords", name: "Played", low: "Bb2", high: "F5",
      blocks: [{ startBar: 1, endBar: 4, notes: [{ bar: 1, beat: 1, beats: 4, pitch: "Eb4", velocity: 90 }, { bar: 1, beat: 1, beats: 4, pitch: "G4", velocity: 90 }] }] };
    const chordsRiff: Score = { ...score, parts: [playedChords] };
    const lanes = (...l: string[]) => ({ ...request.controls, lanes: l });

    it("continue: the model writes a whole new idea from the riff, which isn't kept", async () => {
      const { backend, requests } = setup(JSON.stringify(score));
      const result = await planScore({ ...request, reference: { score: melodyRiff, intent: "continue" } }, backend);
      assert.deepEqual(result.validationErrors, []);
      assert.deepEqual(result.score, score);
      assert.match(requests[0]!, /The producer just played a melody/);
      assert.match(requests[0]!, /continues it/);
      assert.match(requests[0]!, /"pitch":"Eb5"/);
      assert.doesNotMatch(requests[0]!, /stays in the score exactly as played/);
    });

    it("add bass to a played melody: the melody stays as played under the model's head, and plays once the head lands", async () => {
      const bass = score.parts.find((p) => p.role === "bass")!;
      const ownMelody = score.parts.find((p) => p.role === "melody")!;
      // The model also writes a melody of its own; it is dropped.
      const { backend, requests } = setup(JSON.stringify({ ...score, parts: [ownMelody, bass] }));
      const order: string[] = [];
      const req = { ...request, controls: lanes("bass"), reference: { score: melodyRiff, intent: "add_bass" as const } };
      const result = await planScore(req, backend, { onHead: () => order.push("head"), onPart: (p) => order.push(p.id) });
      assert.deepEqual(result.validationErrors, []);
      assert.deepEqual(result.score.parts, [playedMelody, bass]);
      assert.deepEqual(result.score.harmony, score.harmony);
      assert.deepEqual(order, ["head", "melody", bass.id]);
      assert.match(requests[0]!, /It stays in the score exactly as played \(part "melody"\)/);
      assert.match(requests[0]!, /bass line/);
    });

    it("add drums to played chords: the riff keeps its head, and the model writes only the drums", async () => {
      const drums = score.parts.find((p) => p.role === "drums")!;
      const { backend, requests } = setup(JSON.stringify({ parts: [drums] }));
      const req = { ...request, controls: lanes("drums"), reference: { score: chordsRiff, intent: "add_drums" as const } };
      const result = await planScore(req, backend);
      assert.deepEqual(result.validationErrors, []);
      assert.deepEqual(result.score.parts, [playedChords, drums]);
      assert.match(requests[0]!, /The producer just played the part below/);
      assert.match(requests[0]!, /Reply with only the new parts/);
    });

    it("refuses a riff with locked parts too, or a lane the riff already plays", async () => {
      const { backend } = setup("{}");
      const reject = (req: PlanRequest, pattern: RegExp) =>
        assert.rejects(planScore(req, backend), (e: Error & { code?: string }) => e.code === "bad_request" && pattern.test(e.message));
      await reject({ ...request, keep: score, reference: { score: melodyRiff, intent: "continue" } }, /can't also keep locked parts/);
      await reject({ ...request, controls: lanes("melody"), reference: { score: melodyRiff, intent: "harmonize" } }, /already the 'melody' lane/);
      await reject({ ...request, reference: { score: { ...melodyRiff, parts: [] }, intent: "continue" } }, /nothing was played/);
    });
  });

  it("stops when the request is aborted", async () => {
    const { backend } = setup(JSON.stringify(score));
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(planScore(request, backend, { signal: controller.signal }));
  });
});
