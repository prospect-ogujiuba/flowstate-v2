// Plans every prompt in a prompts file and realizes each score with core's fs-realize.
// Usage: tsx src/cli-plan.ts --prompts ../evals/prompts/phase0.json --out ../evals/out/v2 [--only id1,id2] [--concurrency 4] [--plan-only | --realize-only]
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { planScore, plannerBackend, type PlanRequest } from "./planner.ts";

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
if (!planOnly && !existsSync(realizer)) throw new Error(`realizer not built at ${realizer}; run npm run build:core`);
mkdirSync(outDir, { recursive: true });

function seedFor(id: string): number {
  let h = 2166136261;
  for (const ch of id) h = Math.imul(h ^ ch.charCodeAt(0), 16777619) >>> 0;
  return h;
}

const queue = prompts.filter((p) => !only || only.has(p.id));
const summary: Record<string, unknown>[] = [];

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
      const result = await planScore(p);
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
writeFileSync(path.join(outDir, realizeOnly ? "realize.json" : "run.json"), JSON.stringify({ backend: plannerBackend, at: new Date().toISOString(), results: summary }, null, 2));
