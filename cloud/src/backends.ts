// Model backends for the planner.
// - "pi": any provider pi-ai supports (Anthropic, OpenAI, Google, OpenRouter, ...). The production
//   path: provider, model and credential are chosen per request, with the service's own key
//   ("managed", from the provider's usual environment variable) or the user's key ("byok").
// - "claude-code": headless Claude Code (`claude -p`) on the developer's own subscription login.
//   Development and eval runs only; never an end-user path.
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import type { AssistantMessage, Context, Message, Models, ThinkingLevel } from "@earendil-works/pi-ai";
import { builtinModels } from "@earendil-works/pi-ai/providers/all";

export interface Turn { role: "user" | "assistant"; text: string }
export interface Completion { text: string; inputTokens: number; outputTokens: number; cacheReadTokens: number }
export interface Backend { name: string; provider: string; model: string; complete(system: string, turns: Turn[]): Promise<Completion> }

export type Credential = { kind: "managed" } | { kind: "byok"; apiKey: string };
export interface ModelSelection {
  /** A pi-ai provider id ("anthropic", "openai", "google", "openrouter", ...) or "claude-code". */
  provider: string;
  model: string;
  credential: Credential;
  reasoning?: ThinkingLevel;
}

const DEFAULT_MODEL: Record<string, string> = { "claude-code": "opus", anthropic: "claude-opus-5" };
const MAX_OUTPUT_TOKENS = 32000;

/**
 * The backend for one request.
 * Throws on an unknown provider or model, a BYOK selection without a key, or BYOK on claude-code.
 */
export function backendFor(sel: ModelSelection, collection: Models = builtinCollection()): Backend {
  if (sel.provider === "claude-code") {
    if (sel.credential.kind !== "managed") throw new Error("claude-code is a dev backend and takes no key");
    return claudeCodeBackend(sel.model);
  }
  return piBackend(sel, collection);
}

/**
 * The CLI and eval default, from the environment:
 * FLOWSTATE_PLANNER_BACKEND (claude-code | pi; "api" means pi with Anthropic), FLOWSTATE_PLANNER_PROVIDER,
 * FLOWSTATE_PLANNER_MODEL, FLOWSTATE_PLANNER_REASONING. Keys come from the provider's environment
 * variable (ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY, OPENROUTER_API_KEY, ...).
 */
export function selectionFromEnv(overrides: { provider?: string; model?: string } = {}): ModelSelection {
  const backend = process.env.FLOWSTATE_PLANNER_BACKEND ?? "claude-code";
  let provider: string;
  if (backend === "claude-code") provider = "claude-code";
  else if (backend === "api") provider = "anthropic";
  else if (backend === "pi") provider = process.env.FLOWSTATE_PLANNER_PROVIDER ?? "anthropic";
  else throw new Error(`unknown FLOWSTATE_PLANNER_BACKEND '${backend}' (use pi or claude-code)`);
  provider = overrides.provider ?? provider;
  const model = overrides.model ?? process.env.FLOWSTATE_PLANNER_MODEL ?? DEFAULT_MODEL[provider];
  if (!model) throw new Error(`no default model for provider '${provider}'; set FLOWSTATE_PLANNER_MODEL or --model`);
  const reasoning = process.env.FLOWSTATE_PLANNER_REASONING as ThinkingLevel | undefined;
  return { provider, model, credential: { kind: "managed" }, ...(reasoning ? { reasoning } : {}) };
}

// One collection for the process: it holds catalogs only. Credentials are passed per request.
let builtin: Models | undefined;
function builtinCollection(): Models {
  return (builtin ??= builtinModels());
}

