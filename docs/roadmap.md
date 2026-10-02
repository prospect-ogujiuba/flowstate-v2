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

Model backends in development: `claude-code` (headless Claude Code on the developer's own subscription; dev and evals only, never an end-user path) and `pi` (any provider through pi-ai, with a managed or BYOK key; from P1-1).

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

### P0-6 Spike 1: blind A/B, LLM-written IR vs v1 — `done`: Gate A music half PASSED
Result (2026-09-29, owner as sole listener): v2 preferred in 20 of 20 comparisons, no ties, two-sided sign test p = 1.9e-6. Mean musicality 2.6 (v2) vs 1.1 (v1), delta +1.5; fits-prompt delta +1.6. (The neosoul-rhodes-swing row had its scores swapped on the sheet; the owner confirmed and it was corrected.)
**Caveat that shapes Phase 1:** v2 never scored above 3 out of 5. It beats v1 decisively but isn't yet good enough; quality work (prompting, IR expressiveness, realizer voicing and groove) continues alongside latency.

Round 1 (2026-09-28): 20/20 prompts planned on the `claude-code` backend, all valid on the first attempt; latency p50 about 62 s (range 41–107 s). All realized with 0 unjustified out-of-key notes. Evidence is committed in `evals/results/phase0/`: the v1 baseline, the v2 round 1 scores and realizations, the pack, the filled sheet, the key and the verdict (see its README for the re-score command).

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
3. Build the pack; the owner listens blind in their DAW and fills the score sheet. With one listener and 20 prompts, p < 0.05 means at least 15 v2 wins out of 20 non-tie comparisons.
4. **Gate A (music half):** v2 is preferred in ≥ 70% of non-tie comparisons, sign test p < 0.05, one listener (the owner), and zero unjustified out-of-key notes in the v2 realizations (per core's reports). If it fails: tune the prompt, IR and realizer and rerun once; if it fails again, rethink the engine before Phase 1.

### P0-7 Spike 2: WebView UI inside real hosts — `done` (Phase 0 closed 2026-09-29; the rest continues in P1-15)
Closed by the owner's decision: nothing waits on the Mac tester. Ableton (Windows) confirmed so far: it loads, the drag lands, and the WebView UI runs (WebView2). Checks 1–9 on Windows are not yet reported. Logic/macOS runs as the P1-16 hand-off, after CI has verified the Mac build.
Evidence: `docs/spikes/results.md`. Status 2026-09-29: `plugin/spike` builds in CI on the first run (macOS universal, Windows x64). The scheduler tests and a VST3 host smoke test pass on both, and `auval` passes for the AU instrument and the AU MIDI FX. The spike's artifacts have expired. The host checklists now run against the product plugin (`docs/testing-plugin.md`, section 6) in P1-15 and P1-16.

Brief: `docs/spikes/webview-host.md`. Gate A (host half): focus, space-bar pass-through, resize and drag-out of a `.mid` from the WebView work in Ableton, Logic and one of FL or Bitwig.

### P0-8 Spike 3: MIDI out and transport-locked audition — `done` (Ableton, Windows: routing, record and tempo ramp pass)
Findings carried into Phase 1: per-part drag handles and a multi-track "Drag all" (spike fixed, retest pending), and per-part MIDI output (P1-7).
Same builds as P0-7 (the instrument and MIDI FX variants).

Brief: `docs/spikes/midi-out.md`. Proves an instrument build with MIDI out and an AU MIDI FX build can play a realized clip in time with the host transport, looped, in Ableton and Logic.

### P0-9 CI on every push — `done`
Acceptance: GitHub Actions matrix (Linux, macOS, Windows) builds and tests `core` and typechecks TS on each push and PR.

**Update 2026-10-02:** the free Actions minutes for private repos ran out (macOS bills at 10x, Windows at 2x), and a Windows-only compile break in P1-17 (`core/src/catalog.cpp`, missing `<iterator>`) went unnoticed behind it. The repo is now public, so standard runners are free. CI runs only when code changes: Markdown, docs, eval results and the frozen spike don't trigger it. `npm run ci:full` runs it on demand.

## Phase 1: core loop (weeks 3–8)

**Goal:** a producer installs Flowstate, opens it on a MIDI track, describes an idea, hears it in time with their song within seconds, shapes it, and drags it in.

**Gate B:** 5 producers install cold (from the signed installer, with no help) and commit a clip to their DAW in their first session.

**Decisions (2026-09-29):**
- **Layout:** the single-screen Studio from the proposal, styled after v1 (look and feel, not v1's tab layout).
- **Signing:** planned on both platforms, an Apple Developer ID and a Windows signing identity (e.g. Azure Trusted Signing). The owner sets up the accounts; CI gets wired to them in P1-14.
- **Hosting:** decided later. Everything is built against a local agent service until the tester build is ready (weeks 7–8).
- **Backends:** dev runs on the `claude-code` backend and BYOK.

### Ordering

Two tracks run in parallel for weeks 3–5, then join.

| Weeks | Engine and service track | Plugin and UI track |
| --- | --- | --- |
| 3–4 | P1-1 providers, P1-2 latency, P1-3 quality round 2 | P1-5 bridge schema, P1-6 plugin shell, P1-8 design system |
| 5–6 | P1-4 agent service, P1-10 instant sketch | P1-7 audition and MIDI out, P1-9 Studio screen, P1-11 session and lineage |
| 7–8 | P1-13 hosting | P1-12 keys and settings, P1-14 installers, P1-15 CI release checks, P1-16 tester hand-off |

P1-17 (the built-in MIDI library) ran alongside both tracks; the Studio (P1-9) shows it.

### P1-1 Multi-provider layer on pi-ai — `done` (2026-10-01)
Done so far (2026-09-30):
- The `pi` backend in `cloud/src/backends.ts` works with every pi-ai provider. `backendFor({ provider, model, credential })` selects the backend per request, with a managed or BYOK credential. `planScore` takes the backend per call, and the CLI takes `--provider/--model`.
- `run.json` records the backend and stats: validity rate and latency p50/p95.
- `cloud` tests use pi-ai's faux provider to cover selection, BYOK key passing, repair turns, refusals and truncation, and a lockfile check that keeps out Pi coding-agent packages.
- Smoke runs (one prompt, `lofi-rainy-study`) were valid on the first attempt on DeepSeek `deepseek-v4-pro` (141 s) and OpenRouter `openai/gpt-5.5` (98 s). The Anthropic account had no API credit.

Eval run (2026-10-01, `evals/results/p1-1/`): the Phase 0 set is 20/20 valid on DeepSeek `deepseek-v4-pro` (p50 172 s), OpenRouter `openai/gpt-5.5` (p50 135 s) and OpenRouter `google/gemini-3.1-pro-preview` (p50 84 s). The 23 prompts stopped by OpenRouter credit were re-run after a top-up. Latency is P1-2's job.
Planner model calls go through `@earendil-works/pi-ai` (OpenAI, OpenRouter, Anthropic, Google and more). `claude-code` stays as the dev backend.
Acceptance:
- Provider, model and credential are selected per request (managed or BYOK).
- The Phase 0 prompt set plans valid scores on at least 3 providers, with results recorded per provider (validity rate, latency p50/p95, metrics).
- No Pi coding-agent packages are in the dependency tree.

### P1-2 Latency: from 62 s to the targets — `done` (2026-10-02: targets met on DeepSeek Flash, thinking off; per-route models and prompt caching move to P1-4)
Done so far (2026-10-01):
- Where the time goes: in the P1-1 runs a minified score is about 5.4 KB (roughly 2k tokens), but the planner averaged 11–20k output tokens. Most of the output is thinking at reasoning `high`.
- Both backends stream (`claude-code` through `stream-json`), take an abort signal and report time to first token and first answer text.
- Parts stream out of the reply as it is written (`cloud/src/part-stream.ts`): the IR puts `parts` last, so the head is parsed when the `"parts"` key arrives and each part as its closing brace lands. A part is reported (`onPart`, `firstPartMs`) once head and part pass validation; parts that can't play are recorded with the reason. This needs no prompt change. A JSON Lines reply format was tried first and dropped: Sonnet at low effort ignored it about half the time.
- `--reasoning` on the CLI. `run.json` records the reasoning level, first-token, first-text and first-part p50/p95, and the unplayable parts; `<id>.replies.txt` keeps the raw replies.
- Parts-only repair: when the head is sound, a repair turn asks only for the broken or missing parts, and they stream in too. A head that breaks the request's hard constraints (key, bars, meter) plays nothing.
- Round 1 on the full Phase 0 set (`evals/results/p1-2/`): Gemini 3.6 Flash (OpenRouter) at reasoning minimal is 20/20 valid, first text p50 0.8 s, first playable part p50 5.1 s, full plan p50 10.2 s (from 84 s for Gemini 3.1 Pro at high in P1-1). Sonnet at low effort (claude-code) is 20/20, first part 20.5 s, full 24.4 s. Symbolic metrics stay in the P1-1 range; no listening yet. DeepSeek Flash keeps thinking at any effort (70–105 s); the Anthropic API account has no credit.
- Compact motif strings (IR change, additive: the note-object form is still accepted). `"5:.75! 4:.25 b3:1 r:.5 1+:1/3"`: tokens in sequence, onsets implied. Core parses both forms identically; the spec teaches the string. On Gemini Flash minimal: motifs 384 → 98 B (median) while twice as long, head 1056 → 710 B, first part p50 5.1 → 4.4 s, full plan p50 10.4 s, 20/20 valid. On Sonnet low: motifs 597 → 101 B, first part 20.5 → 16.6 s, full 24.4 → 22.3 s, 20/20. No repair came from a motif token.
- Block fields that don't apply may be left out instead of written as null (IR change, additive). Scores shrink by a quarter to a third. **Gemini Flash minimal meets the full-plan target: p50 7.3 s**, 20/20 valid, first part p50 3.8 s; repairs 4 of 20. Sonnet low: full 20.1 s, first part 15.9 s.
- Compact harmony strings (IR change, additive), and the planner's request now says one part per lane with the lane name as its role (the model often wrote the chords lane as a pad, which cost a repair). Over three runs each on Gemini Flash minimal: 60/60 valid, first part p50 3.5 s (from 3.8 s; 18 of 60 under 3 s), full plan p50 7.8 s.
- The planner fills the context fields the session fixes (key, mode, bars, meter, tempo) from the request, so the model writes only swing and style, and a wrong key or length needs no repair. The request also states the step count per bar for the meter; without it the 6/8 gospel prompt failed 2 of 3 runs. Over three runs: **60/60 valid, first part p50 3.2 s (28 of 60 under 3 s), full plan p50 7.3 s** on Gemini Flash minimal.
- `--reasoning off` really turns thinking off (pi-ai raises a level a model lacks to the next one it has, so DeepSeek's "minimal" had been "low"). A sweep of 10 fast models from 8 vendors showed step counts failing everywhere, mostly bars written at half or double the grid's resolution. Core now reads such bars at that resolution (IR semantics; docs/ir-spec.md "Bar length"), and the validator matches core.
- **Targets met** on DeepSeek `deepseek-flash` with thinking off (two runs, `evals/results/p1-2/round7/`): 40/40 valid, first part p50 2.3 s, full plan p50 4.2 s. Gemini 3.1 Flash Lite: 39/40, 2.0 s and 3.6 s.

- Blind listening (owner, `evals/results/p1-2/pack-fast-vs-careful/`): fast (DeepSeek Flash, off) against careful (GPT-5.5, high) preferred 7 to 11 with 1 tie, p = 0.48; musicality 2.74 vs 2.89 of 5. No clear quality cost, but both score low, lowest on groove-defined styles (afrobeats, gospel, funk), which is P1-3's work.
- Rendered listening (`npm run -w evals render`, `evals/README.md`): a pack renders to loudness-matched MP3s through fixed per-genre General MIDI templates, with a `player.html` that switches options in sync and saves the score sheet. Listening no longer needs a DAW setup per prompt, and every round sounds the same.

Targets (p50 on the Phase 0 set): first sound under 100 ms (the P1-10 sketch), first AI part under 3 s, full 4-part plan under 8 s.
Levers, in order:
- Stream parts: each part is realized as it lands (done in the planner; P1-4 carries it to the plugin). **Update 2026-10-01:** parts stream out of the single JSON reply, not one JSON line per part; the line format was unreliable.
- Per-route model and effort (fast model for edits and single parts, stronger model for full plans).
- Prompt caching of the fixed system prompt.
- A more compact IR encoding if tokens dominate.
Acceptance: the targets are met on at least one production provider, measured by `evals`, with no validity regression.

### P1-3 Quality round 2 — `doing`
Phase 0 showed v2 beats v1 20/20, but it never scored above 3/5.
Done so far (2026-10-01):
- Named drum grooves in `core` (`core/src/grooves.cpp`, IR `groove` on drums blocks, docs/ir-spec.md "Grooves"): 23 idiomatic patterns for the Phase 0 styles (four_on_floor, dembow, afrobeats, boom_bap, lofi, trap, drill, neo_soul, gospel_shuffle, jazz_swing and more), each with per-lane feel (a lane can sit late, e.g. lofi's lazy snare). Lanes the model writes replace the groove's lane for that voice. The owner's P1-2 listening put the lowest scores on groove-defined styles (afrobeats, gospel, funk), with notes that the drums were what was missing.
- Voicing movement: the "8.9 vs v1's 2.4" was mostly a metric bug. Humanization staggers a chord's notes by 2–4 ticks, so the metric read each chord as single notes and counted the gaps inside a chord as movement. Grouping notes within a 64th and measuring only at chord changes: v2 moves 5.0 (about 1 semitone per voice, median; top-voice leaps of 5+ semitones in 3% of changes) against v1's 2.6, with richer harmony. The metric is fixed (`evals/README.md`); the voicer needs no change unless listening says otherwise.
- Grooves listening (`evals/results/p1-3/`): grooves 7, none 10, ties 3. The model mostly named a groove and then wrote its own kick, snare and hats, which replaced the groove's, so the grooves were barely heard. Now a groove's lanes win; block lanes only add voices it lacks.
- Bass tokens `2` `4` `6` (scale notes above the chord root) and `1` (root): every model kept writing them, which cost repairs or became rests.
- Grooves round 2: patterns checked against producer references (afrobeats changed most), and the planner's request names the grooves matching the prompt's style. 19 of 20 plans now use a groove; still 20/20 valid, first part 2.0 s, full plan 3.2 s (Flash Lite). Owner listening on blind pack `p1-3-grooves-2` (rendered): grooves preferred 14 to 3 with 3 ties (82%, p = 0.013); mean musicality 3.45 vs 3.00, fit 4.20 vs 3.40. Biggest gains on reggaeton, trap, afrobeats and gospel; small losses on lofi, neo-soul and the jazz waltz. Preference clears 70%; musicality is just short of 3.5.
Work:
- Prompt tuning per style.
- IR expressiveness gaps found in the r1 scores (e.g. per-bar chord rhythm variation, melodic development).
- Realizer voicing movement (8.9 semitones summed vs v1's 2.4; investigate) and groove templates per style.
Acceptance: blind A/B round 2 (new v2 vs r1 v2) is preferred at ≥ 70%, and mean musicality is ≥ 3.5.

### P1-4 Agent service — `done` (2026-10-02)
Done: `cloud/src/service.ts`, run with `npm run serve` (`cloud/README.md`).
- `POST /v1/plan` streams `header`, `partStarted`/`partDone` per part as the model writes, then `done` or `error`. Parts come from the planner's part stream, so repairs stream too. Every event is checked against `ServiceEvent` before it goes out.
- `POST /v1/edit` validates the `EditRequest`, then answers `unavailable` (skeleton). `GET /v1/health` returns `Health`.
- Provider per request: the managed default from the environment, other models only from a managed allowlist, any model with the user's key (`x-flowstate-provider-key`). `FLOWSTATE_FEATURE_BYOK=0` turns BYOK off.
- Errors carry bridge codes: the backends and planner now throw `FlowstateError` (`refused`, `truncated`, `cancelled`, `provider`, `invalid_score`, `bad_request`).
- One log line per request, with no keys, headers or prompt text; keys are redacted from error messages. SSE keepalive comments every 15 s for proxies (P1-13).
- Contract tests (`cloud/src/service.test.ts`, 16): stream order, roles, malformed requests, BYOK passing and redaction, the flag, the managed allowlist, refusal, truncation, invalid plans, cancellation and keepalive.
- Live check (2026-10-02, `claude-code` Sonnet low): a fixture request streamed header, four parts and `done`; closing the connection mid-plan logged `cancelled` and stopped the `claude` process.

Left for later issues:
- ~~The plugin's client~~ done 2026-10-02 with P1-7: `generate` sends a `PlanRequest` per variation and turns the stream into nodes, `partReady` and `generationDone` (`docs/bridge-spec.md`, "The plugin's client"). `edit`, `vary` and `addPart` still reply `unavailable`.
- `keep` and `reference` answer `unavailable` until the planner plans around them (locking in P1-11 needs `keep`).
- Edits (an IR patch from the model) beyond the skeleton.
- Per-route model and effort, and prompt caching (from P1-2), once the plugin sends edits and single parts.
- Feature flags in `Health`, so the plugin can mirror `byok` (P1-12).
- Auth tokens and rate limits: P1-13.

The `cloud/` HTTP service:
- `POST /v1/plan` (streamed SSE events: part started, part done, score done, error).
- `POST /v1/edit` (IR patch; skeleton only in Phase 1).
- Health endpoint and a request log without secrets.
- Feature flags (`byok`), and per-request provider choice from P1-1.
- From P1-2: the default production route is DeepSeek `deepseek-flash` with thinking off; per-route model and effort (edits and single parts vs full plans) and prompt caching of the fixed system prompt.
Acceptance:
- Contract tests against the bridge schema.
- Runs locally with one command.
- A cancelled request stops the provider stream.

### P1-5 Bridge schema — `done` (verified locally on Linux with GCC and Clang; macOS and Windows run in CI on the next push)
Done: `schema/src/bridge.ts` → `bridge.v0.schema.json` and `schema/cpp/include/flowstate/bridge.h` (`flowstate::bridge`). Shared fixtures round-trip in Zod and C++ with matching error paths; CI fails on stale generated files. Spec: `docs/bridge-spec.md`, checked against the proposal's UX flows (talk/tweak/touch, capture, variations, thread, audition, drag and export).

One schema (commands, events, session and score types) generates both the TS and C++ types. It covers WebView ↔ plugin and plugin ↔ service messages.
Acceptance:
- Generation runs in CI and fails on drift.
- A round-trip test in both languages.

### P1-6 Plugin shell — `done` (CI green on macOS, Windows and Linux: tests, pluginval strictness 8 on VST3 and AU, auval)
Done so far: `plugin/` with the instrument and MIDI FX variants, a JUCE-free session and bridge controller, host sync with key and tempo override, a 64-bar capture ring, and state save/restore through an immutable snapshot. Tests: session, processor (editor close and reopen, restore from another thread, junk state) and the VST3 host smoke test (two-idea project reopens intact). pluginval strictness 8 passes for every format on macOS and Windows (CI run 36635134900), and auval passes on macOS. Model commands reply `unavailable` until P1-4.
`plugin/` product target, built from the spike's proven pieces:
- Instrument and MIDI FX variants.
- Processor-owned `Session`, and a WebView host serving the bundled `ui/`.
- State save and restore of the session (IR, seeds, selection).
- Host sync: tempo, meter, bar position and loop, published to the UI; key and tempo override.
- A MIDI capture ring (last 64 bars).
Acceptance:
- pluginval strictness 8 passes on macOS and Windows in CI.
- Closing and reopening the editor loses nothing.
- The DAW project reopens with its ideas intact.

### P1-7 Audition and MIDI out — `done` (2026-10-02: Linux and local MSVC; macOS in CI on the next push)
Done, with the plugin's service client (P1-4's "Left" list):
- `generate` streams from the agent service (`plugin/src/ServiceClient.cpp`): one `PlanRequest` per variation on a per-process network pool, SSE events posted to the message thread, cancel by aborting the request. A variation's node is made when its first part lands and grows part by part; `done` replaces it with the authoritative score; a failed or cancelled variation loses its partial node. Semantics: `docs/bridge-spec.md`, "The plugin's client". The service URL is `FLOWSTATE_SERVICE_URL`, default `npm run serve`.
- The spike's scheduler (`plugin/src/session/Audition.*`), fed by `core` realizations of the current node, the audition node or a previewing catalog entry. A new clip takes over on the next bar line of the one playing, and an unchanged re-render isn't handed over, so it never cuts notes. Clips reach the audio thread through a lock-free hand-off that frees nothing there.
- The spike's preview synth on the instrument variant; MIDI out on both variants, merged with the MIDI passing through.
- Per-instance output: `setMidiOut` sends one role's part or all parts, on each part's channel or a forced one; the preview synth hears the same parts with drums kept on 10. Mute, solo and the loop range apply; free-run plays on the plugin's own clock while the host is stopped.
- Tests: the spike's sync checks ported as `flowstate_scheduler_tests` (527 checks, with bar-quantized switching, the hand-off and the filter); the generate flow in `flowstate_session_tests`; the real client against a fake SSE server in `flowstate_plugin_tests` (stream to playback, MIDI-out filter, cancel, errors, unreachable); the host smoke plays a restored idea and checks "drums only" on a forced channel. All pass on Linux and with MSVC on Windows (WebView build).
- Live (2026-10-02, `npm run serve` on `claude-code` Sonnet low): first part audible at 6.2 s, full plan at 8.6 s, the idea played in time, and a cancel mid-stream logged `cancelled` on the service. Opt-in test: `FLOWSTATE_LIVE_SERVICE_URL`.

Left: the DAW checks from the spike's list (sync at 60/120/180 BPM against a click, loop and seek, routing in Ableton and Logic) on the product build. The density knob doesn't change playback until `core` has the transform.

- The spike's scheduler, fed by `core` realizations, with bar-quantized switching between variations.
- A preview synth.
- **Per-part output choice for each instance** (all parts, or one part per instance: "send: bass only"), so one Flowstate per track works with no channel setup.
Acceptance:
- The scheduler test suite passes.
- The host smoke test covers part filtering.
- The spike's sync checks pass in CI.

### P1-8 Design system from v1 — `doing` (browser, tests and Windows done; the macOS check waits for a working product)
Done so far (2026-09-29):
- `ui/` is TypeScript with Preact and Vite. `npm run build:ui` writes a flat `ui/dist`, which the plugin bundles; CI builds it before the plugin.
- Tokens, 15 components and v1's icons (plus 10 new ones), specified in `docs/design/README.md`.
- `gallery.html` shows every component and state; in the plugin, start the DAW with `FLOWSTATE_UI_PAGE=gallery.html`.
- Side-by-side screenshots against v1: `docs/design/side-by-side-{chat,settings}.png`.
- Playwright (19 tests, Chromium locally; CI adds WebKit): axe WCAG 2.1 AA, an accessible name on every control, a Tab walk that reaches every control, component behaviour, and the plugin page against a mocked bridge.

Windows (2026-10-01): the gallery renders and is interactive in Ableton Live 12.4.6 on Windows 11, build `1b76d05-5` (`docs/host-checks.md`).
Left: check 12 on macOS (Logic or Ableton). Deferred by the owner (2026-10-01): Mac testers get a build once the core loop makes music, not the shell. It runs with the first P1-16 hand-off.
Port v1's visual identity to CSS tokens and components: colours, type, spacing, radii, logo and SVG icons from `docs/design/` and v1 `assets/`, and the dark compact shell. Components: buttons, knobs, toggles, inputs, lanes, cards, sheets and toasts.
Acceptance:
- A component gallery page renders in a browser and in the plugin.
- Side-by-side screenshots match v1's look.
- Accessibility: every control is labelled and keyboard-reachable.

### P1-9 Studio screen — `todo`
- Context strip (key, mode, tempo, meter, bars; host values locked, with override).
- Part lanes (mini piano roll, play/solo, lock, vary, re-roll, density, per-part drag handle).
- Prompt bar with suggestion chips.
- Thread drawer (result cards: play, restore, branch, drag).
- Settings sheet (provider and model, BYOK key, MIDI out, usage).
Acceptance:
- Playwright tests against a mocked bridge cover every flow in the proposal's UX section.
- It works at 720×480 and at larger sizes.
- The space bar still reaches the DAW.

### P1-10 Instant sketch — `todo`
`core` makes a rule-based sketch from the context strip alone, in under 100 ms, so every Generate makes sound immediately. AI parts replace sketch parts as they stream in (P1-2).
Acceptance: under 100 ms for 8 bars × 4 parts; varied across seeds.

### P1-11 Session and lineage — `todo`
- Every result is a lineage node: initial, regenerate, vary or edit.
- Undo and redo, A/B between nodes, lock parts.
- Stored in plugin state, bounded in size.
Acceptance: survives editor close, project save and reopen, and 50 generations in one session.

### P1-12 Keys and settings — `todo`
BYOK keys are stored in the OS keychain (macOS Keychain, Windows Credential Manager) and never in DAW state or logs. Behind the `byok` release flag.
Acceptance:
- A test proves no key is in saved plugin state.
- The flag hides the BYOK UI with no code change.

### P1-13 Hosting the agent service — `todo` (planned: Hetzner)
Plan (2026-10-01): a Hetzner server running the service in Podman, set up with the owner's devarch setup. The service is light and I/O-bound: it holds each streamed plan open for 1–2 minutes while the model writes, with no GPU or database. Caddy can terminate TLS. If a proxy with an idle timeout (e.g. Cloudflare's, about 100 s) sits in front, P1-4's stream needs keepalive events.
Deploy `cloud/` so testers' plugins can reach it (TLS, a per-tester token, basic rate limits).
Acceptance: the tester build talks to the hosted service; a deploy is one command from CI.

### P1-14 Installers and signing — `todo` (needs the owner's Apple Developer and Windows signing accounts)
- macOS: a `.pkg` with VST3, AU and Standalone, Developer ID signed and notarized.
- Windows: an installer for VST3 and Standalone, Authenticode signed.
- Built on each tag.
Acceptance: installs on a clean machine or user account with no Gatekeeper or SmartScreen bypass steps.

### P1-15 CI release checks — `todo`
Every tester build is verified before a human sees it:
- core tests and pluginval on both OSes, and auval on macOS.
- Headless host smoke (load, sync, MIDI out, per-part filter, state round trip).
- UI Playwright suite, and an installer smoke on a fresh runner.
Acceptance: a failing check blocks the artifact; a passing build carries a build ID shown in Settings.

### P1-16 Tester hand-off (Mac friend first) — `todo`
Stand-in until then (2026-09-30): `npm run pack:mac` builds an unsigned tester zip from the latest green CI run, with an install script, a README checklist and the build ID (`docs/testing-plugin.md`, section 7).
A tester package per build: signed installer, a one-page checklist (the Phase 0 host checks plus the core loop), and a feedback form that includes the build ID. The first recipient is the Mac tester for Logic.
Acceptance: the tester completes it without contacting us for setup, and reports come back with build IDs.

### P1-17 Built-in MIDI library — `done` (2026-10-01; verified locally on Linux, CI on macOS and Windows on the next push)
Flowstate ships with a MIDI library, starting with GodFlow's pack, credited "MIDI by GodFlow (flowknows) for Flowstate." (GodFlow / Flowstate). Library clips and AI results are one catalog the Studio can search, preview, drag and start from. Spec: `docs/library.md`.

**Decision: clips are stored as MIDI and as IR.** Each clip keeps its normalized MIDI, and `core`'s new MIDI→IR analyzer also writes it as score IR. The IR is what makes a library clip interchangeable with an AI result: it goes into the lineage, takes the same tweaks and re-voicing, can be locked while the model writes around it, and is read by the model in its own language as a style example. It is tractable: on the GodFlow pack the IR plays back every clip's rhythm exactly (onset F1 1.0, 29/29), and chord pitch classes with 0.75–1.0 overlap. The MIDI stays because the IR is a description, and core re-voices chords. Preview and drag-out play the clip exactly as written, and the gap is recorded per clip as fidelity.

Done:
- `library/packs/godflow`: 29 clips (16 R&B/jazz, 6 world and 5 pop chord clips, 2 hip-hop basses), `manifest.json` in the new `flowstate.libraryPack.v2` (Zod in `schema/src/library.ts`, JSON Schema generated), and `CREDITS.md`. Owner's calls (2026-10-01): v1's pop "melodies" are chord stacks, so they import as pop chords; the world clips keep `world`; bass-01/03/04 stay out (outside C1–C4), with the reasons in the manifest; no drums, so no fake drum entries.
- `core`: an SMF reader, the analyzer (`analyze.h`, CLI `fs-analyze`) and catalog search (`catalog.h`).
  - The analyzer keeps v1's profiler measures and lane thresholds. It adds rolled-chord onsets, key detection (blank with the reason when unreliable: 13 of 29 keys are detected), grid and swing, chord naming through core's parser, IR per lane, a fidelity check and descriptors.
  - Search ranks by key and tempo fit to the session.
- `library/`: `validate` (v1's path-safety, "no silent files", no leaked names, consistent credit, honest lanes), `build` (writes `library/catalog`: `catalog.json` plus normalized clips without source track names) and `check` (drift, in CI).
- Bridge: `CatalogEntry` (library clip with `credit`, or AI result with prompt and lineage), `LibraryCatalog` (a bridge root, with C++ types), the commands `searchCatalog`, `previewEntry`, `useEntry` and `dragEntry`, `Reply.catalog`, `Session.preview`, the node kind `library`, and `entryId` on nodes. Fixtures round-trip in Zod and C++.
- Plugin: the catalog is bundled (`juce_add_binary_data`) and the four commands are handled. A clip becomes a `library` node; its credit travels to the vary, edit, tweak and touch nodes made from it, and into dragged files as copyright text. Plugin state is version 2, and version 1 states still restore.
- Planner: `--examples N` shows up to N matching library clips as style examples, in IR with the credit (off by default; `run.json` records which).

Acceptance:
- [x] Every entry of a pack imports, or fails with a precise, path-free reason (`lane_conflict`, `bass_range`, `not_drums`, `malformed`, ...), tested on the GodFlow pack and on throwaway bad packs.
- [x] Every clip is classified for search: lane, genres, key and mode (or the reason they are blank), tempo and range, meter, bars, energy, density, complexity, groove and feel.
- [x] The credit is next to the content (`CREDITS.md`) and on every catalog entry, and it travels with nodes and dragged files.
- [x] One catalog model for library clips and AI results, with search, preview, use and drag in the bridge contract and the plugin. Tests cover core search, the session controller and the processor.
- [x] Adding a pack is documented and repeatable: a manifest, `npm run -w library validate`, then `build`. CI fails on a stale catalog.

Left for other issues:
- The Studio's library browser (P1-9).
- Preview playback in time with the host (the audition scheduler, P1-7).
- Whether style examples improve plans: an eval run with and without `--examples`, then blind listening (P1-3).
- A transpose-to-session-key action for a used clip (core's `tweak transpose`).
