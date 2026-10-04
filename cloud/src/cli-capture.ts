// Runs the capture prompt set (evals/prompts/capture-phase1.json, built by `npm run -w evals capture:build`): each
// case is the PlanRequest the plugin sends for "Use what I just played" (P1-20). It goes through the service's own
// mapping (plannerRequest, keptDraft) and the planner, and each score is realized with core's fs-realize.
// Usage: tsx src/cli-capture.ts --prompts ../evals/prompts/capture-phase1.json --out ../evals/out/capture
//          [--only id1,id2] [--concurrency 4] [--provider deepseek --model deepseek-flash] [--reasoning off] [--plan-only]
// run.json records per case: valid; kept (harmonize, add bass, add drums: the riff comes back exactly as played);
// lanes (every lane asked for has a part, and no new part repeats a kept riff's lane); attempts, latency and the
// first streamed part. <id>.replies.txt holds the raw replies.
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { isDeepStrictEqual } from "node:util";
import { PlanRequest, type Score } from "@flowstate/schema";
import { backendFor, selectionFromEnv, type Reasoning } from "./backends.ts";
import { keepsReference, keptDraft, planScore } from "./planner.ts";
import { plannerRequest } from "./service.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.join(here, "..", "..");

function arg(name: string, fallback?: string): string {
  const i = process.argv.indexOf(`--${name}`);
  const value = i >= 0 ? process.argv[i + 1] : fallback;
  if (value === undefined) throw new Error(`missing --${name}`);
  return value;
}
const has = (name: string) => process.argv.includes(`--${name}`);

interface CaptureCase { id: string; riff: string; riffRole: string; keyFrom: string; request: unknown }

const cases: CaptureCase[] = JSON.parse(readFileSync(path.resolve(arg("prompts")), "utf8"));
const outDir = path.resolve(arg("out"));
const only = has("only") ? new Set(arg("only").split(",")) : null;
const concurrency = Number(arg("concurrency", "4"));
const planOnly = has("plan-only");
const realizer = path.join(repoRoot, "build", "core", process.platform === "win32" ? "fs-realize.exe" : "fs-realize");
const selection = selectionFromEnv({
  provider: has("provider") ? arg("provider") : undefined,
  model: has("model") ? arg("model") : undefined,
  reasoning: has("reasoning") ? (arg("reasoning") as Reasoning) : undefined,
});
const backend = backendFor(selection);
if (!planOnly && !existsSync(realizer)) throw new Error(`realizer not built at ${realizer}; run npm run build:core`);
mkdirSync(outDir, { recursive: true });

const percentile = (values: number[], p: number) => {
  if (values.length === 0) return null;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.ceil((p / 100) * sorted.length) - 1)]!;
};

interface Result { id: string; intent: string; ok: boolean; [k: string]: unknown }
const results: Result[] = [];
const queue = cases.filter((c) => !only || only.has(c.id));

async function worker() {
  for (let c = queue.shift(); c; c = queue.shift()) {
    const body = PlanRequest.parse(c.request);
    const ref = body.reference!;
    const file = path.join(outDir, c.id);
    try {
      const planned = plannerRequest(body);
      keptDraft(planned); // the service's checks before it streams
      const r = await planScore(planned, backend);
      writeFileSync(`${file}.replies.txt`, r.replies.map((t, i) => `===== attempt ${i + 1} =====\n${t}\n`).join("\n"));
      writeFileSync(`${file}.score.json`, JSON.stringify(r.score, null, 2));
      if (!planOnly)
        execFileSync(realizer, ["--in", `${file}.score.json`, "--seed", "1", "--out-mid", `${file}.mid`, "--out-notes", `${file}.notes.json`, "--out-report", `${file}.report.json`]);
      const kept = !keepsReference(ref.intent) || ref.score.parts.every((p) => r.score.parts.some((q) => isDeepStrictEqual(p, q)));
      const keptIds = keepsReference(ref.intent) ? new Set(ref.score.parts.map((p) => p.id)) : new Set<string>();
      const fresh = (r.score as Score).parts.filter((p) => !keptIds.has(p.id));
      const lanes = planned.controls.lanes.every((l) => fresh.some((p) => p.role === l)) &&
        (!keepsReference(ref.intent) || fresh.every((p) => p.role !== c.riffRole));
      const valid = r.validationErrors.length === 0;
      results.push({
        id: c.id, intent: ref.intent, riff: c.riff, riffRole: c.riffRole, keyFrom: c.keyFrom, ok: true, valid, kept, lanes,
        roles: (r.score as Score).parts.map((p) => p.role), attempts: r.attempts, latencyMs: r.latencyMs, firstPartMs: r.firstPartMs,
        remainingErrors: r.validationErrors, usage: r.usage,
      });
      console.log(`ok   ${c.id.padEnd(24)} ${(r.latencyMs / 1000).toFixed(1)}s  attempts=${r.attempts}  valid=${valid} kept=${kept} lanes=${lanes}  ${(r.score as Score).parts.map((p) => p.role).join(",")}`);
    } catch (err) {
      results.push({ id: c.id, intent: ref.intent, ok: false, error: String(err) });
      console.log(`FAIL ${c.id.padEnd(24)} ${String(err).slice(0, 200)}`);
    }
  }
}

await Promise.all(Array.from({ length: concurrency }, worker));
const done = results.filter((r) => r.ok);
const latency = done.map((r) => r.latencyMs as number);
const firstPart = done.flatMap((r) => (typeof r.firstPartMs === "number" ? [r.firstPartMs] : []));
const stats = {
  cases: results.length,
  valid: done.filter((r) => r.valid).length,
  // Valid, the riff kept where the intent keeps it, and every lane written.
  correct: done.filter((r) => r.valid && r.kept && r.lanes).length,
  firstAttempt: done.filter((r) => r.valid && r.attempts === 1).length,
  latencyP50Ms: percentile(latency, 50),
  latencyP95Ms: percentile(latency, 95),
  firstPartP50Ms: percentile(firstPart, 50),
  firstPartP95Ms: percentile(firstPart, 95),
};
writeFileSync(path.join(outDir, "run.json"), JSON.stringify({
  backend: { name: backend.name, provider: backend.provider, model: backend.model, reasoning: selection.reasoning ?? "high" },
  prompts: path.relative(repoRoot, path.resolve(arg("prompts"))), at: new Date().toISOString(), stats,
  results: results.sort((a, b) => a.id.localeCompare(b.id)),
}, null, 2));
const sec = (ms: number | null) => (ms === null ? "-" : `${(ms / 1000).toFixed(1)}s`);
console.log(`${backend.provider}/${backend.model}: valid ${stats.valid}/${stats.cases}, correct ${stats.correct}, first attempt ${stats.firstAttempt}, ` +
  `p50 ${sec(stats.latencyP50Ms)}, p95 ${sec(stats.latencyP95Ms)}, first part p50 ${sec(stats.firstPartP50Ms)}`);
