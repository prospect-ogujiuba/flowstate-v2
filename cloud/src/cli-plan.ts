// Plans every prompt in a prompts file and realizes each score with core's fs-realize.
// Usage: tsx src/cli-plan.ts --prompts ../evals/prompts/phase0.json --out ../evals/out/v2 [--only id1,id2] [--concurrency 4]
//          [--provider openai --model gpt-5.5] [--plan-only | --realize-only]
// --provider/--model override the environment (see selectionFromEnv in backends.ts). The key comes from the
// provider's environment variable. run.json records the backend, per-prompt results and summary stats.
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { backendFor, selectionFromEnv } from "./backends.ts";
import { planScore, type PlanRequest } from "./planner.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.join(here, "..", "..");

function arg(name: string, fallback?: string): string {
  const i = process.argv.indexOf(`--${name}`);
  const value = i >= 0 ? process.argv[i + 1] : fallback;
  if (value === undefined) throw new Error(`missing --${name}`);
  return value;
}

interface PromptEntry extends PlanRequest { id: string }

const prompts: PromptEntry[] = JSON.parse(readFileSync(path.resolve(arg("prompts")), "utf8"));
const outDir = path.resolve(arg("out"));
const only = process.argv.includes("--only") ? new Set(arg("only").split(",")) : null;
const concurrency = Number(arg("concurrency", "4"));
const realizer = path.join(repoRoot, "build", "core", process.platform === "win32" ? "fs-realize.exe" : "fs-realize");
const planOnly = process.argv.includes("--plan-only");
const realizeOnly = process.argv.includes("--realize-only");
const backend = realizeOnly ? null : backendFor(selectionFromEnv({
  provider: process.argv.includes("--provider") ? arg("provider") : undefined,
  model: process.argv.includes("--model") ? arg("model") : undefined,
}));
if (!planOnly && !existsSync(realizer)) throw new Error(`realizer not built at ${realizer}; run npm run build:core`);
mkdirSync(outDir, { recursive: true });

function seedFor(id: string): number {
  let h = 2166136261;
  for (const ch of id) h = Math.imul(h ^ ch.charCodeAt(0), 16777619) >>> 0;
  return h;
}

const queue = prompts.filter((p) => !only || only.has(p.id));
interface Result { id: string; ok: boolean; latencyMs?: number; remainingErrors?: string[]; [k: string]: unknown }
const summary: Result[] = [];

// Nearest-rank percentile; null for no samples.
function percentile(values: number[], p: number): number | null {
  if (values.length === 0) return null;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.ceil((p / 100) * sorted.length) - 1)]!;
}

function stats(results: Result[]) {
  const planned = results.filter((r) => r.ok && r.latencyMs !== undefined);
  const latencies = planned.map((r) => r.latencyMs!);
  return {
    prompts: results.length,
    planned: planned.length,
    // Valid: planned with no validation errors left after the repair passes.
    valid: planned.filter((r) => r.remainingErrors?.length === 0).length,
    validityRate: results.length ? planned.filter((r) => r.remainingErrors?.length === 0).length / results.length : 0,
    latencyP50Ms: percentile(latencies, 50),
    latencyP95Ms: percentile(latencies, 95),
  };
}

function realize(base: string, id: string) {
  execFileSync(realizer, [
    "--in", `${base}.score.json`, "--seed", String(seedFor(id)),
    "--out-mid", `${base}.mid`, "--out-notes", `${base}.notes.json`, "--out-report", `${base}.report.json`,
  ]);
}

async function worker() {
  for (let p = queue.shift(); p; p = queue.shift()) {
    const base = path.join(outDir, p.id);
    try {
      if (realizeOnly) {
        realize(base, p.id);
        summary.push({ id: p.id, ok: true, realizedOnly: true });
        console.log(`ok   ${p.id}  realized`);
        continue;
      }
      const result = await planScore(p, backend!);
      writeFileSync(`${base}.score.json`, JSON.stringify(result.score, null, 2));
      if (!planOnly) realize(base, p.id);
      summary.push({ id: p.id, ok: true, attempts: result.attempts, latencyMs: result.latencyMs, usage: result.usage, remainingErrors: result.validationErrors });
      console.log(`ok   ${p.id}  ${(result.latencyMs / 1000).toFixed(1)}s  attempts=${result.attempts}  errors=${result.validationErrors.length}`);
    } catch (err) {
      summary.push({ id: p.id, ok: false, error: String(err) });
      console.log(`FAIL ${p.id}  ${String(err).slice(0, 200)}`);
    }
  }
}

await Promise.all(Array.from({ length: concurrency }, worker));
const backendInfo = backend && { name: backend.name, provider: backend.provider, model: backend.model };
const summaryStats = realizeOnly ? undefined : stats(summary);
writeFileSync(path.join(outDir, realizeOnly ? "realize.json" : "run.json"),
  JSON.stringify({ backend: backendInfo, at: new Date().toISOString(), stats: summaryStats, results: summary }, null, 2));
if (summaryStats) {
  const sec = (ms: number | null) => (ms === null ? "-" : `${(ms / 1000).toFixed(1)}s`);
  console.log(`${backendInfo!.provider}/${backendInfo!.model}: valid ${summaryStats.valid}/${summaryStats.prompts}, p50 ${sec(summaryStats.latencyP50Ms)}, p95 ${sec(summaryStats.latencyP95Ms)}`);
}
