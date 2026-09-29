# Testing the plugin

How to build the Flowstate plugin, run its automated tests, validate it, and check it by hand in a DAW. Set up the machine first with `dev-setup.md`. Commands run from the repo root.

## 1. Configure

Pick the block for your OS. Each creates `build/plugin` and is needed once, or again after pulling CMake changes.

macOS (native architecture; CI builds universal):
```sh
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFLOWSTATE_PLUGIN_LTO=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

Windows (x64 Native Tools prompt):
```bat
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DFLOWSTATE_PLUGIN_LTO=OFF
```
Add `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` if ccache is installed.

Linux (headless): the macOS command without anything platform-specific works as is; the headless editor is the default.

Expected: the output ends with `-- Build files have been written to: .../build/plugin`. The first configure downloads JUCE and shows `Downloading Microsoft.Web.WebView2 ...` on Windows.

## 2. Build

```sh
nice -n 19 cmake --build build/plugin -j2      # Windows: cmake --build build/plugin -j2
```

A first build compiles JUCE four times (two plugins and two test apps). Expect roughly 15 to 40 minutes on 4 cores; with ccache, later rebuilds take a few minutes. It ends with a line like `[158/158] Linking CXX executable ...` and no `FAILED:` lines. Warnings from JUCE's own sources (HarfBuzz, `juce_graphics`) are expected; warnings from `plugin/src` or `plugin/tests` are not.

To build only part of it:

| Target | What |
| --- | --- |
| `flowstate_session_tests` | JUCE-free session tests (fast, no JUCE) |
| `flowstate_plugin_tests` | processor tests |
| `flowstate_host_smoke` | host smoke test (also builds both VST3s) |
| `Flowstate_VST3`, `FlowstateMidiFx_VST3`, `Flowstate_AU`, `FlowstateMidiFx_AU`, `Flowstate_Standalone` | individual plugin formats |

Example: `cmake --build build/plugin -j2 --target flowstate_plugin_tests`.

### Where the outputs go

| Output | Path under `build/plugin/` |
| --- | --- |
| Instrument VST3 | `Flowstate_artefacts/Release/VST3/Flowstate.vst3` |
| Instrument AU (macOS) | `Flowstate_artefacts/Release/AU/Flowstate.component` |
| Standalone app | `Flowstate_artefacts/Release/Standalone/Flowstate.app` (macOS), `Flowstate.exe` (Windows), `Flowstate` (Linux) |
| MIDI FX VST3 | `FlowstateMidiFx_artefacts/Release/VST3/Flowstate MIDI FX.vst3` |
| MIDI FX AU (macOS) | `FlowstateMidiFx_artefacts/Release/AU/Flowstate MIDI FX.component` |
| Session tests | `flowstate_session_tests` (`.exe` on Windows) |
| Processor tests | `flowstate_plugin_tests_artefacts/Release/flowstate_plugin_tests` |
| Host smoke | `flowstate_host_smoke_artefacts/Release/flowstate_host_smoke` |

## 3. Automated tests

```sh
ctest --test-dir build/plugin --output-on-failure
```

Expected:
```
1/3 Test #1: session ..........................   Passed
2/3 Test #2: processor ........................   Passed
3/3 Test #3: host_smoke .......................   Passed

100% tests passed out of 3
```

What each covers:
- **session**: lineage, undo/redo and branching; save and restore; effective context (override, host, score, default); every bridge command, including malformed input and an API key never being saved or echoed; capture ring and history; the state envelope. It ends with `[doctest] Status: SUCCESS!` (about 8,300 assertions).
- **processor**: the real processor without a host. It covers host sync into the transport event, capture with MIDI pass-through, a project reopening intact, state set from another thread, junk state being ignored, and editor close and reopen three times. Its summary is `test cases: 6 | 6 passed`.
- **host_smoke**: loads both VST3s through JUCE's VST3 host. It checks MIDI pass-through under a moving transport, silence (no audition yet), bypass, a saved two-idea project restoring with its selection, and the state being stable across instances. On success it prints each plugin path and then `OK (0 failures)`; on failure it prints `FAIL: <check> -- <detail>` lines.

To run one directly with full output, for example:
```sh
build/plugin/flowstate_plugin_tests_artefacts/Release/flowstate_plugin_tests
build/plugin/flowstate_host_smoke_artefacts/Release/flowstate_host_smoke \
  build/plugin/Flowstate_artefacts/Release/VST3/Flowstate.vst3 \
  "build/plugin/FlowstateMidiFx_artefacts/Release/VST3/Flowstate MIDI FX.vst3"
