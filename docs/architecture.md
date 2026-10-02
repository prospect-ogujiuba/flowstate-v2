# Architecture

Three parts: a JUCE 9 plugin that owns state, timing and sound; a WebView UI inside it; and a cloud agent service that owns the models. One C++ music engine (`core`) runs in both the plugin (native) and the cloud (WASM), so music theory exists exactly once.

```
DAW ⇄ plugin processor (audio/MIDI thread: host sync, capture ring, audition, MIDI out)
        ⇅ lock-free queues
      Session (message thread: context, IR lineage, chat thread; saved in plugin state)
        ⇅ core (realize, analyze, transform)       ⇅ typed bridge          ⇅ HTTPS + SSE
                                                  WebView UI (TS)         cloud agent service (TS)
                                                                            ⇅ core.wasm, model providers, accounts
```

## Decisions

| Decision | Choice | Why |
| --- | --- | --- |
| Plugin framework | JUCE 9 | VST3/AU/Standalone from one codebase; mature `WebBrowserComponent` |
| UI | WebView + TypeScript, bundled into the binary | Cards, text, forms and a piano roll are web-shaped; fast iteration and real accessibility |
| Music engine | C++20 `core`, JUCE-free, native + WASM | One implementation, re-renders under 10 ms, offline edits, server-side plan checks |
| AI runtime | Cloud agent service, thin tool loop on the Anthropic SDK | No local runtime, Windows works, prompts and models update without a release |
| Model access | Two provider modes behind one interface: managed (Flowstate's key) and BYOK (the user's key, stored encrypted server-side). BYOK is on from day one behind a release flag. | Development runs on BYOK; the flag lets launch hide or show it without code changes |
| Contract | Score IR (`docs/ir-spec.md`) and the bridge schema (`docs/bridge-spec.md`), one Zod source each; the bridge generates C++ types for the plugin | One versioned contract per boundary |
| Persistence | Session in plugin state (IR + seeds); optional cloud sync by project | Ideas survive editor close and project reload |

## Planner (today)

`cloud/src/planner.ts`: request + controls → model → JSON → Zod parse → semantic validation (`validate.ts`) → up to two repair passes.

The model call goes through a backend from `cloud/src/backends.ts`. Callers pick one per request with `backendFor(selection)`, where a selection is a provider, a model and a credential:
- `pi` (production): any provider in `@earendil-works/pi-ai`, such as Anthropic, OpenAI, Google, OpenRouter or DeepSeek, with high reasoning effort by default. The credential is either **managed**, meaning the service's own key from the provider's usual environment variable (`ANTHROPIC_API_KEY`, `OPENROUTER_API_KEY`, ...), or **byok**, the user's key passed with that request. An empty BYOK key is an error rather than a silent fall back to the managed key. Refusals, safety stops and truncated output are errors.
- `claude-code` (dev and evals only): headless Claude Code on the developer's own subscription. It takes no user key.

A plan is one conversation (`converse` in `backends.ts`). On `pi` it runs on a `pi-agent-core` `Agent` (P1-18): the transcript is Pi messages, and the planner's check runs as the loop's `finishTurn`, which either ends the run or sends a repair request as a follow-up. The score is still the reply's JSON text, not tool-call arguments, so parts stream out as they land. `claude-code` is driven one reply at a time.

Capabilities (`cloud/src/capabilities/`) take the shape of Pi extensions: a factory calls `registerTool` and `on("before_agent_start" | "tool_call")`, and handlers get a context holding the request, the library catalog and core's analyzer. They are imported statically in `registry.ts`. None of them imports Node built-ins, and there is no `exec`. Today there are three:
- style packs: prompt sections after the IR spec, so the fixed prompt stays a cacheable prefix
- `library_examples`: a read-only tool over `library/catalog`
- `analyze_clip`: core's `fs-analyze` on a library clip named by catalog id; the host resolves the file and runs the binary without a shell

Tools are offered only when a request asks (`--tools` on the CLI), because a call costs a model round trip. Every call is checked against `ALLOWED_TOOLS` in `beforeToolCall`, and a request may make at most 4.

`planScore(request, backend)` takes the backend per call. The CLI and evals build one from the environment (`selectionFromEnv`): `FLOWSTATE_PLANNER_BACKEND` (`claude-code`, the default, or `pi`; the old `api` means `pi` with Anthropic), `FLOWSTATE_PLANNER_PROVIDER`, `FLOWSTATE_PLANNER_MODEL` and `FLOWSTATE_PLANNER_REASONING`. Constrained decoding is not used: the IR schema exceeds the providers' grammar-size limits.

Planned: stream parts as they complete, IR patch mode for edits, plan → realize → measure → revise for full sections, and per-route model and effort tuning against the latency target.
