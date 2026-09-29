# Phase 0 results

Evidence for Gate A (music half), snapshotted from the git-ignored working folders `evals/out/` and `evals/ab/packs/`.

| Folder | What |
| --- | --- |
| `v1/` | v1 baseline: output of v1's own compilers for `evals/prompts/phase0.json`, plus `metrics.json` and `failures.json` |
| `v2-r1/` | v2 round 1: the planner's score IRs (`*.score.json`), core realizations (`*.mid`, `*.notes.json`, `*.report.json`), `run.json` (latency and attempts per prompt), `metrics.json` |
| `ab-r1/pack/` | The blind pack as listened to, with the owner's filled `scoresheet.csv` (the neosoul row's swapped scores corrected on the owner's confirmation) |
| `ab-r1/key.json` | The answer key, published now that listening is done |
| `ab-r1/verdict.md` | Scorer output: v2 preferred 20/20, p = 1.9e-6, Gate A music half PASS |

Host-spike evidence (Gate A, host half) is in `docs/spikes/results.md`.

Re-score from the repo root:

```sh
npm run -w evals ab:score -- results/phase0/ab-r1/pack/scoresheet.csv --key results/phase0/ab-r1/key.json --v2-dir results/phase0/v2-r1
```
