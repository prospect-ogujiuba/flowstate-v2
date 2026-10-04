# Capture results (P1-20, "Use what I just played")

The capture set (`evals/prompts/capture-phase1.json`, 21 cases over 14 riffs) on the production route: DeepSeek Flash, thinking off. Each case is the `PlanRequest` the plugin sends, and it runs through the service's own mapping (`plannerRequest`, `keptDraft`). Commands:

```sh
npm run build:core && npm run -w evals capture:build   # riffs -> core's analyzer -> the prompt set
npm run -w cloud capture -- --prompts ../evals/prompts/capture-phase1.json \
  --out ../evals/results/p1-20/<run> --provider deepseek --model deepseek-flash --reasoning off
```

| Run | Valid | Correct | No repair | Full plan p50 / p95 | First part p50 |
| --- | --- | --- | --- | --- | --- |
| `deepseek-flash-1` | 21/21 | 21 | 18 | 2.6 / 6.1 s | 1.2 s |
| `deepseek-flash-2` | 21/21 | 21 | 16 | 3.3 / 7.9 s | 1.4 s |

**Correct** means valid, the riff came back exactly as played where the intent keeps it (harmonize, add bass, add drums), and every lane asked for was written. Per intent, both runs: add bass 5/5, add drums 4/4, harmonize 5/5, continue 4/4, answer 3/3.

The riffs:
- Played library clips (GodFlow): four chord clips and two bass lines.
- Riffs written in `evals/src/capture-set.ts` with human timing and dynamics (`evals/capture/riffs/*.mid`): five melodies (lofi, pop, afro, trap, jazz) and three beats (boom bap, house, dembow).

Each riff is read by `fs-analyze --literal`, as the plugin reads a capture. Core wasn't sure of the key for any of these short riffs, so each takes its session key, the way the plugin falls back to the current idea's key. Drums take the session key too.

What the first runs found (fixed before these runs):
- The service rejected a kept drum riff ("drums block has no drum lanes or groove"): a played beat is literal notes only. Every harmonize or add-bass on played drums failed, 36/42 valid over two runs. `cloud/src/validate.ts` now counts literal notes as drum content.
- Core ignored a given key for drums, so a played beat always planned in C major. It now carries the key it is given.
- The plugin fell back to the effective key when core wasn't sure, even when that key was only the default C major. It now prefers core's best guess over the default.

The repairs are the usual IR slips: step counts per bar in new drum parts, and drum parts split into one part per voice.

Each case's raw replies are in `<id>.replies.txt`, and the planned scores in `<id>.score.json`, realized to `.mid` with seed 1, riff included where the intent keeps it.
