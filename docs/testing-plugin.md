# Testing the plugin

How to build the Flowstate plugin, run its automated tests, validate it, and check it by hand in a DAW. Set up the machine first with `dev-setup.md`. Commands run from the repo root.

## 1. Configure

The plugin bundles the built UI from `ui/dist`, so build it first, and again after UI changes (the next plugin build picks it up):
```sh
npm ci && npm run build:ui
```
Configuring a WebView build (macOS, Windows) without it fails with `No built UI in .../ui/dist`. The headless Linux build only warns.

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

Install either your own build (below) or a CI build from GitHub Actions. Every push that changes the plugin or what it bundles runs the `plugin` workflow; Markdown-only commits don't. `npm run ci:full` runs it on demand. A green run keeps two artifacts for 30 days, `flowstate-windows-x64` and `flowstate-macos-universal`, each with a `BUILD_ID` file (`<commit>-<run>`). The macOS bundles are only ad-hoc signed until P1-14, so macOS blocks them until the quarantine flag is cleared. The install scripts do that.

### A CI build on Windows

From WSL (or any shell with `gh` logged in), in the repo:
```sh
npm run fetch:build -- windows              # latest green run on the current branch
npm run fetch:build -- windows --branch main
npm run fetch:build -- windows --run 36702654122
```
On WSL this writes `flowstate-windows-x64` into your Windows Downloads folder, with `install.ps1` and `gallery.ps1` next to the build (elsewhere, into `dist/builds`). It prints the install commands with the full path filled in. From WSL they use `powershell.exe`, since plain `powershell` isn't found there. From Windows PowerShell, in that folder:
```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1              # asks for admin; replaces any older copy
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall
```
Close DAWs that have Flowstate loaded first: Windows won't replace a loaded plug-in. Then rescan plug-ins in the DAW.

### A CI build on macOS

```sh
npm run pack:mac                            # or: -- --branch main, -- --run <id>
```
This writes `dist/builds/flowstate-macos-<build id>.zip`: the bundles, `install.sh` and a `README.txt`. On the Mac, unzip it, then in Terminal run `bash <path to install.sh>` (typing `bash ` and dragging the file in works). It installs, per user:

