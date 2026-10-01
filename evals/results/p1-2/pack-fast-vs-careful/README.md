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

## Result, rendered (owner, 2026-10-01)

The same pack again through `npm run -w evals render` and `player.html` (`scoresheet-owner-rendered.csv`):

| | First listen (stock DAW setup) | Rendered |
| --- | --- | --- |
| Preferred, fast / careful / tie | 7 / 11 / 1 | 6 / 11 / 3 (p = 0.33) |
| Musicality, fast / careful | 2.74 / 2.89 | 3.05 / 3.21 |
| Fits prompt, fast / careful | 2.74 / 3.00 | 3.32 / 3.53 |
| Scores of 2 or less (of 38) | 15 | 10 |

- Sound selection was hiding quality: both sides scored about 0.3 higher on musicality and 0.55 on fit, most where the instrument carries the style (lofi 3.5 → 5.0, cinematic lament 3.5 → 4.5).
- The fast-vs-careful gap is unchanged (about 0.15, not significant).
- 5 of 19 prompts flipped winner between the two listens, so single-prompt preferences are noisy; read totals, and prefer two runs per side for decisions.
- Still low with good sounds: afrobeats, drill, trap choir, funk, drum and bass, the groove-defined styles P1-3's grooves target.
