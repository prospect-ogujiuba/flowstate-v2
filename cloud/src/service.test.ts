import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import type { AddressInfo } from "node:net";
import path from "node:path";
import { after, describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { createModels, fauxAssistantMessage, fauxProvider } from "@earendil-works/pi-ai";
import { BRIDGE_ID, Health, PlanRequest, ServiceEvent, type Score } from "@flowstate/schema";
import { backendFor, type Backend, type ModelSelection } from "./backends.ts";
import { FlowstateError, providerFailure } from "./errors.ts";
import { createService, KEY_HEADER, type ServiceConfig } from "./service.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..", "..");
const ID = "lofi-rainy-study";
// A valid score from the P1-1 eval run, and a plan request for the same session.
const score: Score = JSON.parse(readFileSync(path.join(repo, "evals", "results", "p1-1", "openrouter-gpt-5.5", `${ID}.score.json`), "utf8"));
const request: PlanRequest = PlanRequest.parse({
  protocol: BRIDGE_ID,
  prompt: "rainy-day lofi hip hop loop with dusty jazzy seventh chords",
  context: { tempo: 78, meterNumerator: 4, meterDenominator: 4, tonic: "Eb", mode: "major", bars: 4, swing: 0, style: ["lofi", "hip-hop"] },
  roles: null, keep: null, reference: null, provider: null,
});
const scoreText = JSON.stringify(score, null, 2);
// The reply up to and including the opening of the parts array: the head, as the model streams it.
const headText = scoreText.slice(0, scoreText.indexOf("\"parts\": [") + "\"parts\": [".length);

const MANAGED: ModelSelection = { provider: "faux", model: "faux-model", credential: { kind: "managed" } };

/** A faux pi-ai backend answering with these replies, recording the selection it was made for. */
function fauxBackends(...replies: string[]) {
  const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model" }, { id: "other" }], tokenSize: { min: 8, max: 64 } });
  const models = createModels();
  models.setProvider(faux.provider);
  faux.setResponses(replies.map((text) => () => fauxAssistantMessage(text)));
  const selections: ModelSelection[] = [];
  return { selections, backendFor: (sel: ModelSelection) => (selections.push(sel), backendFor(sel, models)) };
}

/** A backend whose reply is written by the test. */
function stubBackend(complete: Backend["complete"]): Backend {
  return { name: "stub", provider: "faux", model: "faux-model", complete };
}

const servers: { close(): void }[] = [];
after(() => servers.forEach((s) => s.close()));

async function start(over: Partial<ServiceConfig>) {
  const logs: Record<string, unknown>[] = [];
  const config: ServiceConfig = {
    version: "test", managed: MANAGED, managedAlso: [], features: { byok: true }, keepaliveMs: 60_000,
    log: (l) => logs.push(l), backendFor: fauxBackends(scoreText).backendFor, ...over,
  };
  const server = createService(config);
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  servers.push({ close: () => (server.closeAllConnections(), server.close()) });
  return { url: `http://127.0.0.1:${(server.address() as AddressInfo).port}`, logs };
}

const post = (url: string, body: unknown, headers: Record<string, string> = {}, signal?: AbortSignal) =>
  fetch(url, { method: "POST", body: typeof body === "string" ? body : JSON.stringify(body), headers, ...(signal ? { signal } : {}) });

/** Every `data:` payload of an SSE response, each checked against the contract. */
async function events(res: Response): Promise<ServiceEvent[]> {
  assert.match(res.headers.get("content-type") ?? "", /^text\/event-stream/);
  const text = await res.text();
  return text.split("\n\n").flatMap((block) => block.split("\n").filter((l) => l.startsWith("data: ")))
    .map((l) => ServiceEvent.parse(JSON.parse(l.slice("data: ".length))));
}

const errorOf = (evs: ServiceEvent[]) => {
  assert.equal(evs.length, 1);
  assert.equal(evs[0]!.type, "error");
  return (evs[0] as Extract<ServiceEvent, { type: "error" }>).error;
};

