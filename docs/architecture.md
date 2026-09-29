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

`cloud/src/planner.ts`: request + controls → Claude → JSON → Zod parse → semantic validation (`validate.ts`) → up to two repair passes.

The model call goes through a backend from `cloud/src/backends.ts`, selected with `FLOWSTATE_PLANNER_BACKEND`:
- `claude-code` (the default for dev and evals): headless Claude Code on the developer's own subscription.
- `api`: the Anthropic SDK with a key (the BYOK path), using `claude-opus-5` with adaptive thinking, high effort and server-side fallbacks. Constrained decoding is not used: the IR schema exceeds the API's grammar-size limit.

Planned: stream parts as they complete, IR patch mode for edits, plan → realize → measure → revise for full sections, and per-route model and effort tuning against the latency target.
