# Edit follow-ups with history (P1-19)

`evals/prompts/edits-followups.json`: 12 two-step edits. The second step points back and names no part: "too much, pull it back a bit", "now do the same to the chords", "keep only the last one".

Each case runs the first step on a Phase 0 score from `p1-18/agent-deepseek-2`. It then runs the follow-up twice from the same result: with the history (`EditRequest.history`) and without it. Thinking off, DeepSeek Flash and Gemini 3.1 Flash Lite, two runs each per setup.

**Targeted** means the expected part changed. **Precise** means only the expected part changed. A harmony change counts as the chords, and a motif change counts as the parts that play it. Without history the model often rewrites every part, which "targets" trivially, so precise is the measure that matters.

| Setup (runs) | With history: precise | Without history: precise |
| --- | --- | --- |
| History as prompts and notes (`deepseek-*`, `lite31-*`) | 31/46 (67%) | 12/45 |
| + the rule "a follow-up that points back is about what that step changed" (`scope-*`) | 32/46 (70%) | 11/46 |
| + each step's changed parts in the history (`parts-*`), shipped | 36/46 (78%) | 12/47 |

Validity is unchanged: 44–46 of 46 with history. Cases that didn't complete are first steps that stayed invalid.

The changed parts fixed the cases where the follow-up's scope was misread:
- "keep only the last one" after hi-hat rolls: 1/4 → 3/4. It had been read as "keep only the last phrase".
- "even more" sparser chords: 1/4 → 3/4.

`gospel-bigger` ("make it bigger" after a drum fill) stays at 0. "It" is genuinely ambiguous, and every model grows the whole arrangement. Locking parts in the plugin is the precise tool there.

Each run's `run.json` has, per case, both sides with their touched roles, changed parts, harmony and motif changes, and the model's notes.
