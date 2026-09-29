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
| `src/session/Controller.*` | Dispatches bridge `Command`s (`docs/bridge-spec.md`) against the session. OS actions go through the `Platform` interface. JUCE-free. |
| `src/session/Capture.*` | "Use what I just played": an SPSC ring from the audio thread and a 64-bar history on the message thread. JUCE-free. |
| `src/session/PluginState.*` | The saved-state envelope around `SavedSession`. JUCE-free. |
| `src/PluginProcessor.*` | Host sync, capture, MIDI pass-through, state snapshot and restore, drag and export. |
| `src/WebEditor.cpp` | WebView editor: serves `ui/` and carries the bridge (native function `bridge`, event `bridge`). |
| `src/HeadlessEditor.cpp` | Native stand-in for headless builds (Linux CI, pluginval). |
| `src/MidiFiles.*` | Clip → `.mid` (one track per part, or per drum sublane). |

Threading follows `docs/threading.md`. The audio thread only reads the playhead, publishes atomics, pushes to the capture ring and passes MIDI through. `getStateInformation` reads an immutable snapshot, so it's safe from any thread. `setStateInformation` decodes on the calling thread and applies on the message thread; a get right after a set returns what was set.

What P1-6 doesn't do yet:
- Model commands (`generate`, `edit`, `vary`, `addPart`) reply `unavailable` until the agent service (P1-4).
- `reroll`, `tweak` and `editNotes` reply `unavailable` until `core` has per-part seeds and transforms.
- `setApiKey` replies `unavailable` until the keychain (P1-12).
- Audition, the preview synth and MIDI out are P1-7.

## Build and test

```sh
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugin
ctest --test-dir build/plugin --output-on-failure
```

On Linux the build is headless by default (`JUCE_WEB_BROWSER=0`, native stand-in editor). It needs the usual JUCE packages (ALSA, FreeType, fontconfig, X11 headers). `-DFLOWSTATE_PLUGIN_BUILD_PLUGIN=OFF` builds only the JUCE-free session library and its tests, with no JUCE download.

Tests:
- `flowstate_session_tests`: session, controller, capture and state format.
- `flowstate_plugin_tests`: the real processor without a host. It covers host sync, capture and pass-through, state round trip (including from another thread), junk state, and editor close and reopen.
- `flowstate_host_smoke`: loads the built VST3s through JUCE's host. It checks MIDI pass-through, silence, bypass, and a two-idea project reopening intact.

CI (`.github/workflows/plugin.yml`) runs all of them plus pluginval at strictness 8 on every format, on macOS and Windows (and headless Linux), and `auval` on macOS.
