import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import path from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import {
  contentText, createModels, fauxAssistantMessage, fauxProvider, fauxToolCall, type ToolResultMessage,
} from "@earendil-works/pi-ai";
import type { Score } from "@flowstate/schema";
import { Type } from "typebox";
import { analyzerPath, clipAnalyzer } from "./analyzer.ts";
import { backendFor } from "./backends.ts";
import type { CapabilityContext } from "./capabilities/api.ts";
import { ALLOWED_TOOLS, CAPABILITIES, createLoadout } from "./capabilities/registry.ts";
import { loadCatalog } from "./examples.ts";
import { planScore, type PlanRequest } from "./planner.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..", "..");
const capabilitiesDir = path.join(here, "capabilities");
const ID = "lofi-rainy-study";
const request: PlanRequest = JSON.parse(readFileSync(path.join(repo, "evals", "prompts", "phase0.json"), "utf8"))
  .find((p: { id: string }) => p.id === ID);
const score: Score = JSON.parse(readFileSync(path.join(repo, "evals", "results", "p1-1", "openrouter-gpt-5.5", `${ID}.score.json`), "utf8"));
const catalog = loadCatalog();
const ctx = (over: Partial<CapabilityContext> = {}): CapabilityContext => ({ request: request.controls, catalog, analyzeClip: null, ...over });

