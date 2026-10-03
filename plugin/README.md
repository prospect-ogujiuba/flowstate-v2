# plugin

The Flowstate plugin (P1-6 onward): a JUCE 9 shell that owns the session, follows the host, and
hosts the WebView UI from `../ui`. `spike/` is the frozen Phase 0 spike it was built from.

| Target | Kind | Formats | Codes |
| --- | --- | --- | --- |
| `Flowstate` | Instrument: `IS_SYNTH`, MIDI in and out, stereo out | VST3, AU, Standalone | `Flws` / `Fls2`, `com.flowstate.instrument` |
| `FlowstateMidiFx` | `IS_MIDI_EFFECT`, MIDI only | AU (macOS), VST3 | `Flws` / `Flm2`, `com.flowstate.midifx` |

## Layout

| Path | Role |
| --- | --- |
| `src/session/Session.*` | The session: lineage of score IRs, undo/redo, thread, part states, context override, audition and MIDI-out choices; the effective context (override → host → score → default); realization through `core` into the bridge's `Clip`. JUCE-free. |
| `src/session/Controller.*` | Dispatches bridge `Command`s (`docs/bridge-spec.md`) against the session, and turns the agent service's events into nodes and generation events. OS actions and the service go through the `Platform` interface. Decides what plays (`audition()`). JUCE-free. |
| `src/session/Audition.*` | The audition: the spike's transport-locked scheduler with bar-quantized switching, rendering a realized clip for this instance (loop range, mute and solo, MIDI-out role), and the lock-free clip hand-off to the audio thread. JUCE-free. |
| `src/session/Sse.*` | Incremental parser for the service's server-sent events. JUCE-free. |
| `src/session/Capture.*` | "Use what I just played": an SPSC ring from the audio thread and a 64-bar history on the message thread. JUCE-free. |
| `src/session/PluginState.*` | The saved-state envelope around `SavedSession`. JUCE-free. |
| `src/PluginProcessor.*` | Host sync, capture, MIDI pass-through, audition into MIDI out and the preview synth, free-run, state snapshot and restore, drag and export. |
| `src/ServiceClient.*` | The agent service client: `POST /v1/plan` and its SSE stream on the process's network pool, events posted to the message thread, cancel by aborting the request. |
| `src/PreviewSynth.h` | The spike's preview voices (tones, and drums on channel 10), so the instrument is audible with no routing. |
| `src/WebEditor.cpp` | WebView editor: serves `ui/` and carries the bridge (native function `bridge`, event `bridge`). |
| `src/HeadlessEditor.cpp` | Native stand-in for headless builds (Linux CI, pluginval). |
| `src/MidiFiles.*` | Clip → `.mid` (one track per part, or per drum sublane). |

Threading follows `docs/threading.md`. The audio thread reads the playhead, publishes atomics, pushes to the capture ring, passes MIDI through and schedules the audition from a pre-rendered clip. `getStateInformation` reads an immutable snapshot, so it's safe from any thread. `setStateInformation` decodes on the calling thread and applies on the message thread; a get right after a set returns what was set.

Audition (P1-7):
- What plays: a previewing catalog entry, else the audition node, else the current node. After every session change it is re-rendered on the message thread and handed over only if it differs, so an unchanged idea never cuts sustained notes.
- A new clip takes over on the next bar line of the one playing (at once when the host is stopped, nothing sounds, or the host seeks). Bar 1 is host PPQ 0.
- MIDI out carries the parts this instance sends ("send: bass only"), on each part's channel or the forced one, merged with the MIDI passing through. The instrument's preview synth plays the same parts on their own channels, so drums stay drums on a forced channel. Muted parts are left out, and solo wins.
- With free-run on, the audition plays on the plugin's own clock (from bar 1, at the host's tempo) while the host is stopped.

Generating (P1-4's client): `generate` streams from the agent service at `FLOWSTATE_SERVICE_URL` (default `http://127.0.0.1:8787`, `npm run serve`). The semantics are in `docs/bridge-spec.md`, "The plugin's client".

Not done yet:
- `reroll`, `tweak` and `editNotes` reply `unavailable` until `core` has per-part seeds and transforms. The density knob doesn't change playback yet.
- BYOK keys (P1-12) live in the macOS Keychain or Windows Credential Manager (`src/OsKeyStore_*.cpp`). Linux builds have no keychain, so `setApiKey` replies `unavailable` there.

## Build and test

```sh
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugin
ctest --test-dir build/plugin --output-on-failure
```

On Linux the build is headless by default (`JUCE_WEB_BROWSER=0`, native stand-in editor). It needs the usual JUCE packages (ALSA, FreeType, fontconfig, X11 headers). `-DFLOWSTATE_PLUGIN_BUILD_PLUGIN=OFF` builds only the JUCE-free session library and its tests, with no JUCE download.

Tests:
- `flowstate_session_tests`: session, controller, capture and state format. It also covers the generate flow against recorded service events: streamed parts becoming a node, variations, failures, cancel, `keep`, and what plays.
- `flowstate_scheduler_tests`: the spike's sync checks (100 bars at 60, 120 and 180 BPM, block sizes, seeks, host loops, tempo ramps, stop), plus bar-quantized switching, the clip hand-off and the audition filter.
- `flowstate_plugin_tests`: the real processor without a host. It covers host sync, capture and pass-through, state round trip (including from another thread), junk state, and editor close and reopen. It also runs the service client against a fake SSE server on localhost: a streamed plan that then plays (MIDI out and the preview synth), the MIDI-out filter, cancel aborting the request, error events, and an unreachable service.
- Opt-in, against a running service: `FLOWSTATE_LIVE_SERVICE_URL=http://127.0.0.1:8787` makes `flowstate_plugin_tests` plan, play and cancel for real (skipped otherwise).
- `flowstate_host_smoke`: loads the built VST3s through JUCE's host. It checks MIDI pass-through, silence with no idea, bypass, a two-idea project reopening intact, the restored idea playing in time, and part filtering ("drums only" on a forced channel).

CI (`.github/workflows/plugin.yml`) runs all of them plus pluginval at strictness 8 on every format, on macOS and Windows (and headless Linux), and `auval` on macOS.
