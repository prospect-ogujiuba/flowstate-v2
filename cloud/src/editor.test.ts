import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { contentText, createModels, fauxAssistantMessage, fauxProvider, getCurrentSystemPrompt } from "@earendil-works/pi-ai";
import type { Part, Score } from "@flowstate/schema";
import { backendFor } from "./backends.ts";
import { applyPatch, editScore, rulesFor, type EditRequest } from "./editor.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
// A valid lofi score from the P1-1 run: parts chords, bass, melody (plays motif sleepy_keys) and drums.
const score: Score = JSON.parse(readFileSync(path.join(here, "..", "..", "evals", "results", "p1-1", "openrouter-gpt-5.5", "lofi-rainy-study.score.json"), "utf8"));
const part = (id: string) => structuredClone(score.parts.find((p) => p.id === id)!);
const edit = (over: Partial<EditRequest> = {}): EditRequest => ({ kind: "edit", prompt: "busier bass", score, partIds: null, role: null, ...over });

// The bass with a busier last bar, still 16 steps per bar.
const busierBass = (): Part => {
  const bass = part("bass");
  const block = bass.blocks[0] as { rhythm: string };
  block.rhythm = block.rhythm.replace(/\|[^|]*$/, "|R-5-R-7-R-5-a-5-");
  return bass;
};