```

## 4. pluginval (strictness 8)

```sh
PV=/path/to/pluginval    # see dev-setup.md
"$PV" --strictness-level 8 --validate-in-process --validate build/plugin/Flowstate_artefacts/Release/VST3/Flowstate.vst3
"$PV" --strictness-level 8 --validate-in-process --validate "build/plugin/FlowstateMidiFx_artefacts/Release/VST3/Flowstate MIDI FX.vst3"
```

- On Linux, add `--skip-gui-tests` (and run under `xvfb-run -a` if there's no display).
- On macOS, validate the AUs too, after copying them to `~/Library/Audio/Plug-Ins/Components/`: pass the `.component` paths to `--validate`.

Expected: a long log that ends with `SUCCESS` and exit code 0. Any `!!! Test ... failed` line is a failure.

macOS only, after copying the AUs:
```sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aumu Fls2 Flws     # instrument
auval -v aumi Flm2 Flws     # MIDI FX
```
Expected: each ends with `AU VALIDATION SUCCEEDED.`

## 5. Install into a DAW

Copy the built bundles (or the CI artifacts from the `plugin` workflow run, named `flowstate-macos-universal` and `flowstate-windows-x64`):

| OS | VST3 | AU |
| --- | --- | --- |
| Windows | `C:\Program Files\Common Files\VST3\` | — |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |

CI artifacts on macOS are ad-hoc signed; after unzipping a download, clear the quarantine flag:
```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Flowstate.vst3 ~/Library/Audio/Plug-Ins/Components/Flowstate.component
```
Then rescan plug-ins in the DAW. The plug-ins appear as **Flowstate** (an instrument) and **Flowstate MIDI FX** (a MIDI effect, or an AU MIDI FX in Logic), made by "Flowstate".

## 6. Manual checks in a DAW

What the plugin can do today:
- It follows the host.
- It shows the session in a placeholder UI.
- It passes MIDI through.
- It saves its state with the project.

It can't make ideas or sound yet: generating needs the agent service (P1-4) or the instant sketch (P1-10), and audition is P1-7. Record the results in `docs/spikes/results.md` style, with the build ID (a CI build is named `<commit>-<run>`, a local build is `dev`).

For each host (Ableton Live and FL Studio or Bitwig on Windows; Logic, plus Ableton, on macOS):

1. **Loads:** put **Flowstate** on a MIDI track. The window opens at 960×600 with the dark UI: the context strip at the top, the "Ideas" panel on the right and the prompt bar at the bottom.
2. **Follows the host:**
   - With the transport stopped, the strip shows the host tempo and meter (e.g. `120 bpm · 4/4`) and `C major · 4 bars`.
   - Change the host tempo to 97: the strip follows within a second.
   - Change the meter to 7/8: the strip follows.
3. **Transport:** press play. The transport pill counts `bar N · beat N.N` in time with the host. Stop: it says `stopped`. Loop a region: bar and beat wrap with the loop.
4. **Space bar:** click the empty UI area, then press Space. The DAW transport toggles.
5. **Typing:** click the prompt, type text with Space and letters. Nothing reaches DAW shortcuts. Press Esc and then Space: the transport toggles again.
6. **Prompt reply:** send "moody chords". The notice line reads "Generating needs the agent service, which isn't connected yet." (expected in this build).
7. **Resize:** drag the corner to a new size, close the window and reopen it. It comes back at the same size, and it can't be made smaller than 720×480.
8. **State:** save the project, close it and reopen it. The plugin loads without errors, and the window size persists.
9. **MIDI FX pass-through:** put **Flowstate MIDI FX** in front of an instrument (in Logic: MIDI FX slot; in Ableton: the VST3 on the track before the instrument, where the host allows it). Play the keyboard: the instrument still sounds, so notes pass through.
10. **Multiple instances:** open two Flowstate windows at once. Both render, and each follows the transport.
11. **Drag (placeholder):** the Drag button stays disabled, since there's no idea yet. Drag-out is covered again in P1-7 with real ideas.

Report anything that differs, with the host name and version, the OS and the build ID.