describe("capabilities can't reach the shell or filesystem", () => {
  const sources = readdirSync(capabilitiesDir).filter((f) => f.endsWith(".ts")).map((f) => ({ f, src: readFileSync(path.join(capabilitiesDir, f), "utf8") }));

  it("import nothing but each other, the schema and typebox; Pi packages for types only", () => {
    assert.ok(sources.length >= 5);
    for (const { f, src } of sources) {
      for (const m of src.matchAll(/^\s*import\s+(type\s+)?[^"']*?from\s+["']([^"']+)["']/gm)) {
        const [, typeOnly, spec] = m as unknown as [string, string | undefined, string];
        if (spec.startsWith("./")) continue;
        if (spec === "@flowstate/schema" || spec === "typebox") continue;
        if (typeOnly && spec.startsWith("@earendil-works/pi-")) continue;
        assert.fail(`${f} imports '${spec}'${typeOnly ? " (type)" : ""}: capabilities get I/O only through ctx`);
      }
      assert.doesNotMatch(src, /^\s*import\s+["']/m, `${f}: side-effect import`);
      assert.doesNotMatch(src, /\bimport\s*\(|\brequire\s*\(|\bprocess\.|\bglobalThis\b|\beval\s*\(|\bnew\s+Function\b|\bfetch\s*\(/, `${f} reaches past its imports`);
    }
  });

  it("registry: every tool is on the allowlist, and registering one outside it throws", async () => {
    const all = await createLoadout({ prompt: "x", ctx: ctx(), tools: true });
    assert.deepEqual(all.tools.map((t) => t.name).sort(), [...ALLOWED_TOOLS].sort());
    for (const name of ["bash", "read", "write", "edit", "mcp__fs__read_file"]) {
      const rogue = { name: "rogue", factory: (pi: Parameters<(typeof CAPABILITIES)[number]["factory"]>[0]) =>
        pi.registerTool({ name, label: name, description: name, parameters: Type.Object({}), execute: async () => ({ content: [], details: {} }) }) };
      await assert.rejects(createLoadout({ prompt: "x", ctx: ctx(), tools: true, capabilities: [rogue] }), /not in the tool allowlist/);
    }
  });

  it("beforeToolCall refuses tools outside the allowlist, and allowed tools that weren't offered", async () => {
    const on = await createLoadout({ prompt: "x", ctx: ctx(), tools: true });
    for (const name of ["bash", "read", "write", "exec", "mcp__fs__read_file"]) {
      const r = await on.beforeToolCall({ toolCall: { type: "toolCall", id: "1", name, arguments: {} }, args: {} });
      assert.equal(r?.block, true, name);
    }
    assert.equal(await on.beforeToolCall({ toolCall: { type: "toolCall", id: "1", name: "library_examples", arguments: {} }, args: {} }), undefined);
    const off = await createLoadout({ prompt: "x", ctx: ctx(), tools: false });
    assert.deepEqual(off.tools, []);
    assert.equal((await off.beforeToolCall({ toolCall: { type: "toolCall", id: "1", name: "library_examples", arguments: {} }, args: {} }))?.block, true);
  });

  it("a capability's tool_call handler can block a call", async () => {
    const guard = { name: "guard", factory: (pi: Parameters<(typeof CAPABILITIES)[number]["factory"]>[0]) =>
      pi.on("tool_call", (e) => (e.toolName === "library_examples" ? { block: true, reason: "not now" } : undefined)) };
    const l = await createLoadout({ prompt: "x", ctx: ctx(), tools: true, capabilities: [...CAPABILITIES, guard] });
    const r = await l.beforeToolCall({ toolCall: { type: "toolCall", id: "1", name: "library_examples", arguments: {} }, args: {} });
    assert.deepEqual(r, { block: true, reason: "not now" });
  });

  it("clip analysis takes only catalog ids: a path or a command never reaches fs-analyze", async () => {
    let ran = 0;
    const analyze = clipAnalyzer(catalog, { exe: process.execPath });
    assert.ok(analyze);
    const counted = async (id: string) => (ran++, analyze(id));
    for (const id of ["../../etc/passwd", "lib:godflow/x; rm -rf /", "/etc/passwd", "lib:nope/missing"])
      await assert.rejects(counted(id), /no library clip/);
    assert.equal(ran, 4);
  });
});

/** A faux model whose replies are scripted, recording the tool results it was sent. */
function scripted(...steps: Parameters<ReturnType<typeof fauxProvider>["setResponses"]>[0]) {
  const faux = fauxProvider({ provider: "faux", models: [{ id: "faux-model" }], tokenSize: { min: 8, max: 64 } });
  const models = createModels();
  models.setProvider(faux.provider);
  const toolResults: ToolResultMessage[][] = [];
  faux.setResponses(steps.map((step) => (c, o, s, m) => {
    toolResults.push(c.messages.filter((x): x is ToolResultMessage => x.role === "toolResult"));
    return typeof step === "function" ? step(c, o, s, m) : step;
  }));
  return { backend: backendFor({ provider: "faux", model: "faux-model", credential: { kind: "managed" } }, models), toolResults };
}

describe("tools in the agent loop", () => {
  const withTools: PlanRequest = { ...request, tools: true };
  const host = { catalog, analyzeClip: null };

  it("refuses a shell tool the model makes up, runs an allowed one, then plans", async () => {
    const { backend, toolResults } = scripted(
      fauxAssistantMessage(fauxToolCall("bash", { command: "cat /etc/passwd" }), { stopReason: "toolUse" }),
      fauxAssistantMessage(fauxToolCall("library_examples", { lane: "chords", style: ["neo-soul"], count: 1 }), { stopReason: "toolUse" }),
      fauxAssistantMessage(JSON.stringify(score)),
    );
    const result = await planScore(withTools, backend, { host });
    const [bash] = toolResults[1]!;
    assert.equal(bash!.isError, true);
    assert.match(contentText(bash!.content), /bash not found/);
    const examples = toolResults[2]!.at(-1)!;
    assert.equal(examples.isError, false);
    assert.match(contentText(examples.content), /Style examples from Flowstate's built-in library \(MIDI by GodFlow/);
    assert.deepEqual(result.toolCalls, ["bash", "library_examples"]);
    assert.equal(result.attempts, 1);
    assert.deepEqual(result.score, score);
  });

  it("offers no tools unless asked, so a call fails without running", async () => {
    const { backend, toolResults } = scripted(
      fauxAssistantMessage(fauxToolCall("library_examples", {}), { stopReason: "toolUse" }),
      fauxAssistantMessage(JSON.stringify(score)),
    );
    await planScore(request, backend, { host });
    assert.match(contentText(toolResults[1]![0]!.content), /not found/);
  });

  it("validates arguments before any capability runs", async () => {
    const { backend, toolResults } = scripted(
      fauxAssistantMessage(fauxToolCall("analyze_clip", { entryId: "../../../etc/passwd" }), { stopReason: "toolUse" }),
      fauxAssistantMessage(JSON.stringify(score)),
    );
    let analyzed = 0;
    await planScore(withTools, backend, { host: { catalog, analyzeClip: async () => { analyzed++; throw new Error("unreachable"); } } });
    assert.equal(toolResults[1]![0]!.isError, true);
    assert.equal(analyzed, 0);
  });

  it("stops running tools after the per-request budget", async () => {
    const call = () => fauxAssistantMessage(fauxToolCall("library_examples", {}), { stopReason: "toolUse" });
    const { backend, toolResults } = scripted(call(), call(), call(), call(), call(), fauxAssistantMessage(JSON.stringify(score)));
    const result = await planScore(withTools, backend, { host });
    assert.match(contentText(toolResults[5]!.at(-1)!.content), /no more tool calls/);
    assert.deepEqual(result.score, score);
  });

  it("analyzes a library clip with core's fs-analyze", { skip: !existsSync(analyzerPath()) && "fs-analyze not built" }, async () => {
    const id = "lib:godflow/rnb-jazz-chords-01";
    const { backend, toolResults } = scripted(
      fauxAssistantMessage(fauxToolCall("analyze_clip", { entryId: id }), { stopReason: "toolUse" }),
      fauxAssistantMessage(JSON.stringify(score)),
    );
    await planScore(withTools, backend, { host: { catalog, analyzeClip: clipAnalyzer(catalog) } });
    const result = toolResults[1]![0]!;
    assert.equal(result.isError, false, contentText(result.content));
    assert.match(contentText(result.content), new RegExp(`^${id}: chords, 4 bars of 4/4[\\s\\S]*grid: \\d`));
  });
});
