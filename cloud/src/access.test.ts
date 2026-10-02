import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import type { AddressInfo } from "node:net";
import path from "node:path";
import { after, describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { createModels, fauxAssistantMessage, fauxProvider } from "@earendil-works/pi-ai";
import { BRIDGE_ID, PlanRequest, ServiceEvent, type Score } from "@flowstate/schema";
import { Access, hashToken, newToken, parseTokens } from "./access.ts";
import { backendFor, type Backend, type ModelSelection } from "./backends.ts";
import { createService, type ServiceConfig } from "./service.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const ID = "lofi-rainy-study";
const score: Score = JSON.parse(readFileSync(path.join(here, "..", "..", "evals", "results", "p1-1", "openrouter-gpt-5.5", `${ID}.score.json`), "utf8"));
const request = PlanRequest.parse({
  protocol: BRIDGE_ID,
  prompt: "rainy-day lofi hip hop loop",
  context: { tempo: 78, meterNumerator: 4, meterDenominator: 4, tonic: "Eb", mode: "major", bars: 4, swing: 0, style: ["lofi"] },
  roles: null, keep: null, reference: null, provider: null,
});
const MANAGED: ModelSelection = { provider: "faux", model: "faux-model", credential: { kind: "managed" } };

describe("tester tokens", () => {
  it("parses the tokens file, and rejects malformed lines and repeats", () => {
    const a = newToken();
    const b = newToken();
    assert.match(a, /^fst_[\w-]{43}$/);
    assert.notEqual(a, b);
    const testers = parseTokens(`# testers\nmac ${hashToken(a)}\n\nwin ${hashToken(b)}  # Ableton\n`);
    assert.deepEqual([...testers.values()], ["mac", "win"]);
    assert.throws(() => parseTokens(`mac ${a}`), /line 1/, "a raw token is not a hash");
    assert.throws(() => parseTokens(`mac ${hashToken(a)}\nwin ${hashToken(a)}`), /line 2: the same token/);
  });

  it("authenticates a bearer token and names the tester; anything else is unauthorized", () => {
    const token = newToken();
    const access = new Access(new Map([[hashToken(token), "mac"]]));
    assert.equal(access.authenticate(`Bearer ${token}`), "mac");
    for (const header of [undefined, "", token, `Basic ${token}`, `Bearer ${token}x`, `Bearer ${hashToken(token)}`])
      assert.throws(() => access.authenticate(header), (e: { code?: string }) => e.code === "unauthorized");
  });

  it("limits streams at once and requests an hour, per tester", () => {
    let now = 0;
    const access = new Access(new Map([["h1", "a"], ["h2", "b"]]), { concurrent: 2, perHour: 3 }, () => now);
    const r1 = access.admit("a");
    access.admit("a");
    assert.throws(() => access.admit("a"), (e: { code?: string }) => e.code === "rate_limited");
    access.admit("b"); // another tester has their own limits
    r1();
    r1(); // releasing twice frees one slot, not two
    access.admit("a");
    assert.throws(() => access.admit("a"), /3 requests an hour/);
    now += 60 * 60 * 1000;
    assert.throws(() => access.admit("a"), /at once/, "the hour is up, but two streams are still open");
  });
});

const servers: { close(): void }[] = [];
after(() => servers.forEach((s) => s.close()));

async function start(access: Access, backend?: Backend) {
  const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model" }] });
  const models = createModels();
  models.setProvider(faux.provider);
  faux.setResponses(Array.from({ length: 8 }, () => () => fauxAssistantMessage(JSON.stringify(score))));
  const logs: Record<string, unknown>[] = [];
  const config: ServiceConfig = {
    version: "test", managed: MANAGED, managedAlso: [], features: { byok: true }, keepaliveMs: 60_000,
    log: (l) => logs.push(l), backendFor: (sel) => backend ?? backendFor(sel, models), access,
  };
  const server = createService(config);
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  servers.push({ close: () => (server.closeAllConnections(), server.close()) });
  return { url: `http://127.0.0.1:${(server.address() as AddressInfo).port}`, logs };
}

const plan = (url: string, headers: Record<string, string> = {}, signal?: AbortSignal) =>
  fetch(`${url}/v1/plan`, { method: "POST", headers: { "content-type": "application/json", ...headers }, body: JSON.stringify(request), ...(signal ? { signal } : {}) });

const events = async (res: Response) =>
  (await res.text()).split("\n").filter((l) => l.startsWith("data: ")).map((l) => ServiceEvent.parse(JSON.parse(l.slice(6))));

describe("the hosted service", () => {
  const token = newToken();
  const testers = new Map([[hashToken(token), "mac"]]);

  it("leaves health open, and plans only with a valid token", async () => {
    const { url, logs } = await start(new Access(testers));
    assert.equal((await fetch(`${url}/v1/health`)).status, 200);

    for (const headers of [{}, { authorization: "Bearer nope" }] as Record<string, string>[]) {
      const res = await plan(url, headers);
      assert.equal(res.status, 401);
      const [e] = await events(res);
      assert.equal(e?.type === "error" && e.error.code, "unauthorized");
    }
    const ok = await plan(url, { authorization: `Bearer ${token}` });
    assert.equal(ok.status, 200);
    assert.equal((await events(ok)).at(-1)?.type, "done");
    assert.equal(logs.at(-1)?.tester, "mac");
    assert.ok(!JSON.stringify(logs).includes(token), "the token never reaches a log line");
  });

  it("answers 429 with Retry-After over a limit, and frees the slot when a stream ends or is cancelled", async () => {
    // A backend that writes nothing until it is aborted, so its stream stays open.
    const hanging: Backend = {
      name: "stub", provider: "faux", model: "faux-model",
      complete: (_s, _t, options = {}) => new Promise((_, reject) => options.signal?.addEventListener("abort", () => reject(new Error("aborted")))),
    };
    const { url } = await start(new Access(testers, { concurrent: 1, perHour: 10 }), hanging);
    const auth = { authorization: `Bearer ${token}` };
    const first = new AbortController();
    const open = await plan(url, auth, first.signal);
    assert.equal(open.status, 200);

    const over = await plan(url, auth);
    assert.equal(over.status, 429);
    assert.ok(Number(over.headers.get("retry-after")) > 0);
    const [e] = await events(over);
    assert.equal(e?.type === "error" && e.error.code, "rate_limited");

    first.abort();
    await new Promise((r) => setTimeout(r, 100));
    const again = new AbortController();
    assert.equal((await plan(url, auth, again.signal)).status, 200, "the cancelled stream gave its slot back");
    again.abort();
  });
});

describe("serve", () => {
  it("refuses to listen beyond loopback without tester tokens", () => {
    const run = spawnSync(process.execPath, ["--import", "tsx", path.join(here, "serve.ts")], {
      env: {
        ...process.env, FLOWSTATE_SERVICE_HOST: "0.0.0.0", FLOWSTATE_SERVICE_PORT: "0", FLOWSTATE_SERVICE_TOKENS_FILE: "",
        FLOWSTATE_PLANNER_BACKEND: "pi", FLOWSTATE_PLANNER_PROVIDER: "deepseek", FLOWSTATE_PLANNER_MODEL: "deepseek-flash",
      },
      encoding: "utf8",
      timeout: 30_000,
    });
    assert.equal(run.status, 1, run.stderr);
    assert.match(run.stderr, /Refusing to listen on 0\.0\.0\.0 without tester tokens/);
  });
});
