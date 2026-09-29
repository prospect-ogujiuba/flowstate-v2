# CLAUDE.md

Flowstate v2: an AI co-writer that lives on a MIDI track. The model composes a score IR; `core` performs it; the plugin plays it in time with the DAW.

Design source: the v2 proposal doc (https://claude.ai/code/artifact/9ac041de-641a-4525-9047-ff12e23de0c1). Repo docs in `docs/` are the working specs; when they disagree with the proposal, the repo docs win and the proposal gets a comment.

**Current state:** Phase 0 is closed (Gate A passed). Phase 1 (core loop) is next. `docs/roadmap.md` is the source of truth for status, ordering and acceptance criteria.

## Layout

| Path | What | Language |
| --- | --- | --- |
| `core/` | Music engine: IR parse, realizer, MIDI export, `fs-realize` CLI. No JUCE, no I/O beyond files in the CLI. | C++20 |
| `schema/` | Score IR schema (Zod source of truth, generated JSON Schema) | TS |
| `cloud/` | Agent service. Today: the planner (prompt -> IR) and its model backends. | TS |
| `evals/` | Prompt sets, v1 baseline, metrics, blind A/B packs; committed evidence in `evals/results/` | TS |
| `plugin/spike/` | Phase 0 JUCE spike (WebView UI, drag-out, MIDI audition). Phase 1 builds `plugin/` and `ui/` from its proven pieces. | C++ / JS |
| `docs/` | product, architecture, ir-spec, threading, roadmap, spikes | Markdown |

v1 lives in the sibling repo `../flowstate` (frozen at tag `v1-final`). Use it as reference only: its visual identity and `assets/` for the design system (P1-8), and its compilers for the baseline. Don't port its code wholesale, and don't edit it.

## Rules

- Do not add `Co-Authored-By` trailers to commits, especially Claude attribution.
- Work from an issue in `docs/roadmap.md`; each has acceptance criteria. Update its status there when you finish. No per-task planning documents in the repo.
- The IR is the contract. Change `schema/src/score.ts` and `docs/ir-spec.md` together, then run `npm run schema` to regenerate the JSON Schema.
- Theory lives in `core` only. Never re-implement voicing, scale or timing logic in TS or in the plugin.
- `core` is deterministic: same IR + seed -> identical bytes. Seeded RNG only; no `std::random` distributions.
- Model calls live in `cloud/` only, behind the `Backend` interface in `cloud/src/backends.ts`. Current backends: `claude-code` (headless Claude Code on the developer's subscription, for dev and evals only, never an end-user path) and `api` (Anthropic SDK, the BYOK path). From P1-1, managed and BYOK access go through `@earendil-works/pi-ai`; no Pi coding-agent packages. Every backend must surface refusals and truncated output (e.g. `refusal`, `max_tokens`) as errors.
- Audio thread: no allocation, locks or I/O. See `docs/threading.md`.
- No secrets in the repo, DAW state or logs. BYOK keys go in the OS keychain.
- Tests assert behaviour, not copy or pixel geometry.
- Musical quality is decided by `evals/` (metrics + blind listening by the owner), not by opinion in review.

## Setup

Needs Node 22+, CMake 3.22+, Ninja and a C++20 compiler. On a machine without Ninja: `uv tool install ninja` (or the system package).

```sh
npm install
npm run build:core && npm run test:core     # C++ engine
npm run typecheck                            # all TS packages
npm test                                     # core tests + workspace tests (evals)
npm run schema                               # regenerate schema/score.v0.schema.json
```

Planner and evals (the `claude-code` backend is the default; set `FLOWSTATE_PLANNER_BACKEND=api` and `ANTHROPIC_API_KEY` for the API):

```sh
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/v2
npm run -w evals metrics -- out/v2
```

The plugin spike needs a JUCE checkout and builds in CI (`.github/workflows/plugin-spike.yml`) for macOS and Windows; see `plugin/spike/README.md`.
