# AGENTS.md

Flowstate v2: an AI co-writer that lives on a MIDI track. The model composes a score IR; `core` performs it; the plugin plays it in time with the DAW.

Design source: the v2 proposal doc (https://claude.ai/code/artifact/9ac041de-641a-4525-9047-ff12e23de0c1). Repo docs in `docs/` are the working specs; when they disagree with the proposal, the repo docs win and the proposal gets a comment.

## Layout

| Path | What | Language |
| --- | --- | --- |
| `core/` | Music engine: IR parse, realizer, MIDI export. No JUCE, no I/O beyond files in the CLI. | C++20 |
| `schema/` | Score IR schema (Zod source of truth, generated JSON Schema) | TS |
| `cloud/` | Agent service. Today: the planner (prompt -> IR). | TS |
| `evals/` | Prompt sets, v1 baseline, metrics, blind A/B packs | TS |
| `plugin/`, `ui/` | JUCE shell and WebView UI (Phase 1) | C++ / TS |
| `docs/` | product, architecture, ir-spec, threading, roadmap | Markdown |

## Rules

- Work from an issue in `docs/roadmap.md` (or GitHub Issues once the repo is published); each has acceptance criteria. No slice cascades, no per-task planning documents in the repo.
- The IR is the contract. Change `schema/src/score.ts` and `docs/ir-spec.md` together, then regenerate the JSON Schema with `npm run schema`.
- Theory lives in `core` only. Never re-implement voicing, scale or timing logic in TS or in the plugin.
- `core` is deterministic: same IR + seed -> identical bytes. Seeded RNG only; no `std::random` distributions.
- Model calls live in `cloud/` only, use the Anthropic TypeScript SDK, and handle `refusal` and `max_tokens` stop reasons.
- Audio thread: no allocation, locks or I/O. See `docs/threading.md`.
- No secrets in the repo, DAW state or logs.
- Tests assert behaviour, not copy or pixel geometry.
- Musical quality is decided by `evals/` (metrics + blind listening), not by opinion in review.

## Commands

```sh
npm install
npm run build:core && npm run test:core     # C++ engine
npm run typecheck                            # all TS packages
npm run schema                               # regenerate schema/score.v0.schema.json
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/v2   # needs ANTHROPIC_API_KEY
```
