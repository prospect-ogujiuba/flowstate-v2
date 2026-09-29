# Roadmap

Four phases, each unlocked by a gate that producers can feel. Week ranges are estimates.

| Phase | Weeks | Gate to leave it |
| --- | --- | --- |
| 0. Salvage and spikes | 0–2 | **A:** IR plans win the blind A/B against v1, and drag-out works in 3 hosts |
| 1. Core loop | 3–8 | **B:** 5 producers install cold and commit a clip in their first session |
| 2. Depth | 9–12 | **C:** committed clips per session and 4-week retention hit target |
| 3. Public beta | 13–16 | — |

Decided (2026-09-28): **bring-your-own-key ships from day one** as a first-class provider mode alongside managed access. It's what development runs on. A release flag (`byok` in the service's feature config, mirrored in the plugin's Settings) can hide it before launch and turn it back on later without code changes.

Provider layer (decided 2026-09-28): the agent service uses **`@earendil-works/pi-ai`**, Pi's standalone multi-provider package (OpenAI, OpenRouter, Anthropic, Google and more), for both managed and BYOK access. It does not use Pi's coding agent or its runtime. That work is Phase 1.

Mac testing (decided 2026-09-28): CI builds the macOS artifacts; a trusted friend or tester runs the Logic and AU checklist until the MacBook is back.

Model backends in development: `claude-code` (headless Claude Code on the developer's own subscription; dev and evals only, never an end-user path) and `api` (Anthropic API with a key: the BYOK path).

## Phase 0 issues

Status: `todo`, `doing`, `done`, `blocked`.

### P0-1 Freeze v1 — `done`
- [x] Tag v1 `v1-final` at `806d8a4` (local tag; push when publishing).
- [x] v1's live-tools security issue is documented; v1 is not being shared, so no patch.

### P0-2 Score IR v0 — `done`
- [x] `docs/ir-spec.md` and `schema/src/score.ts`, with JSON Schema generation.
- [x] Resolved realizer details folded into the spec ("Resolved details").

### P0-3 `core` realizer v0 — `done`
Done: 37 test cases / 19k assertions, determinism and properties tested, warning-free; CI green on Linux, macOS and Windows (MSVC).

Acceptance: all roles and tokens in the spec realized. Deterministic. Properties tested (clip length, range, monophony). Voice-led chords. `fs-realize` CLI emits `.mid`, notes JSON and a report. Builds warning-free on Linux; CI on macOS and Windows.

### P0-4 Planner v0 — `done` (quality tuning continues under P0-6)
- [x] Prompt + controls -> IR through Claude (`claude-opus-5`, adaptive thinking, high effort), with Zod and semantic validation plus up to two repair passes.
- [x] Smoke test: valid 4-bar dorian score on the first attempt; 63 s latency at high effort (latency is tuned in Phase 1, target under 8 s).
- Note: constrained decoding (`output_config.format`) is rejected for this schema ("compiled grammar is too large"), so the planner validates on our side instead.

### P0-5 v1 baseline and A/B tooling — `done`
Done: v1 baseline 20/20 prompts. The pack builder rewrites clips with neutral track names and channels, so options can't be told apart by metadata. The key check uses core's unjustified out-of-key count.

Acceptance: 20-prompt set within v1's supported controls. v1 output generated through v1's own compilers. Metrics script. Blind A/B pack builder and a scorer with a sign test.

### P0-6 Spike 1: blind A/B, LLM-written IR vs v1 — `doing` (waiting on listeners)
Round 1 (2026-09-28): 20/20 prompts planned on the `claude-code` backend, all valid on the first attempt; latency p50 about 62 s (range 41–107 s). All realized with 0 unjustified out-of-key notes. Pack `evals/ab/packs/phase0-r1.zip` is built; the key is in `phase0-r1.key.json`, kept separate. Score with `npm run -w evals ab:score -- <sheets...> --key ab/packs/phase0-r1.key.json --v2-dir out/v2`.

| Mean per prompt | v1 | v2 |
| --- | --- | --- |
| chord onsets / bar | 3.8 | 12.0 |
| bass notes / bar | 1.9 | 4.3 |
| melody notes / bar | 2.0 | 3.8 |
| drum hits / bar | 5.0 | 19.2 |
| drum ghost share | 0 | 0.27 |
| bar repetition | 0.22 | 0.02 |
| chord voice movement (semitones, summed) | 2.4 | 8.9 (watch: richer voicings and more changes; listening decides) |

1. `npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/v2`
2. `npm run -w evals metrics -- out/v2` and `... out/v1`: compare.
3. Build the pack, then at least 2 producers listen blind in their DAW and fill the score sheet.
4. **Gate A (music half):** v2 is preferred in ≥ 70% of non-tie comparisons, sign test p < 0.05, and no regression in out-of-key share. If it fails: tune the prompt, IR and realizer and rerun once; if it fails again, rethink the engine before Phase 1.

### P0-7 Spike 2: WebView UI inside real hosts — `doing` (builds ready; needs DAW testing)
Status 2026-09-29: `plugin/spike` builds in CI on the first run (macOS universal, Windows x64). The scheduler tests and a VST3 host smoke test pass on both, and `auval` passes for the AU instrument and the AU MIDI FX. Artifacts: the latest `plugin-spike` run on GitHub Actions (`flowstate-spike-macos-universal`, `flowstate-spike-windows-x64`). Next: run the host checklists in Ableton and FL/Bitwig on Windows, and in Logic on a friend's Mac; record results in `docs/spikes/results.md`.

Brief: `docs/spikes/webview-host.md`. Gate A (host half): focus, space-bar pass-through, resize and drag-out of a `.mid` from the WebView work in Ableton, Logic and one of FL or Bitwig.

### P0-8 Spike 3: MIDI out and transport-locked audition — `doing` (builds ready; needs DAW testing)
Same builds as P0-7 (the instrument and MIDI FX variants).

Brief: `docs/spikes/midi-out.md`. Proves an instrument build with MIDI out and an AU MIDI FX build can play a realized clip in time with the host transport, looped, in Ableton and Logic.

### P0-9 CI on every push — `done`
Acceptance: GitHub Actions matrix (Linux, macOS, Windows) builds and tests `core` and typechecks TS on each push and PR.