function piBackend(sel: ModelSelection, models: Models): Backend {
  if (!models.getProvider(sel.provider)) throw new Error(`unknown provider '${sel.provider}'`);
  const model = models.getModel(sel.provider, sel.model);
  if (!model) {
    const known = models.getModels(sel.provider).map((m) => m.id);
    throw new Error(`unknown model '${sel.model}' for ${sel.provider} (known: ${known.slice(0, 20).join(", ")}${known.length > 20 ? ", ..." : ""})`);
  }
  // An empty BYOK key would silently fall back to the service's own key.
  if (sel.credential.kind === "byok" && !sel.credential.apiKey.trim()) throw new Error("BYOK request without a key");
  const apiKey = sel.credential.kind === "byok" ? sel.credential.apiKey : undefined;

  return {
    name: "pi",
    provider: sel.provider,
    model: sel.model,
    async complete(system, turns) {
      const context: Context = { systemPrompt: system, messages: turns.map((t) => toMessage(t, model)) };
      const response = await models.completeSimple(model, context, {
        reasoning: sel.reasoning ?? "high",
        maxTokens: Math.min(MAX_OUTPUT_TOKENS, model.maxTokens),
        ...(apiKey ? { apiKey } : {}),
      });
      // pi-ai maps refusals and safety stops to "error", and the output-token limit to "length".
      if (response.stopReason === "error" || response.stopReason === "aborted")
        throw new Error(`planner call failed (${sel.provider}/${sel.model}, ${response.rawStopReason ?? response.stopReason}): ${response.errorMessage ?? "no details"}`);
      if (response.stopReason === "length") throw new Error("planner hit the output token limit before finishing the score");
      return {
        text: response.content.flatMap((b) => (b.type === "text" ? [b.text] : [])).join(""),
        inputTokens: response.usage.input,
        outputTokens: response.usage.output,
        cacheReadTokens: response.usage.cacheRead,
      };
    },
  };
}

// Repair turns replay the planner's earlier answer as an assistant message.
function toMessage(t: Turn, model: { api: AssistantMessage["api"]; provider: string; id: string }): Message {
  if (t.role === "user") return { role: "user", content: t.text, timestamp: Date.now() };
  return {
    role: "assistant",
    content: [{ type: "text", text: t.text }],
    api: model.api,
    provider: model.provider,
    model: model.id,
    usage: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, totalTokens: 0, cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, total: 0 } },
    stopReason: "stop",
    timestamp: Date.now(),
  };
}

function claudeCodeBackend(model: string): Backend {
  // A clean working directory keeps repo CLAUDE.md/AGENTS.md out of the planner's context.
  const cwd = mkdtempSync(path.join(os.tmpdir(), "flowstate-planner-"));
  const env = { ...process.env };
  // An API key in the environment would take precedence over the subscription login.
  delete env.ANTHROPIC_API_KEY;
  delete env.ANTHROPIC_AUTH_TOKEN;

  return {
    name: "claude-code",
    provider: "claude-code",
    model,
    complete(system, turns) {
      const args = [
        "-p", "--output-format", "json", "--model", model, "--effort", "high",
        "--tools", "", "--no-session-persistence", "--system-prompt", system,
      ];
      return new Promise((resolve, reject) => {
        const child = spawn("claude", args, { cwd, env, stdio: ["pipe", "pipe", "pipe"] });
        let stdout = "";
        let stderr = "";
        child.stdout.on("data", (d) => (stdout += d));
        child.stderr.on("data", (d) => (stderr += d));
        child.on("error", reject);
        child.on("close", (code) => {
          let out: { result?: string; is_error?: boolean; usage?: Record<string, number> };
          try {
            out = JSON.parse(stdout);
          } catch {
            return reject(new Error(`claude -p exited ${code}: ${(stderr || stdout).slice(0, 400)}`));
          }
          if (code !== 0 || out.is_error || typeof out.result !== "string")
            return reject(new Error(`claude -p failed (${code}): ${(out.result ?? stderr).slice(0, 400)}`));
          resolve({
            text: out.result,
            inputTokens: out.usage?.input_tokens ?? 0,
            outputTokens: out.usage?.output_tokens ?? 0,
            cacheReadTokens: out.usage?.cache_read_input_tokens ?? 0,
          });
        });
        child.stdin.end(flatten(turns));
      });
    },
  };
}

// `claude -p` takes one prompt, so a repair conversation is replayed as a single message.
function flatten(turns: Turn[]): string {
  if (turns.length === 1) return turns[0]!.text;
  return turns
    .map((t) => (t.role === "user" ? `### Request\n${t.text}` : `### Your previous answer\n${t.text}`))
    .join("\n\n");
}