| Bundle | Folder |
| --- | --- |
| `Flowstate.vst3`, `Flowstate MIDI FX.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| `Flowstate.component`, `Flowstate MIDI FX.component` | `~/Library/Audio/Plug-Ins/Components/` |
| `Flowstate.app` (Standalone) | `~/Applications/` |

It clears the quarantine flag on each one and restarts `AudioComponentRegistrar`, so AU hosts see the new components. `bash install.sh --uninstall` removes them all. Running a `.sh` through `bash` gets past Gatekeeper; double-clicking a script or bundle doesn't.

### Your own build

Copy the bundles from `build/plugin/*_artefacts/Release/` (see "Where the outputs go") into the same folders: VST3 on Windows goes in `C:\Program Files\Common Files\VST3\`. Local builds aren't quarantined.

Then rescan plug-ins in the DAW. The plug-ins appear as **Flowstate** (an instrument) and **Flowstate MIDI FX** (a MIDI effect, or an AU MIDI FX in Logic), made by "Flowstate".

### CI status

```sh
npm run ci:status                 # the last 10 runs on the current branch
npm run ci:status -- --watch      # follow the newest plugin run; exits non-zero if it fails
npm run ci:full                   # run ci and plugin on demand (pushed branch)
```

## 6. Manual checks in a DAW

What the plugin can do today:
- It follows the host.
- It shows the Studio (P1-9): v1's header and footer, the context strip, part lanes, the prompt bar and the thread drawer.
- It generates through the agent service and plays ideas in time (P1-7), through the preview synth and MIDI out.
- It passes MIDI through, and saves its state with the project.

Start the agent service first: `npm run serve` (on WSL it listens on `127.0.0.1:8787`, which Windows reaches). Controls this build can't run yet (re-roll, tweak, note edits, density) look dimmed: hovering shows why, and pressing one shows the reason as a toast. Record the results in `docs/host-checks.md`, with the build ID (Settings shows it; a CI build is named `<commit>-<run>`, a local build is `dev`).

For each host (Ableton Live and FL Studio or Bitwig on Windows; Logic, plus Ableton, on macOS):

1. **Loads:** put **Flowstate** on a MIDI track. The window opens at 960×600: the header (Studio and Library pills, the logo, the yellow "All parts" pill, the connection bars, settings and account), the "What do you want to make today?" starters, the prompt bar and the footer.
2. **Follows the host:** with the transport stopped, the Tempo and Meter chips show the host values with a lock icon. Change the host tempo to 97: the chip follows within a second. Change the meter to 7/8: it follows.
3. **Generate:** click **Surprise me**, or type "moody chords" and press Return. "Planning…" shows with a Cancel button, then lanes appear part by part; the connection bars turn green. Play the host: the ideas play in time, the lanes show a playhead, and the footer counts `Bar N · beat N`. Loop a region: the playhead wraps.
4. **Space bar:** click a lane button (e.g. **M**), then press Space. The DAW transport toggles and the button doesn't change.
5. **Typing:** click the prompt, type text with Space and letters. Nothing reaches DAW shortcuts. Press Esc and then Space: the transport toggles again.
6. **Shape and commit:** mute and solo a lane; open the thread (the menu button), restore the first card, then Undo and Redo. Drag a lane's handle (⋮⋮) into the arrangement, then the toolbar's drag handle for all parts. Set **All parts** (the yellow pill) to "Bass only" and check MIDI out on another track.
7. **Resize:** drag the corner to a new size, close the window and reopen it. It comes back at the same size, and it can't be made smaller than 720×480. At 720×480 everything still fits: the thread opens over the lanes.
8. **State:** save the project, close it and reopen it. The idea, the thread and the window size come back.
9. **MIDI FX pass-through:** put **Flowstate MIDI FX** in front of an instrument (in Logic: the MIDI FX slot; in Bitwig or FL Studio: before the instrument, where the host allows it). Play the keyboard: the instrument still sounds, so notes pass through. **Ableton: n/a.** Ableton can't open a VST3 MIDI effect (it says "This VST3 plug-in could not be opened"); there, the instrument variant sends MIDI to other tracks through MIDI From.
10. **Multiple instances:** open two Flowstate windows at once. Both render, and each follows the transport.
11. **Library:** open **Library**, preview a GodFlow clip while the host plays (it plays in time), then **Use** it: it becomes the current idea, with "MIDI by GodFlow (flowknows) for Flowstate." under the lanes.
12. **Your own key (P1-12):** open Settings, choose a provider (say OpenRouter) and a model, **Use this model**, paste your key and **Save key**. The field empties and says a key is stored. Generate: the idea comes from that provider (the service's log line says `byok`). Check the key is in the keychain (macOS: Keychain Access, search "Flowstate"; Windows: Credential Manager → Windows Credentials, `Flowstate/openrouter`). Save and reopen the project: the key is still used, and it is not in the project file (search the `.als` or project folder for the key's first characters). **Remove key** deletes it from the keychain. With the service started as `FLOWSTATE_FEATURE_BYOK=0 npm run serve`, close and reopen the window: the key field is gone. On macOS, a second host app may ask once for access to the key; allow it.

13. **Use what I just played (P1-20):** with the host playing, play a 2- or 4-bar riff on a keyboard into Flowstate's track (a melody, then chords, then a bass line). Open **Use what I just played**, pick the bars and **Add drums**: the riff plays back at once exactly as you played it (same notes, timing and dynamics), with sketched drums, then the AI's drums replace the sketch. Try **Harmonize** on the melody and **Add bass** on the chords: the new part fits what you played. **Continue** makes a new idea in your riff's key. Asking **Harmonize** of played chords, or using it with nothing played, says why instead of generating.
14. **Component gallery (P1-8):** quit the DAW, then start it from a terminal with `FLOWSTATE_UI_PAGE=gallery.html` in its environment. On macOS, run the app's binary directly, because `open -a` doesn't pass the variable on: `FLOWSTATE_UI_PAGE=gallery.html "/Applications/Ableton Live 12 Suite.app/Contents/MacOS/Live"`. On Windows, run `set FLOWSTATE_UI_PAGE=gallery.html` in a Command Prompt, then start the DAW's `.exe` from that same prompt. With a CI build, `gallery.ps1` in the downloaded folder does this: `powershell -ExecutionPolicy Bypass -File .\gallery.ps1 -Daw "C:\...\daw.exe"`, or with no `-Daw`, the Standalone app. The plugin window shows the gallery instead of the Studio. Check:
    - It looks like the browser gallery (`npm run -w ui dev`, then `/gallery.html`) and like `docs/design/side-by-side-*.png`.
    - Tab reaches every control, with a cyan focus ring. On macOS, WKWebView follows the system setting: with **Keyboard navigation** off (System Settings → Keyboard), Tab reaches only text fields and Option-Tab reaches the rest.
    - Knobs turn with the arrow keys and by dragging. The settings sheet opens, Escape closes it, and focus returns to the button that opened it.
    - With a button focused, Space toggles the DAW transport and doesn't press the button.

Report anything that differs, with the host name and version, the OS and the build ID.

## 7. Sending a build to testers

Until the signed installers (P1-14) and the tester package (P1-16) exist, send the zip from `npm run pack:mac`. The repo is private, so testers can't download from Actions themselves.

1. Wait for a green `plugin` run on the commit you want (`npm run ci:status -- --watch`; `npm run ci:full` starts one if the push didn't).
2. `npm run pack:mac` (add `-- --branch main` if you're on another branch).
3. Send `dist/builds/flowstate-macos-<build id>.zip` by any means (AirDrop, Drive, email). Its `README.txt` has the install steps, a short checklist (checks 1–10 above, in plain words) and what to report, with the build ID filled in.
4. Record what comes back in `docs/host-checks.md`, with the build ID.

The tester README lives in `scripts/macos/README.txt`. Update it when section 6 changes. The pack script fills in `{{BUILD_ID}}`.
