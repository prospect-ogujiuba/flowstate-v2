# P1-3 results

## Grooves (2026-10-01)

Named drum grooves in `core` (docs/ir-spec.md, Grooves), measured on Gemini 3.1 Flash Lite with thinking off through OpenRouter. DeepSeek's chat API was not answering at the time (the balance endpoint was fine), so the groove runs use the other fast model, against its own round 7 run as the no-groove baseline.

- `pack-grooves/a-grooves/`: the Phase 0 set with grooves in the spec (19/20 valid; first part p50 1.9 s, full plan 3.4 s). 12 of 20 plans used a groove, fitting the style: afrobeats → `afrobeats`, reggaeton → `dembow`, gospel → `gospel_shuffle`, lofi → `lofi`, neo-soul → `neo_soul`, house → `four_on_floor`, tech house → `tech_house`, funk → `funk`, rock → `rock`, pop → `pop`, drum and bass → `dnb`, jazz trio → `brush_swing`. Trap, drill, cinematic and the waltzes wrote their own lanes.
- `grooves-lite31-2/`: a second run (20/20 valid, 9 of 20 used a groove).
- `pack-grooves/b-no-grooves/`: the same model before grooves (`p1-2/round7/lite31-1`), realized with today's `core`.

Blind pack `p1-3-grooves` (seed 1003; a = grooves, the pack's "v2" side), rendered with `npm run -w evals render`.

Score a returned sheet: `npm run -w evals ab:score -- --key ab/packs/p1-3-grooves.key.json <sheet.csv> --v2-dir results/p1-3/pack-grooves/a-grooves`
