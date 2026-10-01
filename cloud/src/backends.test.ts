import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { afterEach, describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { createModels, fauxAssistantMessage, fauxProvider, type SimpleStreamOptions } from "@earendil-works/pi-ai";
import { backendFor, selectionFromEnv, type ModelSelection } from "./backends.ts";

function fauxSetup() {
  const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model", reasoning: true }] });
  const models = createModels();
  models.setProvider(faux.provider);
  return { faux, models };
}

const managed = (model = "faux-model"): ModelSelection => ({ provider: "faux", model, credential: { kind: "managed" } });

describe("selectionFromEnv", () => {
  const saved = { ...process.env };
  afterEach(() => {
    for (const k of Object.keys(process.env)) if (k.startsWith("FLOWSTATE_PLANNER_")) delete process.env[k];
    Object.assign(process.env, saved);
  });
  const clear = () => {
    for (const k of Object.keys(process.env)) if (k.startsWith("FLOWSTATE_PLANNER_")) delete process.env[k];
  };

  it("defaults to claude-code for dev", () => {
    clear();
    assert.deepEqual(selectionFromEnv(), { provider: "claude-code", model: "opus", credential: { kind: "managed" } });
  });

  it("maps the old api backend to Anthropic through pi", () => {
    clear();
    process.env.FLOWSTATE_PLANNER_BACKEND = "api";
    assert.equal(selectionFromEnv().provider, "anthropic");
    assert.equal(selectionFromEnv().model, "claude-opus-5");
  });

  it("takes provider and model from the environment, and overrides over both", () => {
    clear();
    process.env.FLOWSTATE_PLANNER_BACKEND = "pi";
    process.env.FLOWSTATE_PLANNER_PROVIDER = "openai";
    process.env.FLOWSTATE_PLANNER_MODEL = "o3";
    assert.equal(selectionFromEnv().provider, "openai");
    assert.equal(selectionFromEnv({ provider: "google", model: "gemini-flash-latest" }).model, "gemini-flash-latest");
  });

  it("requires a model when the provider has no default", () => {
    clear();
    process.env.FLOWSTATE_PLANNER_BACKEND = "pi";
    process.env.FLOWSTATE_PLANNER_PROVIDER = "openai";
    assert.throws(() => selectionFromEnv(), /no default model/);
  });
});

describe("backendFor", () => {
  it("rejects unknown providers and models", () => {
    const { models } = fauxSetup();
    assert.throws(() => backendFor({ ...managed(), provider: "nope" }, models), /unknown provider/);
    assert.throws(() => backendFor(managed("nope"), models), /unknown model 'nope'.*faux-model/);
  });

  it("rejects an empty BYOK key instead of falling back to the managed key", () => {
    const { models } = fauxSetup();
    assert.throws(() => backendFor({ ...managed(), credential: { kind: "byok", apiKey: " " } }, models), /without a key/);
  });

  it("never takes a user key on claude-code", () => {
    assert.throws(() => backendFor({ provider: "claude-code", model: "opus", credential: { kind: "byok", apiKey: "k" } }), /takes no key/);
  });

  it("returns the text and usage, and passes the BYOK key with the request", async () => {
    const { faux, models } = fauxSetup();
    let seen: SimpleStreamOptions | undefined;
    faux.setResponses([(_ctx, options) => ((seen = options), fauxAssistantMessage("{\"ir\":1}"))]);
    const backend = backendFor({ ...managed(), credential: { kind: "byok", apiKey: "user-key" } }, models);
    const out = await backend.complete("system", [{ role: "user", text: "hi" }]);
    assert.equal(out.text, "{\"ir\":1}");
    assert.equal(seen?.apiKey, "user-key");
    assert.equal(seen?.reasoning, "high");
  });

  it("sends no thinking level for reasoning off, so the provider does not think", async () => {
    const { faux, models } = fauxSetup();
    let seen: SimpleStreamOptions | undefined;
    faux.setResponses([(_ctx, options) => ((seen = options), fauxAssistantMessage("ok"))]);
    await backendFor({ ...managed(), reasoning: "off" }, models).complete("s", [{ role: "user", text: "x" }]);
    assert.ok(seen && !("reasoning" in seen));
  });

  it("replays repair turns as a conversation", async () => {
    const { faux, models } = fauxSetup();
    let roles: string[] = [];
    faux.setResponses([(ctx) => ((roles = ctx.messages.map((m) => m.role)), fauxAssistantMessage("ok"))]);
    await backendFor(managed(), models).complete("system", [
      { role: "user", text: "plan" }, { role: "assistant", text: "bad" }, { role: "user", text: "fix" },
    ]);
    assert.deepEqual(roles.filter((r) => r !== "system"), ["user", "assistant", "user"]);
  });

  it("surfaces refusals and provider errors as errors", async () => {
    const { faux, models } = fauxSetup();
    faux.setResponses([fauxAssistantMessage("", { stopReason: "error", errorMessage: "The model refused to complete the request" })]);
    await assert.rejects(backendFor(managed(), models).complete("s", [{ role: "user", text: "x" }]), /refused/);
  });

  it("surfaces truncated output as an error", async () => {
    const { faux, models } = fauxSetup();
    faux.setResponses([fauxAssistantMessage("{\"ir\":", { stopReason: "length" })]);
    await assert.rejects(backendFor(managed(), models).complete("s", [{ role: "user", text: "x" }]), /output token limit/);
  });
});

describe("dependencies", () => {
  it("has no Pi coding-agent packages in the tree", () => {
    const here = path.dirname(fileURLToPath(import.meta.url));
    const lock = JSON.parse(readFileSync(path.join(here, "..", "..", "package-lock.json"), "utf8"));
    const pi = Object.keys(lock.packages)
      .map((p) => p.slice(p.lastIndexOf("node_modules/") + "node_modules/".length))
      .filter((name) => name.startsWith("@earendil-works/"));
    assert.ok(pi.includes("@earendil-works/pi-ai"));
    // pi-ai and its telemetry contracts only; the coding agent, agent core and TUI stay out.
    assert.deepEqual([...new Set(pi)].sort(), ["@earendil-works/pi-ai", "@earendil-works/pi-telemetry"]);
  });
});
