# evals

Phase 0 tooling for the blind A/B test: v2 (an LLM writes a score IR, then `core` realizes it) against v1 (the old deterministic JS lane compilers).

## Scripts

Run these from `evals/`, or with `npm run -w evals <script>` from the repo root.

| Script | What it does |
| --- | --- |
| `npm run baseline:v1` | Runs v1's `fallback*Plan` + `compile*Plan` for every prompt in `prompts/phase0.json`, in the order chords → bass → melody → drums. Bass and melody get the chords' `harmonicContext`. It writes `out/v1/<id>.notes.json` (the same format `core` emits), `out/v1/<id>.mid` (SMF type 1, 960 PPQ, drums on channel 10), `out/v1/<id>.v1.json` (the v1 decisions and adherence evidence) and `out/v1/failures.json`. The v1 modules are loaded read-only from `../flowstate` (override with `--v1-bin` or `FLOWSTATE_V1_BIN`). |
| `npm run metrics -- <dir> [--prompts prompts/phase0.json]` | Computes symbolic metrics for every `*.notes.json` in `<dir>`. It prints a markdown table and writes `<dir>/metrics.json`. `npm run metrics:v1` is a shortcut for `out/v1`. |
| `npm run ab -- --a out/v2 --b out/v1 --name <pack> --seed <n>` | Builds `ab/packs/<pack>/`. Each prompt gets a folder `NN-<id>/` holding `option-1.mid`, `option-2.mid` (in seeded random order) and `prompt.txt`. The pack also gets `scoresheet.csv` and `README.txt`. The hidden key is written to `ab/packs/<pack>.key.json`, outside the pack folder. |
| `npm run ab:score -- --key ab/packs/<pack>.key.json sheet-a.csv sheet-b.csv [--metrics-v2 out/v2/metrics.json --metrics-v1 out/v1/metrics.json]` | Joins the filled sheets (one per listener) with the key. It prints v2's win rate, an exact binomial sign test and the mean score deltas (v2 − v1), per listener and pooled, then a Gate A checklist. |
| `npm test` | Runs node:test tests for the SMF writer and the sign test. |

v2 output comes from `cloud` (`npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/v2`). It writes the same `<id>.mid` / `<id>.notes.json` pairs.

### Metrics

- **Per part:** notes per bar, distinct pitches, pitch-class entropy (bits), onset entropy (onset position within the bar at 16th resolution, in bits) and a repetition score.
  - The repetition score is the share of non-empty bars whose (onset, pitch) content is identical to another bar's. Onsets are snapped to a 1/12-beat grid, so humanization doesn't hide repeated bars.
- **Melody and bass:** the share of steps of 2 semitones or less, the share of leaps over 7 semitones, and the mean interval.
- **Chords:** average voice movement between consecutive onsets. For each note in the new chord, take its smallest semitone distance to any note in the previous chord, then sum.
- **Drums:** velocity range, and ghost share (hits at velocity 55 or below).
- **Per file:** out-of-key share. This is the share of pitched notes outside the prompt's tonic/mode. The prompt is matched by the filename id.

## Phase 0 A/B procedure

1. Generate both sides with the same prompts file: `npm run baseline:v1` and the `cloud` plan run into `out/v2`.
2. Run `npm run metrics -- out/v1` and `npm run metrics -- out/v2`. Sanity-check the v2 output for failures and empty parts before anyone listens.
3. Build one pack with `npm run ab -- --a out/v2 --b out/v1 --name phase0-r1 --seed <n>`. Give listeners only the `ab/packs/phase0-r1/` folder, never the `.key.json`.
4. Each listener (at least 2 producers) works through the pack's `README.txt`:
   - Import both options into the same instrument setup at the prompt's tempo.
   - Loop them and switch back and forth.
   - Fill `scoresheet.csv` without looking at the key, and return it as `scoresheet-<name>.csv`.
5. Score the sheets with `npm run ab:score -- --key ab/packs/phase0-r1.key.json <sheets...> --metrics-v2 out/v2/metrics.json --metrics-v1 out/v1/metrics.json`.

## Gate A decision

Gate A passes only if all four conditions hold:

- At least 2 listeners returned sheets.
- v2 wins at least 70% of the non-tie comparisons, pooled across listeners.
- The exact two-sided sign test on the pooled non-tie comparisons gives p < 0.05. Ties are excluded.
- There is no out-of-key regression: v2's mean out-of-key share is no higher than v1's (`--ook-tolerance` can loosen this).
  - v2 can use chromatic chord symbols on purpose. If v2 fails only on this check, look at the realization report before overriding the result.

`score-ab` prints PASS, FAIL or INCOMPLETE. INCOMPLETE means the metrics files were not passed.

Pooling treats each listener × prompt judgement as independent. With 2–3 listeners on 20 prompts, also check that each listener's direction agrees with the pooled result.

## v1 baseline fairness notes

- The baseline uses v1's production defaults. These are the balanced expressive context, density/energy/complexity 0, chords compiler v2, maxEvents 16 (32 for drums), and no kick context.
  - `genreHint` is sent (the style tags) but has no effect: v1's compilers ignore it.
  - Tempo does not change the notes (it only scales humanize-timing jitter).
- v1's provider schema let the model choose a few fields: chords `progressionStrategy` and `inversionTendency`, and melody `phraseLengthBeats`.
  - The baseline pins them to the fallback values.
  - One exception: when the fallback melody (phrase length 4) exceeds the 16-event budget (8-bar clips and 3/4 or 6/8), it retries with the schema-legal 8 and then 2. The deviation is recorded in `<id>.v1.json`.
- Every prompt stays inside v1's controls (modes major, minor, dorian, phrygian, mixolydian and pentatonic; meters 4/4, 3/4 and 6/8; 4 or 8 bars). That is why the prompt set has **no harmonic-minor prompt**: v1 cannot produce one.
- v1 chord events have no velocity. They are written at v1's plugin default of 96.
