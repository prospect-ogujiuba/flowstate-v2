// Runs an edit prompt set (evals/prompts/edits-*.json) against base scores and realizes each result with core's
// fs-realize. Usage:
//   tsx src/cli-edit.ts --prompts ../evals/prompts/edits-phase1.json --bases ../evals/results/p1-18/agent-deepseek-1 \
//     --out ../evals/out/edits [--only id1,id2] [--concurrency 4] [--provider deepseek --model deepseek-flash] [--reasoning off] [--plan-only]
// Each case names its base score (a Phase 0 prompt id), the kind, the prompt, the roles it may change ("change";
// null = every part) and the roles it should change ("expect"). run.json records validity, latency, the first
// streamed part, and "targeted": every expected role was changed (for a question: answered with text only).
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type { Score } from "@flowstate/schema";
import { backendFor, selectionFromEnv, type Reasoning } from "./backends.ts";
import { editScore, type EditKind } from "./editor.ts";
import type { Role } from "./part-stream.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.join(here, "..", "..");

function arg(name: string, fallback?: string): string {
  const i = process.argv.indexOf(`--${name}`);
  const value = i >= 0 ? process.argv[i + 1] : fallback;
  if (value === undefined) throw new Error(`missing --${name}`);
  return value;
}
const has = (name: string) => process.argv.includes(`--${name}`);

interface EditCase {
  id: string;
  base: string;
  kind: EditKind;
  prompt: string;
  role?: Role;
  change: Role[] | null;
  expect: Role[];
}

const cases: EditCase[] = JSON.parse(readFileSync(path.resolve(arg("prompts")), "utf8"));
const bases = path.resolve(arg("bases"));
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

interface Result { id: string; kind: EditKind; ok: boolean; [k: string]: unknown }
const results: Result[] = [];
const queue = cases.filter((c) => !only || only.has(c.id));

async function worker() {
  for (let c = queue.shift(); c; c = queue.shift()) {
    const base: Score = JSON.parse(readFileSync(path.join(bases, `${c.base}.score.json`), "utf8"));
    const partIds = c.change === null ? null : base.parts.filter((p) => c.change!.includes(p.role)).map((p) => p.id);
    const file = path.join(outDir, c.id);
    try {
      const r = await editScore({ kind: c.kind, prompt: c.prompt, score: base, partIds, role: c.role ?? null }, backend);
      writeFileSync(`${file}.replies.txt`, r.replies.map((t, i) => `===== attempt ${i + 1} =====\n${t}\n`).join("\n"));
      const roleOf = (id: string) => [...base.parts, ...(r.score?.parts ?? [])].find((p) => p.id === id)?.role;
      const touched = [...r.changed, ...r.removed].map(roleOf);
      const targeted = c.expect.length === 0 ? r.score === null : c.expect.every((role) => touched.includes(role));
      if (r.score) {
        writeFileSync(`${file}.score.json`, JSON.stringify(r.score, null, 2));
        if (!planOnly)
          execFileSync(realizer, ["--in", `${file}.score.json`, "--seed", "1", "--out-mid", `${file}.mid`, "--out-notes", `${file}.notes.json`, "--out-report", `${file}.report.json`]);
      }
      results.push({
        id: c.id, kind: c.kind, ok: true, valid: r.validationErrors.length === 0, targeted, attempts: r.attempts,
        latencyMs: r.latencyMs, firstPartMs: r.firstPartMs, changed: r.changed, removed: r.removed, message: r.message,
        remainingErrors: r.validationErrors, usage: r.usage,
      });
      console.log(`ok   ${c.id.padEnd(24)} ${(r.latencyMs / 1000).toFixed(1)}s  attempts=${r.attempts}  changed=${[...r.changed, ...r.removed].join(",") || "-"}  targeted=${targeted}`);
    } catch (err) {
      results.push({ id: c.id, kind: c.kind, ok: false, error: String(err) });
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
  targeted: done.filter((r) => r.valid && r.targeted).length,
  firstAttempt: done.filter((r) => r.valid && r.attempts === 1).length,
  latencyP50Ms: percentile(latency, 50),
  latencyP95Ms: percentile(latency, 95),
  firstPartP50Ms: percentile(firstPart, 50),
  firstPartP95Ms: percentile(firstPart, 95),
};
writeFileSync(path.join(outDir, "run.json"), JSON.stringify({
  backend: { name: backend.name, provider: backend.provider, model: backend.model, reasoning: selection.reasoning ?? "high" },
  bases: path.relative(repoRoot, bases), at: new Date().toISOString(), stats, results: results.sort((a, b) => a.id.localeCompare(b.id)),
}, null, 2));
const sec = (ms: number | null) => (ms === null ? "-" : `${(ms / 1000).toFixed(1)}s`);
console.log(`${backend.provider}/${backend.model}: valid ${stats.valid}/${stats.cases}, targeted ${stats.targeted}, first attempt ${stats.firstAttempt}, ` +
  `p50 ${sec(stats.latencyP50Ms)}, p95 ${sec(stats.latencyP95Ms)}, first part p50 ${sec(stats.firstPartP50Ms)}`);
