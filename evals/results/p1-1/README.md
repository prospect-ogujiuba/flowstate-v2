# P1-1 results: the Phase 0 set on several providers

Evidence for P1-1 (multi-provider layer on pi-ai), snapshotted from `evals/out/p1-1/` on 2026-10-01. Each folder holds the planner's score IRs (`*.score.json`), core realizations (`*.mid`, `*.notes.json`, `*.report.json`), `run.json` (backend, per-prompt latency and attempts, summary stats) and `metrics.json`.

All runs: `FLOWSTATE_PLANNER_BACKEND=pi`, reasoning `high`, concurrency 4, the three runs in parallel, managed keys from the environment.

| Folder | Provider / model | Planned | Valid | p50 | p95 | Repair passes needed |
| --- | --- | --- | --- | --- | --- | --- |
| `deepseek-v4-pro/` | DeepSeek `deepseek-v4-pro` | 20/20 | 20/20 | 171.5 s | 271.5 s | 2 prompts |
| `openrouter-gpt-5.5/` | OpenRouter `openai/gpt-5.5` | 8/20 | 8/8 planned | 112.5 s | 138.1 s | 0 |
| `openrouter-gemini-3.1-pro/` | OpenRouter `google/gemini-3.1-pro-preview` | 9/20 | 9/9 planned | 63.9 s | 124.5 s | 1 prompt |
| (Phase 0 round 1, for reference) | `claude-code` Opus | 20/20 | 20/20 | 61.0 s | 93.8 s | 0 |

**The OpenRouter runs are incomplete.** All 23 missing prompts failed with OpenRouter's `402` ("This request would exceed your available credits given your current in-flight requests"). The account ran out of credit partway through, with both OpenRouter runs sharing it. No prompt failed on output quality: every score these models returned passed Zod and semantic validation, and every one realized. The latency figures cover the planned prompts only. Their metric means are over different subsets of prompts, so they aren't comparable with each other or with DeepSeek's.

Metric means (from `metrics.json`; symbolic measures, not a quality verdict, which comes from blind listening):

| Run | chords/bar | melody/bar | pcEntropy | onsetEntropy | voiceMove | ghostShare | outOfKey |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DeepSeek (20) | 9.30 | 3.66 | 2.78 | 2.16 | 10.36 | 0.12 | 0.05 |
| GPT-5.5 (8) | 12.88 | 5.81 | 3.00 | 3.01 | 8.72 | 0.20 | 0.07 |
| Gemini 3.1 Pro (9) | 9.54 | 4.42 | 2.87 | 2.53 | 7.04 | 0.09 | 0.06 |
| Phase 0 r1, Opus (20) | 12.02 | 3.76 | 2.84 | 2.52 | 8.88 | 0.27 | 0.06 |

Latency on every provider is far from the P1-2 targets (first AI part under 3 s, full plan under 8 s). DeepSeek is valid on everything but the slowest.

## Completing the OpenRouter runs

After topping up OpenRouter credit, re-run each model on its own (so they don't reserve credit against each other), from `cloud/`:

```sh
FLOWSTATE_PLANNER_BACKEND=pi npx tsx src/cli-plan.ts --prompts ../evals/prompts/phase0.json \
  --out ../evals/out/p1-1/openrouter-gpt-5.5 --provider openrouter --model openai/gpt-5.5
FLOWSTATE_PLANNER_BACKEND=pi npx tsx src/cli-plan.ts --prompts ../evals/prompts/phase0.json \
  --out ../evals/out/p1-1/openrouter-gemini-3.1-pro --provider openrouter --model google/gemini-3.1-pro-preview
npm run -w evals metrics -- out/p1-1/openrouter-gpt-5.5
npm run -w evals metrics -- out/p1-1/openrouter-gemini-3.1-pro
```

Then copy both folders over the ones here and update the tables.
