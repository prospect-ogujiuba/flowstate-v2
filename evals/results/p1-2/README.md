# P1-2 results: round 1, streamed parts and fast models

Evidence for P1-2 (latency), snapshotted from `evals/out/p1-2/` on 2026-10-01. Each folder holds the planner's score IRs (`*.score.json`), the raw replies per attempt (`*.replies.txt`), core realizations (`*.mid`, `*.notes.json`, `*.report.json`), `run.json` and `metrics.json`.

All runs use the Phase 0 prompt unchanged, at concurrency 4. They also use two planner changes from this round:
- **Streamed parts:** each part is reported as soon as the head and that part pass validation, while the reply is still being written.
- **Parts-only repair:** when the head is sound, a repair turn asks only for the broken or missing parts.

Times are measured from the start of the request: first answer text, first playable part, full plan including repairs.

| Folder | Backend / model | Reasoning | Valid | First text p50 | First part p50 / p95 | Full plan p50 / p95 | Repairs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `gemini-3.6-flash-minimal/` | OpenRouter `google/gemini-3.6-flash` | minimal | 20/20 | 0.8 s | 5.1 s / 7.6 s | 10.2 s / 17.3 s | 8 |
| `gemini-3.6-flash-low/` | OpenRouter `google/gemini-3.6-flash` | low | 19/20 | 5.2 s | 9.0 s / 14.4 s | 13.8 s / 20.2 s | 3 |
| `sonnet-low/` | claude-code `sonnet` | low (effort) | 20/20 | 16.1 s | 20.5 s / 27.1 s | 24.4 s / 35.8 s | 2 |
| (P1-1, for reference) | OpenRouter `google/gemini-3.1-pro-preview` | high | 20/20 | — | — | 84.1 s / 145.9 s | 4 |
| (Phase 0 r1, for reference) | claude-code `opus` | high | 20/20 | — | — | 61.0 s / 93.8 s | 0 |

The one failure at `low` (`ambient-pentatonic-drift`) was a provider error (`finish_reason: error`), not bad output.

Metric means over all prompts (from `metrics.json`; symbolic measures, not a quality verdict):

| Run | chords/bar | melody/bar | pcEntropy | onsetEntropy | voiceMove | ghostShare | outOfKey |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Gemini 3.6 Flash minimal | 11.06 | 3.60 | 2.78 | 2.31 | 8.61 | 0.16 | 0.06 |
| Gemini 3.6 Flash low | 11.28 | 4.05 | 2.81 | 2.34 | 8.13 | 0.15 | 0.04 |
| Sonnet low | 12.07 | 3.52 | 2.82 | 2.40 | 8.77 | 0.26 | 0.05 |
| P1-1 GPT-5.5 high | 11.60 | 5.75 | 2.87 | 2.79 | 9.77 | 0.28 | 0.05 |
| P1-1 Gemini 3.1 Pro high | 8.04 | 3.69 | 2.74 | 2.29 | 8.44 | 0.17 | 0.06 |

What the numbers say:
- **Thinking was the cost.** Gemini 3.6 Flash at minimal writes about 2.2k output tokens per plan (median), against 11–20k at high in P1-1, and its first answer text lands in under a second.
- **The head gates the first part.** About 4 s pass between the first text and the first playable part: form, harmony and the verbose motif notes come before any part, and are most of the score's bytes.
- **Repairs gate the full plan.** 8 of 20 minimal-effort plans needed one. The first-attempt errors are mostly step counts (a 32-step bar for 16, 6 for 12 in 6/8, 15 for 16) and `-` in drum lanes, plus some hard-constraint misses.
- **Some providers ignore the effort setting.** DeepSeek `deepseek-flash` thought for 70–105 s even at minimal (smoke runs on 2 prompts, not kept), so it is no use for the latency path.
- Anthropic API runs could not be made: the account has no credit.

Whether the fast plans sound good enough is for blind listening, not these metrics.