describe("applyPatch", () => {
  it("replaces the changed parts by id and keeps the rest", () => {
    const req = edit();
    const out = applyPatch(JSON.stringify({ message: "Busier bass in bar 4.", parts: [busierBass()] }), req, rulesFor(req));
    assert.deepEqual(out.errors, []);
    assert.deepEqual(out.changed, ["bass"]);
    assert.equal(out.message, "Busier bass in bar 4.");
    assert.deepEqual(out.score!.parts.map((p) => p.id), score.parts.map((p) => p.id));
    assert.deepEqual(out.score!.parts.find((p) => p.id === "bass"), busierBass());
    assert.deepEqual(out.score!.context, score.context);
  });

  it("never changes a locked part, harmony a locked part plays from, or a motif a locked part plays", () => {
    const req = edit({ partIds: ["bass", "drums"] });
    const rules = rulesFor(req);
    const melody = part("melody");
    melody.velocity = 30;
    const out = applyPatch(JSON.stringify({
      harmony: "Ebmaj7:16",
      motifs: [{ id: "sleepy_keys", notes: "1:4" }],
      parts: [busierBass(), melody],
    }), req, rules);
    assert.ok(out.errors.some((e) => /harmony is fixed/.test(e)));
    assert.ok(out.errors.some((e) => /motif sleepy_keys is fixed/.test(e)));
    assert.ok(out.errors.some((e) => /part melody is locked/.test(e)));
    assert.deepEqual(out.score?.parts.find((p) => p.id === "melody"), part("melody"), "the locked part is never replaced");
  });

  it("lets harmony change when only drums are locked, and adds new motifs alongside locked ones", () => {
    const req = edit({ partIds: ["chords", "bass", "melody"] });
    const out = applyPatch(JSON.stringify({ harmony: score.harmony, motifs: [{ id: "answer", notes: "3:1 2:1 1:2" }], parts: [busierBass()] }), req, rulesFor(req));
    assert.deepEqual(out.errors, []);
    assert.deepEqual(out.score!.motifs.map((m) => m.id), ["sleepy_keys", "answer"]);
  });

  it("vary: exactly the one part, same role; addPart: exactly one new part of the role", () => {
    const vary = edit({ kind: "vary", partIds: ["bass"], prompt: "" });
    assert.deepEqual(applyPatch(JSON.stringify({ parts: [busierBass()] }), vary, rulesFor(vary)).errors, []);
    const newPart = { ...busierBass(), id: "bass2" };
    assert.ok(applyPatch(JSON.stringify({ parts: [newPart] }), vary, rulesFor(vary)).errors.some((e) => /adds no parts/.test(e)));

    const add = edit({ kind: "addPart", role: "bass", partIds: [], prompt: "" });
    const added = applyPatch(JSON.stringify({ parts: [newPart] }), add, rulesFor(add));
    assert.deepEqual(added.errors, []);
    assert.deepEqual(added.changed, ["bass2"]);
    assert.equal(added.score!.parts.length, score.parts.length + 1);
    assert.ok(applyPatch(JSON.stringify({ parts: [busierBass()] }), add, rulesFor(add)).errors.some((e) => /locked or not part of this request/.test(e)));
  });

  it("a question gets a text answer; a patch that changes nothing is a problem", () => {
    const req = edit({ prompt: "what key is this in?" });
    const answer = applyPatch(JSON.stringify({ message: "Eb major." }), req, rulesFor(req));
    assert.equal(answer.score, null);
    assert.deepEqual(answer.errors, []);
    assert.match(applyPatch("{}", req, rulesFor(req)).errors.join(), /changes nothing/);
  });

  it("removes only editable parts, and only in an edit", () => {
    const req = edit({ partIds: ["drums"] });
    const out = applyPatch(JSON.stringify({ remove: ["drums", "melody"] }), req, rulesFor(req));
    assert.deepEqual(out.removed, ["drums"]);
    assert.ok(out.errors.some((e) => /melody can't be removed/.test(e)));
  });

  it("refuses requests it can't serve", () => {
    assert.throws(() => rulesFor(edit({ partIds: ["nope"] })), /not in the score/);
    assert.throws(() => rulesFor(edit({ kind: "vary", partIds: ["bass", "drums"] })), /exactly one/);
    assert.throws(() => rulesFor(edit({ kind: "addPart", role: null })), /needs a role/);
    assert.throws(() => rulesFor(edit({ partIds: [] })), /every part is locked/);
  });
});

function scripted(...replies: string[]) {
  const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model" }], tokenSize: { min: 8, max: 64 } });
  const models = createModels();
  models.setProvider(faux.provider);
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

const host = { catalog: null, analyzeClip: null };

describe("editScore", () => {
  it("streams the edited head and only the changed parts, and returns the whole edited score", async () => {
    const { backend, requests, systems } = scripted(JSON.stringify({ message: "Busier bass.", parts: [busierBass()] }, null, 2));
    const heads: Score[] = [];
    const parts: string[] = [];
    const result = await editScore(edit({ partIds: ["bass", "drums"] }), backend, { host, onHead: (s) => heads.push(s), onPart: (p) => parts.push(p.id) });
    assert.deepEqual(parts, ["bass"]);
    assert.equal(heads.length, 1);
    assert.deepEqual(heads[0]!.harmony, score.harmony);
    assert.equal(result.score!.parts.length, 4);
    assert.equal(result.message, "Busier bass.");
    assert.ok(result.firstPartMs !== null);
    assert.match(requests[0]!, /parts you may change: bass \(bass\), drums \(drums\)/);
    assert.match(requests[0]!, /locked, keep as they are: chords \(chords\), melody \(melody\)/);
    assert.match(requests[0]!, /harmony: fixed/);
    assert.match(systems[0]!, /<edit_mode>[\s\S]*<\/edit_mode>/);
  });

  it("asks again with the problems, and never streams a part it may not change", async () => {
    const melody = part("melody");
    melody.velocity = 30;
    const { backend, requests } = scripted(
      JSON.stringify({ parts: [melody] }),
      JSON.stringify({ parts: [busierBass()] }),
    );
    const parts: string[] = [];
    const result = await editScore(edit({ partIds: ["bass"] }), backend, { host, onPart: (p) => parts.push(p.id) });
    assert.match(requests[1]!, /part melody is locked/);
    assert.deepEqual(parts, ["bass"]);
    assert.equal(result.attempts, 2);
    assert.deepEqual(result.validationErrors, []);
    assert.deepEqual(result.score!.parts.find((p) => p.id === "melody"), part("melody"));
  });
});
