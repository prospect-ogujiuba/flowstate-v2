# Capture listening (P1-20), owner, 2026-10-27

Pack `capture-r1`: the 21 cases of `deepseek-flash-1`, each the riff alone and then Flowstate's result (`npm run -w evals capture:pack`, rendered with `render`). Not blind. Sheet: `capture-r1-owner.csv`. Scored with `npm run -w evals ab:score -- --key ab/packs/capture-r1.key.json <sheet>`.

**Result: fits the riff in 17 of 21 (81.0%), PASS** (P1-20 asks for at least 4 of 5). Musicality of the results: mean 3.0 of 5.

| Intent | Fits |
| --- | --- |
| add bass | 4 of 5 |
| add drums | 4 of 4 |
| continue | 3 of 4 |
| answer | 2 of 3 |
| harmonize | 4 of 5 |

The four that didn't fit, in the owner's words:
- `bass-continue` (3): "Was quite different from the original riff."
- `trap-add-bass` (1): "Sounds off a little."
- `jazz-harmonize` (1): "Felt off sync with the melody."
- `trap-answer` (2): "Garbage drums and ruined melody."

Fits, but weak (musicality 2):
- `afro-add-drums`: "Drums rhythms were definitely going for afrobeat but doesn't get the essentials and therefore sounds pretty whack."
- `house-continue`.

Other notes: `pop-hook-answer` "changed the melody quite a bit"; `boom-bap-add-bass` "pretty simple"; `dembow-harmonize` "drums on point, melody/chords/stabs very simple"; `lofi-continue` "chill as expected".

For P1-3 (quality): the trap riffs (both cases fail), the swung jazz melody's harmonization (timing against a played, off-grid melody), afrobeat drums missing the style's essentials, and continue/answer drifting from the riff. Chords and bass riffs did best (8 of 8 add a part that fits).
