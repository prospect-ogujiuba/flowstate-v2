// Joins filled A/B scoresheets with the hidden key and reports v2's win rate, sign test and score deltas.
// Usage: tsx src/score-ab.ts --key ab/packs/<name>.key.json <sheet.csv> [more sheets...]
//          [--metrics-v2 out/v2/metrics.json --metrics-v1 out/v1/metrics.json] [--ook-tolerance 0]
// Each sheet is one listener (listener name = file name without .csv).
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "./common.ts";
import { signTest } from "./stats.ts";

export function parseCsv(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [], field = "", quoted = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (quoted) {
      if (ch === '"' && text[i + 1] === '"') { field += '"'; i++; }
      else if (ch === '"') quoted = false;
      else field += ch;
    } else if (ch === '"') quoted = true;
    else if (ch === ",") { row.push(field); field = ""; }
    else if (ch === "\n" || ch === "\r") {
      if (ch === "\r" && text[i + 1] === "\n") i++;
      row.push(field); field = "";
      if (row.some((f) => f.trim() !== "")) rows.push(row);
      row = [];
    } else field += ch;
  }
  row.push(field);
  if (row.some((f) => f.trim() !== "")) rows.push(row);
  return rows;
}

interface Judgement { listener: string; promptId: string; outcome: "v2" | "v1" | "tie"; mus: [number | null, number | null]; fit: [number | null, number | null] }

function score(v: string | undefined): number | null {
  const n = Number((v ?? "").trim());
  return (v ?? "").trim() !== "" && Number.isFinite(n) && n >= 1 && n <= 5 ? n : null;
}

export function readSheet(file: string, v2OptionById: Map<string, 1 | 2>): { judgements: Judgement[]; problems: string[] } {
  const listener = path.basename(file).replace(/\.csv$/i, "");
  const rows = parseCsv(readFileSync(file, "utf8"));
  const header = (rows.shift() ?? []).map((h) => h.trim().toLowerCase());
  const col = (name: string) => header.indexOf(name);
  const need = ["prompt_id", "preferred", "musicality_1", "musicality_2", "fits_prompt_1", "fits_prompt_2"];
  const missing = need.filter((n) => col(n) < 0);
  if (missing.length) throw new Error(`${file}: missing columns ${missing.join(", ")}`);
  const judgements: Judgement[] = [], problems: string[] = [];
  for (const r of rows) {
    const id = (r[col("prompt_id")] ?? "").trim();
    const pref = (r[col("preferred")] ?? "").trim().toLowerCase();
    if (!id) continue;
    const v2Option = v2OptionById.get(id);
    if (!v2Option) { problems.push(`${listener}: ${id} not in key`); continue; }
    if (!pref) { problems.push(`${listener}: ${id} has no preference (skipped)`); continue; }
    if (!["1", "2", "tie"].includes(pref)) { problems.push(`${listener}: ${id} preferred='${pref}' (expected 1/2/tie)`); continue; }
    const outcome = pref === "tie" ? "tie" : Number(pref) === v2Option ? "v2" : "v1";
    const m1 = score(r[col("musicality_1")]), m2 = score(r[col("musicality_2")]);
    const f1 = score(r[col("fits_prompt_1")]), f2 = score(r[col("fits_prompt_2")]);
    // reorder scores to [v2, v1]
    const mus: [number | null, number | null] = v2Option === 1 ? [m1, m2] : [m2, m1];
    const fit: [number | null, number | null] = v2Option === 1 ? [f1, f2] : [f2, f1];
    judgements.push({ listener, promptId: id, outcome, mus, fit });
  }
  return { judgements, problems };
}

export function summarize(js: Judgement[]) {
  const wins = js.filter((j) => j.outcome === "v2").length;
  const losses = js.filter((j) => j.outcome === "v1").length;
  const ties = js.length - wins - losses;
  const n = wins + losses;
  const deltas = (pick: (j: Judgement) => [number | null, number | null]) => {
    const d = js.map(pick).filter((p): p is [number, number] => p[0] !== null && p[1] !== null).map(([a, b]) => a - b);
    return { n: d.length, mean: d.length ? d.reduce((a, b) => a + b, 0) / d.length : null };
  };
  return { judgements: js.length, wins, losses, ties, winRate: n ? wins / n : null, ...signTestSafe(wins, n), musicalityDelta: deltas((j) => j.mus), fitsPromptDelta: deltas((j) => j.fit) };
}
function signTestSafe(wins: number, n: number) { const t = signTest(wins, n); return { pTwoSided: t.twoSided, pOneSided: t.oneSidedGreater }; }

