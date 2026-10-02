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
import { FlowstateError, providerFailure } from "./errors.ts";

export interface Turn { role: "user" | "assistant"; text: string }
export interface Completion {
  text: string;
  inputTokens: number;
  outputTokens: number;
  cacheReadTokens: number;
  /** Time to the first streamed event of any kind (thinking or text), from the call. */
  firstTokenMs: number | null;
  /** Time to the first answer text, after any thinking. */
  firstTextMs: number | null;
}
export interface CompleteOptions {
  /** Called with each answer-text delta as it streams in. Thinking is not passed on. */
  onText?: (delta: string) => void;
  /** Aborting stops the provider stream; the call rejects. */
  signal?: AbortSignal;
}
export interface Backend {
  name: string;
  provider: string;
  model: string;
  complete(system: string, turns: Turn[], options?: CompleteOptions): Promise<Completion>;
}

export type Credential = { kind: "managed" } | { kind: "byok"; apiKey: string };
export interface ModelSelection {
  /** A pi-ai provider id ("anthropic", "openai", "google", "openrouter", ...) or "claude-code". */
  provider: string;
  model: string;
  credential: Credential;
  /** Thinking effort; "off" asks the provider not to think at all. Default "high". */
  reasoning?: Reasoning;
}

export type Reasoning = ThinkingLevel | "off";

const DEFAULT_MODEL: Record<string, string> = { "claude-code": "opus", anthropic: "claude-opus-5" };
const MAX_OUTPUT_TOKENS = 32000;

/**
 * The backend for one request.
 * Throws `bad_request` on an unknown provider or model, a BYOK selection without a key, or BYOK on claude-code.
 */
export function backendFor(sel: ModelSelection, collection: Models = builtinCollection()): Backend {
  if (sel.provider === "claude-code") {
    if (sel.credential.kind !== "managed") throw new FlowstateError("bad_request", "claude-code is a dev backend and takes no key");
    return claudeCodeBackend(sel.model, sel.reasoning ?? "high");
  }
  return piBackend(sel, collection);
}

/**
 * The CLI and eval default, from the environment:
 * FLOWSTATE_PLANNER_BACKEND (claude-code | pi; "api" means pi with Anthropic), FLOWSTATE_PLANNER_PROVIDER,
 * FLOWSTATE_PLANNER_MODEL, FLOWSTATE_PLANNER_REASONING. Keys come from the provider's environment
 * variable (ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY, OPENROUTER_API_KEY, ...).
 */
export function selectionFromEnv(overrides: { provider?: string; model?: string; reasoning?: Reasoning } = {}): ModelSelection {
  const backend = process.env.FLOWSTATE_PLANNER_BACKEND ?? "claude-code";
  let provider: string;
  if (backend === "claude-code") provider = "claude-code";
  else if (backend === "api") provider = "anthropic";
  else if (backend === "pi") provider = process.env.FLOWSTATE_PLANNER_PROVIDER ?? "anthropic";
  else throw new Error(`unknown FLOWSTATE_PLANNER_BACKEND '${backend}' (use pi or claude-code)`);
  provider = overrides.provider ?? provider;
  const model = overrides.model ?? process.env.FLOWSTATE_PLANNER_MODEL ?? DEFAULT_MODEL[provider];
  if (!model) throw new Error(`no default model for provider '${provider}'; set FLOWSTATE_PLANNER_MODEL or --model`);
  const reasoning = overrides.reasoning ?? (process.env.FLOWSTATE_PLANNER_REASONING as Reasoning | undefined);
  return { provider, model, credential: { kind: "managed" }, ...(reasoning ? { reasoning } : {}) };
}

// One collection for the process: it holds catalogs only. Credentials are passed per request.
let builtin: Models | undefined;
function builtinCollection(): Models {
  return (builtin ??= builtinModels());
}

