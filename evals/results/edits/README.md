# Edit results (P1-19)

The edit set (`evals/prompts/edits-phase1.json`, 20 cases) run against the Phase 0 scores in `p1-18/agent-deepseek-2` (20/20 valid). Thinking off. Command:

```sh
npm run -w cloud edit -- --prompts ../evals/prompts/edits-phase1.json --bases ../evals/results/p1-18/agent-deepseek-2 \
  --out ../evals/results/edits/<run> --provider deepseek --model deepseek-flash --reasoning off
```

| Run | Valid | Targeted | No repair | Full edit p50 / p95 |
| --- | --- | --- | --- | --- |
| `deepseek-1` (DeepSeek Flash) | 20/20 | 20 | 15 | 3.3 / 6.7 s |
| `deepseek-2` (DeepSeek Flash) | 19/20 | 19 | 15 | 3.5 / 9.8 s |
| `lite31-1` (Gemini 3.1 Flash Lite) | 20/20 | 20 | 19 | 2.3 / 7.7 s |

**Targeted** means every role the case expects was changed (for the question, a text-only answer). Locked parts are never changed: the service enforces it.

The first streamed part lands close to the end, since most edits change one part.

The one invalid edit (`jazz-walking-bass`) had a bar of 7 steps where 8 were expected after three attempts.

The repairs are the IR slips plans make too: wrong step counts, and pitch tokens where the IR has none. Two are new to edits:
- Darker-chord edits put flats (`b3`) in bass rhythms, in all three runs.
- New counter and arp parts put scale degrees in rhythm strings.

Each case's raw replies are in `<id>.replies.txt`, and the edited scores in `<id>.score.json`, realized to `.mid` with seed 1.