const pct = (x: number | null) => (x === null ? "–" : `${(x * 100).toFixed(1)}%`);
const num = (x: number | null) => (x === null ? "–" : (x >= 0 ? "+" : "") + x.toFixed(2));

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = parseArgs(process.argv.slice(2));
  const keyFile = args.flags.key;
  if (!keyFile || !args._.length) { console.error("usage: tsx src/score-ab.ts --key <pack>.key.json <sheet.csv> [...]"); process.exit(2); }
  const key = JSON.parse(readFileSync(path.resolve(keyFile), "utf8")) as { pack: string; entries: { promptId: string; v2Option: 1 | 2 }[] };
  const v2OptionById = new Map(key.entries.map((e) => [e.promptId, e.v2Option]));
  const all: Judgement[] = [];
  const problems: string[] = [];
  for (const sheet of args._) { const r = readSheet(path.resolve(sheet), v2OptionById); all.push(...r.judgements); problems.push(...r.problems); }
  const listeners = [...new Set(all.map((j) => j.listener))];

  console.log(`# A/B results: ${key.pack}\n`);
  console.log("| listener | judged | v2 wins | v1 wins | ties | v2 win rate (non-tie) | sign test p (2-sided) | Δ musicality (v2−v1) | Δ fits prompt (v2−v1) |");
  console.log("|---|---:|---:|---:|---:|---:|---:|---:|---:|");
  const line = (label: string, s: ReturnType<typeof summarize>) =>
    console.log(`| ${label} | ${s.judgements} | ${s.wins} | ${s.losses} | ${s.ties} | ${pct(s.winRate)} | ${s.pTwoSided.toPrecision(3)} | ${num(s.musicalityDelta.mean)} | ${num(s.fitsPromptDelta.mean)} |`);
  for (const l of listeners) line(l, summarize(all.filter((j) => j.listener === l)));
  const pooled = summarize(all);
  line("**pooled**", pooled);
  console.log(`\nOne-sided p (v2 better than chance): ${pooled.pOneSided.toPrecision(3)}`);

  // Gate A (see evals/README.md)
  const checks: [string, boolean | null][] = [
    [`>= 2 listeners (have ${listeners.length})`, listeners.length >= 2],
    [`v2 wins >= 70% of non-tie comparisons (${pct(pooled.winRate)})`, pooled.winRate !== null && pooled.winRate >= 0.7],
    [`sign test p < 0.05, two-sided (${pooled.pTwoSided.toPrecision(3)})`, pooled.pTwoSided < 0.05],
  ];
  if (args.flags["metrics-v2"] && args.flags["metrics-v1"]) {
    const ook = (f: string) => (JSON.parse(readFileSync(path.resolve(f), "utf8")) as { aggregate: { outOfKey: number | null } }).aggregate.outOfKey;
    const a = ook(args.flags["metrics-v2"]), b = ook(args.flags["metrics-v1"]);
    const tol = Number(args.flags["ook-tolerance"] ?? "0");
    checks.push([`no out-of-key regression: v2 ${pct(a)} <= v1 ${pct(b)} + ${pct(tol)}`, a !== null && b !== null && a <= b + tol]);
  } else checks.push(["no out-of-key regression (pass --metrics-v2/--metrics-v1 to check)", null]);
  console.log("\n## Gate A\n");
  for (const [label, ok] of checks) console.log(`- [${ok === null ? "?" : ok ? "x" : " "}] ${label}`);
  const verdict = checks.every(([, ok]) => ok === true) ? "PASS" : checks.some(([, ok]) => ok === false) ? "FAIL" : "INCOMPLETE";
  console.log(`\nGate A: **${verdict}**`);
  if (problems.length) console.log(`\nSheet problems:\n${problems.map((p) => `- ${p}`).join("\n")}`);
}