function piBackend(sel: ModelSelection, models: Models): Backend {
  if (!models.getProvider(sel.provider)) throw new FlowstateError("bad_request", `unknown provider '${sel.provider}'`);
  const model = models.getModel(sel.provider, sel.model);
  if (!model) {
    const known = models.getModels(sel.provider).map((m) => m.id);
    throw new FlowstateError("bad_request", `unknown model '${sel.model}' for ${sel.provider} (known: ${known.slice(0, 20).join(", ")}${known.length > 20 ? ", ..." : ""})`);
  }
  // An empty BYOK key would silently fall back to the service's own key.
  if (sel.credential.kind === "byok" && !sel.credential.apiKey.trim()) throw new FlowstateError("bad_request", "BYOK request without a key");
  const apiKey = sel.credential.kind === "byok" ? sel.credential.apiKey : undefined;

  return {
    name: "pi",
    provider: sel.provider,
    model: sel.model,
    async complete(system, turns, options = {}) {
      const context: Context = { systemPrompt: system, messages: turns.map((t) => toMessage(t, model)) };
      const started = Date.now();
      let firstTokenMs: number | null = null;
      let firstTextMs: number | null = null;
      // pi-ai turns thinking off when no level is given. A level the model lacks is raised to the next one it has.
      const level = sel.reasoning ?? "high";
      const stream = models.streamSimple(model, context, {
        ...(level === "off" ? {} : { reasoning: level }),
        maxTokens: Math.min(MAX_OUTPUT_TOKENS, model.maxTokens),
        ...(apiKey ? { apiKey } : {}),
        ...(options.signal ? { signal: options.signal } : {}),
      });
      for await (const event of stream) {
        if (firstTokenMs === null && (event.type === "text_delta" || event.type === "thinking_delta")) firstTokenMs = Date.now() - started;
        if (event.type === "text_delta") {
          firstTextMs ??= Date.now() - started;
          options.onText?.(event.delta);
        }
      }
      const response = await stream.result();
      // pi-ai maps refusals and safety stops to "error", and the output-token limit to "length".
      if (response.stopReason === "error" || response.stopReason === "aborted") {
        const raw = response.stopReason === "aborted" ? "aborted" : response.rawStopReason ?? response.stopReason;
        throw providerFailure(raw, `planner call failed (${sel.provider}/${sel.model}, ${raw}): ${response.errorMessage ?? "no details"}`);
      }
      if (response.stopReason === "length") throw new FlowstateError("truncated", "planner hit the output token limit before finishing the score");
      return {
        text: response.content.flatMap((b) => (b.type === "text" ? [b.text] : [])).join(""),
        inputTokens: response.usage.input,
        outputTokens: response.usage.output,
        cacheReadTokens: response.usage.cacheRead,
        firstTokenMs,
        firstTextMs,
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

function claudeCodeBackend(model: string, reasoning: Reasoning): Backend {
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
    complete(system, turns, options = {}) {
      const args = [
        "-p", "--output-format", "stream-json", "--verbose", "--include-partial-messages",
        // claude has no "off" or "minimal" effort; low is its floor.
        "--model", model, "--effort", reasoning === "minimal" || reasoning === "off" ? "low" : reasoning,
        "--tools", "", "--no-session-persistence", "--system-prompt", system,
      ];
      return new Promise((resolve, reject) => {
        if (options.signal?.aborted) return reject(new FlowstateError("cancelled", "planner call aborted"));
        const started = Date.now();
        let firstTokenMs: number | null = null;
        let firstTextMs: number | null = null;
        let result: { result?: string; is_error?: boolean; usage?: Record<string, number> } | undefined;
        let buffered = "";
        let stderr = "";
        const child = spawn("claude", args, { cwd, env, stdio: ["pipe", "pipe", "pipe"] });
        const onAbort = () => child.kill("SIGTERM");
        options.signal?.addEventListener("abort", onAbort, { once: true });

        // One JSON event per line: partial-message stream events, then a final "result".
        const onLine = (line: string) => {
          if (!line.trim()) return;
          let msg: { type?: string; event?: { type?: string; delta?: { type?: string; text?: string } } } & typeof result;
          try {
            msg = JSON.parse(line);
          } catch {
            return;
          }
          if (msg.type === "result") result = msg;
          const delta = msg.type === "stream_event" && msg.event?.type === "content_block_delta" ? msg.event.delta : undefined;
          if (!delta) return;
          if (firstTokenMs === null && (delta.type === "text_delta" || delta.type === "thinking_delta")) firstTokenMs = Date.now() - started;
          if (delta.type === "text_delta" && delta.text) {
            firstTextMs ??= Date.now() - started;
            options.onText?.(delta.text);
          }
        };
        child.stdout.on("data", (d) => {
          buffered += d;
          let nl;
          while ((nl = buffered.indexOf("\n")) >= 0) {
            onLine(buffered.slice(0, nl));
            buffered = buffered.slice(nl + 1);
          }
        });
        child.stderr.on("data", (d) => (stderr += d));
        child.on("error", reject);
        child.on("close", (code) => {
          options.signal?.removeEventListener("abort", onAbort);
          onLine(buffered);
          if (options.signal?.aborted) return reject(new FlowstateError("cancelled", "planner call aborted"));
          if (!result) return reject(new FlowstateError("provider", `claude -p exited ${code}: ${stderr.slice(0, 400)}`));
          if (code !== 0 || result.is_error || typeof result.result !== "string")
            return reject(providerFailure("", `claude -p failed (${code}): ${(result.result ?? stderr).slice(0, 400)}`));
          resolve({
            text: result.result,
            inputTokens: result.usage?.input_tokens ?? 0,
            outputTokens: result.usage?.output_tokens ?? 0,
            cacheReadTokens: result.usage?.cache_read_input_tokens ?? 0,
            firstTokenMs,
            firstTextMs,
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
