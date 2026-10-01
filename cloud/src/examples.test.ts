import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { createModels, fauxAssistantMessage, fauxProvider } from "@earendil-works/pi-ai";
import type { LibraryCatalog } from "@flowstate/schema";
import { backendFor } from "./backends.ts";
import { examplesText, loadCatalog, pickExamples } from "./examples.ts";
import { planScore, type PlanRequest } from "./planner.ts";

const catalog = loadCatalog();
const neoSoul = { style: ["neo-soul", "lofi"], lanes: ["chords", "bass", "melody", "drums"], tempo: 118, meterNumerator: 4, meterDenominator: 4 };

describe("style examples", () => {
  it("picks clips that share a style tag, in a requested lane and meter, one lane before a second", () => {
    const picked = pickExamples(catalog, neoSoul, 2);
    assert.equal(picked.length, 2);
    assert.ok(picked.every((c) => c.entry.genres.includes("neo-soul")));
    assert.deepEqual(pickExamples(catalog, neoSoul, 2), picked, "deterministic");

    const hipHop = pickExamples(catalog, { ...neoSoul, style: ["hip-hop", "rnb"], tempo: 90 }, 2);
    assert.deepEqual(hipHop.map((c) => c.entry.roles[0]).sort(), ["bass", "chords"]);
  });

  it("picks nothing for an unrelated style, another meter, absent lanes or count 0", () => {
    assert.deepEqual(pickExamples(catalog, { ...neoSoul, style: ["drum-and-bass"] }, 3), []);
    assert.deepEqual(pickExamples(catalog, { ...neoSoul, meterNumerator: 6, meterDenominator: 8 }, 3), []);
    assert.deepEqual(pickExamples(catalog, { ...neoSoul, lanes: ["drums"] }, 3), []);
    assert.deepEqual(pickExamples(catalog, neoSoul, 0), []);
  });

  it("leaves out clips whose license doesn't allow style examples", () => {
    const closed: LibraryCatalog = structuredClone(catalog);
    for (const c of closed.clips) c.entry.credit!.allowsStyleExamples = false;
    assert.deepEqual(pickExamples(closed, neoSoul, 3), []);
  });

  it("credits the producer and shows the clip in IR", () => {
    const text = examplesText(pickExamples(catalog, neoSoul, 1));
    assert.match(text, /MIDI by GodFlow \(flowknows\) for Flowstate\./);
    assert.match(text, /harmony: "/);
    assert.match(text, /part: \{"/);
    assert.equal(examplesText([]), "");
  });

  it("the planner sends them with the request, and only when asked", async () => {
    const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model" }] });
    const models = createModels();
    models.setProvider(faux.provider);
    const seen: string[] = [];
    faux.setResponses([0, 1].map(() => (ctx) => {
      const last = ctx.messages.at(-1);
      seen.push(typeof last?.content === "string" ? last.content : JSON.stringify(last?.content));
      return fauxAssistantMessage("not a score");
    }));
    const backend = backendFor({ provider: "faux", model: "faux-model", credential: { kind: "managed" } }, models);
    const req: PlanRequest = {
      prompt: "warm neo-soul loop",
      controls: { tonic: "C", mode: "major", bars: 4, tempo: 118, meterNumerator: 4, meterDenominator: 4, style: neoSoul.style, lanes: ["chords"] },
    };
    await planScore({ ...req, examples: pickExamples(catalog, neoSoul, 1) }, backend).catch(() => undefined);
    await planScore(req, backend).catch(() => undefined);
    assert.match(seen[0]!, /Style examples from Flowstate's built-in library/);
    assert.ok(seen.slice(1).every((m) => !m.includes("Style examples")));
  });
});
