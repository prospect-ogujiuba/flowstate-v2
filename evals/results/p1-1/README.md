# P1-1 results: the Phase 0 set on several providers

Evidence for P1-1 (multi-provider layer on pi-ai), snapshotted from `evals/out/p1-1/` on 2026-10-01. Each folder holds the planner's score IRs (`*.score.json`), core realizations (`*.mid`, `*.notes.json`, `*.report.json`), `run.json` (backend, per-prompt latency and attempts, summary stats) and `metrics.json`.

All runs: `FLOWSTATE_PLANNER_BACKEND=pi`, reasoning `high`, managed keys from the environment. The first pass ran all three models in parallel at concurrency 4. OpenRouter's credit ran out partway (`402`, "would exceed your available credits given your current in-flight requests"). After a top-up, only the 23 failed prompts were re-run, at concurrency 2 per model, and merged in (`run.json` lists them under `reruns`, and their results carry a `rerun` timestamp). No prompt failed on output quality.

| Folder | Provider / model | Valid | p50 | p95 | Prompts needing a repair pass |
| --- | --- | --- | --- | --- | --- |
| `deepseek-v4-pro/` | DeepSeek `deepseek-v4-pro` | 20/20 | 171.5 s | 271.5 s | 2 |
| `openrouter-gpt-5.5/` | OpenRouter `openai/gpt-5.5` | 20/20 | 135.0 s | 186.8 s | 0 |
| `openrouter-gemini-3.1-pro/` | OpenRouter `google/gemini-3.1-pro-preview` | 20/20 | 84.1 s | 145.9 s | 4 |
| (Phase 0 round 1, for reference) | `claude-code` Opus | 20/20 | 61.0 s | 93.8 s | 0 |

The OpenRouter latencies mix the two passes, and the load differed between them, so read them as rough figures. The two OpenRouter runs go through one provider to two model vendors (OpenAI, Google), so with DeepSeek this covers three model vendors.

Metric means over all 20 prompts (from `metrics.json`; symbolic measures, not a quality verdict, which comes from blind listening):

| Run | chords/bar | melody/bar | pcEntropy | onsetEntropy | voiceMove | ghostShare | outOfKey |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DeepSeek | 9.30 | 3.66 | 2.78 | 2.16 | 10.36 | 0.12 | 0.05 |
| GPT-5.5 | 11.60 | 5.75 | 2.87 | 2.79 | 9.77 | 0.28 | 0.05 |
| Gemini 3.1 Pro | 8.04 | 3.69 | 2.74 | 2.29 | 8.44 | 0.17 | 0.06 |
| Phase 0 r1, Opus | 12.02 | 3.76 | 2.84 | 2.52 | 8.88 | 0.27 | 0.06 |

Latency on every provider is far from the P1-2 targets (first AI part under 3 s, full plan under 8 s). DeepSeek is the slowest.
