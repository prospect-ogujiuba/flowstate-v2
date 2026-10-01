# CLAUDE.md

Flowstate v2: an AI co-writer that lives on a MIDI track. The model composes a score IR; `core` performs it; the plugin plays it in time with the DAW.

Design source: `docs/proposal.md`, the v2 proposal as a living document. The other docs in `docs/` are the working specs. When they disagree with the proposal, the specs win, and the proposal gets a dated **Update** note at that spot.

**Current state:** Phase 0 is closed (Gate A passed). Phase 1 (core loop) is next. `docs/roadmap.md` is the source of truth for status, ordering and acceptance criteria.

## Layout

| Path | What | Language |
| --- | --- | --- |
| `core/` | Music engine: IR parse, realizer, MIDI export, `fs-realize` CLI. No JUCE, no I/O beyond files in the CLI. | C++20 |
| `schema/` | Contracts: score IR and bridge schema (Zod source of truth), generated JSON Schemas and the C++ bridge header (`schema/cpp`) | TS / generated C++ |
| `cloud/` | Agent service. Today: the planner (prompt -> IR) and its model backends. | TS |
| `evals/` | Prompt sets, v1 baseline, metrics, blind A/B packs; committed evidence in `evals/results/` | TS |
| `plugin/` | JUCE 9 plugin: instrument and MIDI FX variants, processor-owned session, bridge host, host sync, capture. `plugin/spike/` is the frozen Phase 0 spike. See `plugin/README.md`. | C++ |
| `ui/` | WebView UI bundled into the plugin. Today a placeholder that proves the bridge; the Studio (P1-9) replaces it. | JS (TS from P1-8) |
| `docs/` | product, architecture, ir-spec, threading, roadmap, spikes | Markdown |

v1 lives in the sibling repo `../flowstate` (frozen at tag `v1-final`). Use it as reference only: its visual identity and `assets/` for the design system (P1-8), and its compilers for the baseline. Don't port its code wholesale, and don't edit it.

## Rules

- Do not add `Co-Authored-By` trailers to commits, especially Claude attribution.
- Work from an issue in `docs/roadmap.md`; each has acceptance criteria. Update its status there when you finish. No per-task planning documents in the repo.
- The IR is the contract. Change `schema/src/score.ts` and `docs/ir-spec.md` together, then run `npm run schema` to regenerate the JSON Schema.
- The bridge (WebView <-> plugin <-> service messages) is the other contract. Change `schema/src/bridge.ts` and `docs/bridge-spec.md` together, add fixtures in `schema/fixtures/bridge/`, and run `npm run schema`. Never hand-edit `schema/cpp/include/flowstate/bridge.h`; CI fails on drift.
- Theory lives in `core` only. Never re-implement voicing, scale or timing logic in TS or in the plugin.
- `core` is deterministic: same IR + seed -> identical bytes. Seeded RNG only; no `std::random` distributions.
- Model calls live in `cloud/` only, behind the `Backend` interface in `cloud/src/backends.ts`. Current backends: `claude-code` (headless Claude Code on the developer's subscription, for dev and evals only, never an end-user path) and `api` (Anthropic SDK, the BYOK path). From P1-1, managed and BYOK access go through `@earendil-works/pi-ai`; no Pi coding-agent packages. Every backend must surface refusals and truncated output (e.g. `refusal`, `max_tokens`) as errors.
- Audio thread: no allocation, locks or I/O. See `docs/threading.md`.
- No secrets in the repo, DAW state or logs. BYOK keys go in the OS keychain.
- Tests assert behaviour, not copy or pixel geometry.
- Musical quality is decided by `evals/` (metrics + blind listening by the owner), not by opinion in review.

## Setup

Full machine setup: `docs/dev-setup.md`. Plugin build, tests, pluginval and DAW checks: `docs/testing-plugin.md`.

Needs Node 22+, CMake 3.22+, Ninja and a C++20 compiler. On a machine without Ninja: `uv tool install ninja` (or the system package).

```sh
npm install
npm run build:core && npm run test:core     # C++ engine
npm run build:bridge && npm run test:bridge # generated C++ bridge types, round-trip tests
npm run typecheck                            # all TS packages
npm test                                     # core + bridge tests + workspace tests (schema, evals)
npm run schema                               # regenerate JSON Schemas and the C++ bridge header
```

Planner and evals (the `claude-code` backend is the default; set `FLOWSTATE_PLANNER_BACKEND=api` and `ANTHROPIC_API_KEY` for the API):

```sh
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/v2
npm run -w evals metrics -- out/v2
```

The plugin fetches JUCE 9.0.2 and builds on Linux headless (no WebView) for local tests; the WebView builds, pluginval and auval run in CI (`.github/workflows/plugin.yml`) on macOS and Windows:

```sh
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/plugin
ctest --test-dir build/plugin --output-on-failure
```

CI builds and testers (need `gh`, logged in; details in `docs/testing-plugin.md` sections 5 and 7):

```sh
npm run ci:status [-- --watch]               # recent runs on this branch; --watch follows the plugin run
npm run fetch:build -- windows               # latest green Windows build (+ install.ps1, gallery.ps1) into Windows Downloads on WSL
npm run pack:mac                             # macOS tester zip in dist/builds: bundles, install.sh, README with the build ID
```
