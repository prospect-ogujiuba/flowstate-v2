# Listening pack: fast path against a careful plan (P1-2)

The two sides of the blind pack `p1-2-fast-vs-careful` (built with `--seed 1001`), realized with today's `core`.

- `a-deepseek-flash-off/` (the pack's "v2" side): DeepSeek `deepseek-flash` with thinking off, `round7/deepseek-1` (first part p50 2.3 s, full plan 4.2 s).
- `b-gpt-5.5-high/` (the pack's "v1" side): OpenRouter `openai/gpt-5.5` at reasoning high, the P1-1 scores (`p1-1/openrouter-gpt-5.5`, full plan p50 135 s), realized again with today's `core` so both sides use the same engine.

The question: does the fast path cost quality? Symbolic hints before listening: the careful side is rhythmically busier (onset entropy 2.79 vs 2.27, ghost share 0.28 vs 0.14) and repeats less (0.06 vs 0.15).

Rebuild the pack: `npm run -w evals ab -- --a results/p1-2/pack-fast-vs-careful/a-deepseek-flash-off --b results/p1-2/pack-fast-vs-careful/b-gpt-5.5-high --name p1-2-fast-vs-careful --seed 1001`

Score a returned sheet: `npm run -w evals ab:score -- --key ab/packs/p1-2-fast-vs-careful.key.json <sheet.csv> --v2-dir results/p1-2/pack-fast-vs-careful/a-deepseek-flash-off`

## Result (owner, 2026-10-01)

`scoresheet-owner.csv`, scored against `key.json` (published now that the sheet is in):

| | Fast (DeepSeek Flash, off) | Careful (GPT-5.5, high) |
| --- | --- | --- |
| Preferred | 7 | 11 (1 tie, 1 skipped) |
| Mean musicality / fits prompt (1–5) | 2.74 / 2.74 | 2.89 / 3.00 |
| Scored 4 or more on musicality | 5 | 6 |

- No clear difference: sign test p = 0.48 two-sided. One listener and 19 judgements can't show the two are equal, only that the fast path does not lose by much.
- Both sides score low in absolute terms (about 2.8 of 5). The lowest prompts are the groove-defined styles (afrobeats 1–2, gospel 1–3, funk 1–2), and the notes say the drums are what is missing there.
- The owner listened through a quick stock setup, so sound selection may hide differences; see the roadmap note on rendered listening.
