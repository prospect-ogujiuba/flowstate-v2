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

## Round 2: compact motif strings (2026-10-01)

Motif notes are now written as a compact string (`"5:.75! 4:.25 b3:1 r:.5 1+:1/3"`, see `docs/ir-spec.md`) instead of note objects, so the head is smaller. Same prompt set and planner; the IR spec in the prompt now teaches the string form.

| Folder | Backend / model | Valid | First part p50 / p95 | Full plan p50 / p95 | Repairs | Head (median) | Motifs (median) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `gemini-3.6-flash-minimal/` (round 1) | OpenRouter `google/gemini-3.6-flash`, minimal | 20/20 | 5.1 s / 7.6 s | 10.2 s / 17.3 s | 8 | 1056 B | 384 B, 5 notes |
| `gemini-3.6-flash-minimal-motifs/` | same | 20/20 | 4.4 s / 6.9 s | 10.4 s / 14.5 s | 11 | 710 B | 98 B, 10 tokens |
| `sonnet-low/` (round 1) | claude-code `sonnet`, low | 20/20 | 20.5 s / 27.1 s | 24.4 s / 35.8 s | 2 | 1209 B | 597 B |
| `sonnet-low-motifs/` | same | 20/20 | 16.6 s / 21.7 s | 22.3 s / 29.7 s | 8 | 814 B | 101 B |

- Every motif (27/27 on Gemini, 37/37 on Sonnet) was written in the string form, and no repair came from a motif token.
- Gemini's repairs are the same mechanical slips as round 1 (step counts, `-` in drum lanes, enum values, constraint misses); 8 against 11 is within run-to-run noise.
- Sonnet's repairs rose from 2 to 8. Two were broken JSON (mismatched brackets; a JavaScript `.replace(...)` call inside a string), two were step counts or `-`, and three were a drums block that left out the required `notes: null` key. That omission happened once in round 1 too (1 of 153 blocks, against 3 of 131 now). Overloading the name `notes` (motif string vs block literal notes) may play a part, but the sample is small. Letting block fields be omitted would remove this class of error.
- Bytes before the first part fell from 1.47 to 1.08 KB, and the first part came 0.7 s sooner. Motifs got longer (10 tokens against 5 notes) while their bytes fell to a quarter.
- Metrics stay in range. Gemini: chords/bar 11.31, melody/bar 3.54, pcEntropy 2.78, onsetEntropy 2.36, voiceMove 8.36, ghostShare 0.18, outOfKey 0.07. Melodies are a little more stepwise (step share 0.61 → 0.67) and less repetitive (0.25 → 0.18). Sonnet: chords/bar 10.69, melody/bar 3.71, pcEntropy 2.77, onsetEntropy 2.36, voiceMove 9.25, ghostShare 0.21, outOfKey 0.05, step share 0.60 → 0.66, repetition 0.13 → 0.09.

## Round 3: block fields may be left out (2026-10-01)

Block fields that don't apply to a part's role may now be left out instead of written as `null` (IR change, additive: nulls are still accepted; core treats both the same, and a test checks the MIDI is byte-identical). The prompt now says to leave them out. Motif strings from round 2 stay.

| Folder | Backend / model | Valid | First part p50 / p95 | Full plan p50 / p95 | Repairs | Score (median) | Before first part |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `gemini-3.6-flash-minimal-motifs/` (round 2) | OpenRouter `google/gemini-3.6-flash`, minimal | 20/20 | 4.4 s / 6.3 s | 10.4 s / 14.5 s | 11 | 2968 B | 1079 B |
| `gemini-3.6-flash-minimal-sparse/` | same | 20/20 | 3.8 s / 6.2 s | **7.3 s** / 13.4 s | 4 | 2225 B | 996 B |
| `sonnet-low-motifs/` (round 2) | claude-code `sonnet`, low | 20/20 | 16.6 s / 21.7 s | 22.3 s / 29.7 s | 8 | 3167 B | 1208 B |
| `sonnet-low-sparse/` | same | 20/20 | 15.9 s / 23.1 s | 20.1 s / 31.7 s | 5 | 2042 B | 988 B |

- Both models left out every unused field (0 nulls per block, from about 7). Scores shrank by a quarter to a third.
- Gemini Flash minimal meets the full-plan target (7.3 s p50 against 8 s). Fewer bytes also meant fewer repairs on both models (11 → 4, 8 → 5), though the samples are small.
- The first part moved less (996 B before it): the head is now most of that, and harmony is its largest piece (344 B median for 8 chords on Gemini).
- Metrics stay in range. Gemini: chords/bar 10.30, melody/bar 3.20, pcEntropy 2.87, onsetEntropy 2.29, voiceMove 8.41, ghostShare 0.16, outOfKey 0.08, step share 0.57, repetition 0.15. Sonnet: chords/bar 11.46, melody/bar 3.54, pcEntropy 2.78, onsetEntropy 2.36, voiceMove 8.96, ghostShare 0.24, outOfKey 0.05, step share 0.60, repetition 0.15.

## Round 4: compact harmony strings, and the lane rule (2026-10-01)