async function untilLogged(logs: Record<string, unknown>[]) {
  for (let i = 0; i < 100 && logs.length === 0; i++) await new Promise((r) => setTimeout(r, 20));
  return logs[0]!;
}

describe("agent service", () => {
  it("answers health with the contract's Health", async () => {
    const { url } = await start({});
    const res = await fetch(`${url}/v1/health`);
    assert.equal(res.status, 200);
    assert.deepEqual(Health.parse(await res.json()), { protocol: BRIDGE_ID, version: "test", ok: true, features: { byok: true } });
  });

  it("reports the byok release flag in health, so the plugin can mirror it", async () => {
    const { url } = await start({ features: { byok: false } });
    const res = await fetch(`${url}/v1/health`);
    assert.deepEqual(Health.parse(await res.json()).features, { byok: false });
  });

  it("streams a plan: header first, each part started then done, then the full score", async () => {
    const { url, logs } = await start({});
    const res = await post(`${url}/v1/plan`, request);
    assert.equal(res.status, 200);
    const evs = await events(res);

    assert.equal(evs[0]!.type, "header");
    assert.deepEqual((evs[0] as Extract<ServiceEvent, { type: "header" }>).score.parts, []);
    const last = evs.at(-1)!;
    assert.equal(last.type, "done");
    assert.deepEqual((last as Extract<ServiceEvent, { type: "done" }>).score, score);

    const started = new Set<string>();
    const done: string[] = [];
    for (const e of evs.slice(1, -1)) {
      if (e.type === "partStarted") started.add(e.partId);
      else if (e.type === "partDone") {
        assert.ok(started.has(e.part.id), `partDone ${e.part.id} without partStarted`);
        done.push(e.part.id);
      } else assert.fail(`unexpected ${e.type} between header and done`);
    }
    assert.deepEqual(done, score.parts.map((p) => p.id));

    const line = await untilLogged(logs);
    assert.equal(line.status, "done");
    assert.equal(line.provider, "faux");
    assert.equal(line.credential, "managed");
    assert.ok(!JSON.stringify(line).includes("rainy"), "the log keeps no prompt text");
  });

  it("plans only the requested roles", async () => {
    let asked = "";
    const { url } = await start({
      backendFor: () => stubBackend(async (_s, turns) => {
        asked = turns[0]!.text;
        throw new FlowstateError("provider", "stop here");
      }),
    });
    await events(await post(`${url}/v1/plan`, { ...request, roles: ["bass", "drums", "bass"] }));
    assert.match(asked, /lanes to write: bass, drums \(/);
  });

  it("rejects malformed requests with bad_request", async () => {
    const { url } = await start({});
    for (const body of ["{nope", { ...request, protocol: "flowstate.bridge.v9" }, { ...request, context: null }]) {
      const res = await post(`${url}/v1/plan`, body);
      assert.equal(res.status, 400);
      assert.equal(errorOf(await events(res)).code, "bad_request");
    }
    assert.equal((await fetch(`${url}/v2/plan`)).status, 404);
  });

  it("plans around kept parts: they stream first and come back unchanged; the rest is new", async () => {
    const chords = score.parts.find((p) => p.role === "chords")!;
    const others = score.parts.filter((p) => p !== chords);
    const { url } = await start({ backendFor: fauxBackends(JSON.stringify({ parts: others })).backendFor });
    const res = await post(`${url}/v1/plan`, { ...request, keep: { ...score, parts: [chords] } });
    assert.equal(res.status, 200);
    const evs = await events(res);
    assert.equal(evs[0]!.type, "header");
    const done = (e: ServiceEvent) => e.type === "partDone";
    assert.deepEqual(evs.filter(done).map((e) => (e as Extract<ServiceEvent, { type: "partDone" }>).part.id), [chords.id, ...others.map((p) => p.id)]);
    const final = evs.at(-1) as Extract<ServiceEvent, { type: "done" }>;
    assert.equal(final.type, "done");
    assert.deepEqual(final.score!.parts.find((p) => p.role === "chords"), chords);
    assert.equal(final.score!.parts.length, 4);
  });

  it("refuses a plan where every requested part is locked, and still answers a reference with unavailable", async () => {
    const { url } = await start({});
    const allLocked = await post(`${url}/v1/plan`, { ...request, keep: score });
    assert.equal(allLocked.status, 400);
    assert.match(errorOf(await events(allLocked)).message, /every requested part is locked/);
    const ref = await post(`${url}/v1/plan`, { ...request, reference: { score, intent: "continue" } });
    assert.equal(ref.status, 503);
    assert.equal(errorOf(await events(ref)).code, "unavailable");
  });

  it("passes a BYOK key to the provider and never logs or echoes it", async () => {
    const key = "sk-user-secret-123";
    const faux = fauxBackends(scoreText);
    const { url, logs } = await start({ backendFor: faux.backendFor });
    const res = await post(`${url}/v1/plan`, { ...request, provider: { provider: "faux", model: "other" } }, { [KEY_HEADER]: key });
    const text = await res.text();
    assert.equal(res.status, 200);
    assert.deepEqual(faux.selections[0]!.credential, { kind: "byok", apiKey: key });
    assert.equal(faux.selections[0]!.model, "other");
    const line = await untilLogged(logs);
    assert.equal(line.credential, "byok");
    assert.ok(!text.includes(key) && !JSON.stringify(logs).includes(key));
  });

  it("redacts the key from provider errors", async () => {
    const key = "sk-user-secret-456";
    const { url, logs } = await start({
      backendFor: (sel) => stubBackend(async () => {
        throw providerFailure("error", `401 bad key ${sel.credential.kind === "byok" ? sel.credential.apiKey : ""}`);
      }),
    });
    const res = await post(`${url}/v1/plan`, { ...request, provider: { provider: "faux", model: "faux-model" } }, { [KEY_HEADER]: key });
    const error = errorOf(await events(res));
    assert.equal(error.code, "provider");
    assert.ok(!error.message.includes(key) && error.message.includes("[key]"));
    await untilLogged(logs);
    assert.ok(!JSON.stringify(logs).includes(key));
  });

  it("hides BYOK behind the release flag, and needs a provider with a key", async () => {
    const off = await start({ features: { byok: false } });
    const res = await post(`${off.url}/v1/plan`, { ...request, provider: { provider: "faux", model: "faux-model" } }, { [KEY_HEADER]: "k-1234" });
    assert.equal(res.status, 503);
    assert.equal(errorOf(await events(res)).code, "unavailable");

    const on = await start({});
    const noProvider = await post(`${on.url}/v1/plan`, request, { [KEY_HEADER]: "k-1234" });
    assert.equal(errorOf(await events(noProvider)).code, "bad_request");
  });

  it("offers only the configured models on managed keys", async () => {
    const { url } = await start({ managedAlso: ["faux/other"] });
    assert.equal((await post(`${url}/v1/plan`, { ...request, provider: { provider: "faux", model: "other" } })).status, 200);
    const res = await post(`${url}/v1/plan`, { ...request, provider: { provider: "openai", model: "gpt-5.5" } });
    assert.equal(res.status, 400);
    assert.match(errorOf(await events(res)).message, /isn't offered with managed access/);
  });

  for (const [what, err, code] of [
    ["a refusal", providerFailure("refusal", "The model refused"), "refused"],
    ["truncated output", new FlowstateError("truncated", "hit the output token limit"), "truncated"],
    ["a bug", new TypeError("x is undefined"), "internal"],
  ] as const) {
    it(`reports ${what} as ${code}`, async () => {
      const { url } = await start({ backendFor: () => stubBackend(async () => { throw err; }) });
      const res = await post(`${url}/v1/plan`, request);
      assert.equal(res.status, 200);
      assert.equal(errorOf(await events(res)).code, code);
    });
  }

  it("reports a plan still invalid after repairs as invalid_score", async () => {
    const { url } = await start({ backendFor: fauxBackends("not json", "still not", "nope").backendFor });
    assert.equal(errorOf(await events(await post(`${url}/v1/plan`, request))).code, "invalid_score");
  });

  it("stops the provider stream when the client cancels", async () => {
    let aborted = false;
    const { url, logs } = await start({
      backendFor: () => stubBackend((_s, _t, options = {}) => {
        options.onText?.(headText);
        return new Promise((_, reject) => options.signal?.addEventListener("abort", () => {
          aborted = true;
          reject(new FlowstateError("cancelled", "aborted"));
        }));
      }),
    });
    const client = new AbortController();
    const res = await post(`${url}/v1/plan`, request, {}, client.signal);
    const reader = res.body!.getReader();
    const first = new TextDecoder().decode((await reader.read()).value);
    assert.match(first, /"type":"header"/);
    client.abort();
    const line = await untilLogged(logs);
    assert.ok(aborted, "the provider call saw the abort");
    assert.equal(line.status, "cancelled");
  });

  it("sends keepalive comments while the model writes", async () => {
    const { url } = await start({
      keepaliveMs: 20,
      backendFor: () => stubBackend(async () => {
        await new Promise((r) => setTimeout(r, 120));
        throw new FlowstateError("provider", "done waiting");
      }),
    });
    const text = await (await post(`${url}/v1/plan`, request)).text();
    assert.match(text, /^: keepalive$/m);
  });

  it("refuses edits it can't serve before the stream starts", async () => {
    const { url } = await start({});
    const edit = { protocol: BRIDGE_ID, kind: "edit", prompt: "busier bass", score, partIds: null, role: null, history: [], provider: null };
    const bad = await post(`${url}/v1/edit`, { ...edit, score: null });
    assert.equal(bad.status, 400);
    assert.equal(errorOf(await events(bad)).code, "bad_request");
    const twoParts = await post(`${url}/v1/edit`, { ...edit, kind: "vary", partIds: ["bass", "drums"] });
    assert.equal(twoParts.status, 400);
    assert.match(errorOf(await events(twoParts)).message, /exactly one part/);
  });

  it("streams an edit: the edited head, only the changed parts, the note, then the whole score", async () => {
    const bass = structuredClone(score.parts.find((p) => p.role === "bass")!);
    bass.velocity = 100;
    const patch = JSON.stringify({ message: "Pushed the bass forward.", parts: [bass] }, null, 2);
    const { url } = await start({ backendFor: fauxBackends(patch).backendFor });
    const res = await post(`${url}/v1/edit`, { protocol: BRIDGE_ID, kind: "edit", prompt: "louder bass", score, partIds: [bass.id], role: null, history: [], provider: null });
    assert.equal(res.status, 200);
    const evs = await events(res);
    assert.deepEqual(evs.map((e) => e.type), ["header", "partStarted", "partDone", "message", "done"]);
    assert.deepEqual((evs[2] as Extract<ServiceEvent, { type: "partDone" }>).part, bass);
    const done = (evs.at(-1) as Extract<ServiceEvent, { type: "done" }>).score!;
    assert.deepEqual(done.parts.map((p) => p.id), score.parts.map((p) => p.id));
    assert.deepEqual(done.parts.filter((p) => p.id !== bass.id), score.parts.filter((p) => p.id !== bass.id));
  });

  it("answers a question about the score with text and no score", async () => {
    const { url } = await start({ backendFor: fauxBackends(JSON.stringify({ message: "It's in Eb major." })).backendFor });
    const res = await post(`${url}/v1/edit`, { protocol: BRIDGE_ID, kind: "edit", prompt: "what key is this?", score, partIds: null, role: null, history: [], provider: null });
    const evs = await events(res);
    assert.deepEqual(evs, [{ type: "message", text: "It's in Eb major." }, { type: "done", score: null }]);
  });
});
