// Model backends for the planner.
// - "api": Anthropic API via the TypeScript SDK (production path; billed per token).
// - "claude-code": headless Claude Code (`claude -p`) on the developer's own subscription login.
//   Development and eval runs only; never an end-user path.
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import Anthropic from "@anthropic-ai/sdk";

export interface Turn { role: "user" | "assistant"; text: string }
export interface Completion { text: string; inputTokens: number; outputTokens: number; cacheReadTokens: number }
export interface Backend { name: string; model: string; complete(system: string, turns: Turn[]): Promise<Completion> }

export function backendFromEnv(): Backend {
  const name = process.env.FLOWSTATE_PLANNER_BACKEND ?? "claude-code";
  if (name === "api") return apiBackend(process.env.FLOWSTATE_PLANNER_MODEL ?? "claude-opus-5");
  if (name === "claude-code") return claudeCodeBackend(process.env.FLOWSTATE_PLANNER_MODEL ?? "opus");
  throw new Error(`unknown FLOWSTATE_PLANNER_BACKEND '${name}' (use api or claude-code)`);
}

function apiBackend(model: string): Backend {
  const client = new Anthropic();
  return {
    name: "api",
    model,
    async complete(system, turns) {
      const response = await client.beta.messages
        .stream({
          model,
          max_tokens: 32000,
          betas: ["server-side-fallback-2026-07-01"],
          fallbacks: "default",
          thinking: { type: "adaptive" },
          output_config: { effort: "high" },
          system: [{ type: "text", text: system, cache_control: { type: "ephemeral" } }],
          messages: turns.map((t) => ({ role: t.role, content: t.text })),
        })
        .finalMessage();
      if (response.stop_reason === "refusal") throw new Error(`planner refused: ${JSON.stringify(response.stop_details)}`);
      if (response.stop_reason === "max_tokens") throw new Error("planner hit max_tokens before finishing the score");
      return {
        text: response.content.flatMap((b) => (b.type === "text" ? [b.text] : [])).join(""),
        inputTokens: response.usage.input_tokens,
        outputTokens: response.usage.output_tokens,
        cacheReadTokens: response.usage.cache_read_input_tokens ?? 0,
      };
    },
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