Harmony is now written as a compact string (`"Dm9:4 | G13:4 | Em7:2 A7#9:2"`, see `docs/ir-spec.md`); the chord-object array is still accepted. The planner's request now says "one part per lane, with the lane name as its role": before, on pad-friendly styles the model wrote the chords lane as a `pad` or `arp` part, which counted as a missing lane and cost a repair turn. That was the largest first-attempt error class (10 of 60 plans).

Single runs vary too much to compare these changes (Gemini's repairs ranged 4–12 of 20 between runs), so `round4/` holds three runs of each variant with the lane rule, at reasoning minimal on Gemini 3.6 Flash. `gemini-sparse-*` uses the round 3 IR (chord objects), `gemini-harmony-*` the harmony string. They are plan-only (scores, replies and `run.json`, no MIDI), and all six ran at once, which loads the provider more than a single run.

| Variant (3 runs pooled) | Valid | First part p50 / p95 | First parts under 3 s | Full plan p50 / p95 | Repairs per run |
| --- | --- | --- | --- | --- | --- |
| Chord objects (`gemini-sparse-*`) | 60/60 | 3.84 s / 10.0 s | 9 of 60 | 7.55 s / 13.6 s | 5, 7, 7 |
| Harmony string (`gemini-harmony-*`) | 60/60 | 3.52 s / 10.1 s | 18 of 60 | 7.81 s / 15.2 s | 8, 5, 7 |

- The harmony string cuts the bytes before the first part from about 1000 to about 730 (median) and moves the first part 0.3 s sooner, with twice as many first parts under 3 s. The full plan is the same within noise.
- Without the lane rule, three runs of the harmony string had more counting errors than the chord objects (step counts 11 against 4, form length 8 against 3), and 32 repairs against 18. With the rule, repairs are level. Whether implied chord positions make bar counting harder stays open; worth watching in P1-3.
- `round4/sonnet-low-harmony/` (claude-code Sonnet, low, harmony string, before the lane rule; one run): 20/20, first part p50 7.4 s, full 13.8 s, 5 repairs. Sonnet's first text came at 4.3 s against 12.8 s in round 3, so its thinking time varies a lot between runs; read it as one sample.

## Round 5: the request fixes the context, and states the bar arithmetic (2026-10-01)

Two planner changes, no IR change:
- **Context from the request.** The model is told the session fixes the key, meter, tempo and length, and writes only `swing` and `style` in `context`. The planner fills the fixed fields from the request before validating, and overrides any it wrote anyway, so a wrong key or length no longer costs a repair. The assembled score is still complete IR.
- **Bar arithmetic.** The request now states the step count per bar for the meter ("one bar = 6 beats, so every step-string bar has 6 × grid steps (grid 2: 12, grid 3: 18, grid 4: 24)"). Without it, the 6/8 gospel prompt failed in 2 of 3 runs once the model no longer wrote `meterNumerator` itself.

`round5/` holds three plan-only runs of each, on Gemini 3.6 Flash at minimal (three at a time):

| Variant (3 runs pooled) | Valid | First part p50 / p95 | First parts under 3 s | Full plan p50 / p95 | Repairs per run |
| --- | --- | --- | --- | --- | --- |
| Round 4 harmony string (`round4/gemini-harmony-*`) | 60/60 | 3.52 s / 10.1 s | 18 of 60 | 7.81 s / 15.2 s | 8, 5, 7 |
| Context from the request (`gemini-context-*`) | 58/60 | 3.37 s / 9.9 s | 25 of 58 | 7.76 s / 14.7 s | 8, 8, 8 |
| + bar arithmetic (`gemini-context-meter-*`) | 60/60 | **3.20 s** / 10.4 s | 28 of 60 | **7.27 s** / 14.8 s | 6, 7, 5 |

- The model wrote only `swing` and `style` in 59 of 60 contexts; context fell from 129 to 40 B (median).
- The two failures without the arithmetic line were both `gospel-68-sunday` (6/8), with step counts still wrong after two repairs. With the line, every plan is valid.

## Round 6: other fast models (2026-10-01)

Same planner as round 5; one plan-only run each through OpenRouter (`round6/`):

| Folder | Model, reasoning | Valid | First part p50 / p95 | Full plan p50 / p95 | Repairs |
| --- | --- | --- | --- | --- | --- |
| `gemini-3.1-flash-lite-minimal/` | `google/gemini-3.1-flash-lite`, minimal | 18/20 | **2.0 s** | **3.8 s** / 8.3 s | 8 |
| `gemini-3.5-flash-lite-minimal/` | `google/gemini-3.5-flash-lite`, minimal | 16/20 | 2.2 s | 5.9 s / 9.0 s | 15 |
| `gpt-mini-latest-low/` | `~openai/gpt-mini-latest`, low (its minimal is unsupported) | 20/20 | 10.6 s | 14.4 s / 18.1 s | — |

- The Flash Lite models are about twice as fast as Gemini 3.6 Flash, but less reliable. Every plan still invalid after two repairs had step-count slips: a bar written at another resolution (8 or 32 steps where 16 were expected), or stray `-` or `r` tokens.
- GPT mini at low is valid but too slow for the targets.
