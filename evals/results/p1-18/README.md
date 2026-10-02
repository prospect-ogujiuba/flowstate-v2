# P1-18 results

## Agent loop on Pi (2026-10-02)

The `pi` backend now runs each plan on a `pi-agent-core` `Agent`, with repair as its `finishTurn`, and the groove hint moved from the request into a style-pack prompt section. Phase 0 set, thinking off, plan only, concurrency 4. Runs of one kind ran side by side.

- `base-*`: the code before the change (HEAD `2a10f13`, on pi-ai 1.0), the same day. `base-deepseek-3/4` ran next to `agent-deepseek-3/4`.
- `agent-*`: the agent loop, no tools offered (the default).
- `tools-*`: the agent loop with `--tools` (`library_examples`, `analyze_clip`).

Pooled per kind (p50 / p95 in seconds; round 7 is `p1-2/round7`):

| Model | Runs | Valid | Full plan | First part |
| --- | --- | --- | --- | --- |
| DeepSeek `deepseek-flash` | round 7 (2) | 40/40 | 4.2 / 7.0 | 2.3 / 5.4 |
| | base (4) | 78/80 | 3.8 / 7.6 | 2.3 / 6.7 |
| | agent (4) | 78/80 | 3.5 / 7.0 | 2.3 / 5.7 |
| | tools (1) | 19/20 | 5.9 / 12.5 | 3.8 / 9.7 |
| Gemini 3.1 Flash Lite (OpenRouter) | round 7 (2) | 39/40 | 3.6 / 7.7 | 2.0 / 5.8 |
| | base (2) | 40/40 | 3.3 / 6.5 | 2.1 / 4.3 |
| | agent (2) | 40/40 | 3.1 / 5.6 | 1.9 / 4.4 |
| | tools (1) | 20/20 | 3.1 / 5.5 | 1.9 / 4.5 |

- **No latency regression** against round 7 or the same-day baseline. Full plan and first part are level or a little faster on both models.
- **Validity:** the agent matches the same-day baseline (DeepSeek 78/80 each). Both are 2 short of round 7's 40/40. Every invalid plan, before and after the change, is a step-string bar of the wrong length after three attempts (`gospel-68-sunday`'s 6/8 chords, `funk-clav-mixolydian`'s bass). That is the model, not the loop.
- **Tools:** DeepSeek called `library_examples` in every plan (21 calls), which adds a round trip: full plan p50 +2.4 s, first part +1.5 s. Flash Lite never called a tool. So tools stay off by default; whether the examples improve the music is for a blind pack.
