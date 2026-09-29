# Flowstate Spike (Phase 0)

A throwaway JUCE 9.0.2 plugin that answers two questions before Phase 1 commits to an architecture:

- **Spike 2, WebView host** (`docs/spikes/webview-host.md`): can a `juce::WebBrowserComponent` UI behave like a first-class plugin UI (typing, Space bar, drag-out, resize, several instances, bridge latency) in Ableton Live 12, Logic Pro and FL Studio or Bitwig?
- **Spike 3, MIDI out** (`docs/spikes/midi-out.md`): can a realized clip play through the user's own instruments, locked to the host transport, from an instrument with MIDI out and from an AU MIDI FX?

Results go in `docs/spikes/results.md`.

## What's in it

| Target | Kind | Formats | Codes |
| --- | --- | --- | --- |
| `FlowstateSpike` ("Flowstate Spike") | Instrument: `IS_SYNTH`, MIDI in + out, stereo preview voice | VST3, AU, Standalone | `Flws` / `Fsp1`, `com.flowstate.spike.instrument` |
| `FlowstateSpikeMidiFx` ("Flowstate Spike MIDI FX") | `IS_MIDI_EFFECT`, MIDI only, no audio buses | AU (macOS), VST3 | `Flws` / `Fspm`, `com.flowstate.spike.midifx` |

Both are built from the same sources:

| File | Role |
| --- | --- |
| `src/AuditionScheduler.*` | Pure C++ (no JUCE). Pre-renders a clip into a sorted note list and schedules each block from host PPQ: loops over the clip length aligned to bar 1 (PPQ 0), sample-accurate offsets, seek/loop detection with all-notes-off, note-offs on stop, host loops that wrap inside a block, no stacked note-ons. |
| `src/ClipFile.*` | `*.notes.json` parsing and validation, conversion to the audition list, `.mid` export (`juce::MidiFile`), JSON for the page. |
| `src/PluginProcessor.*` | Owns all state. Lock-free hand-off of the rendered clip to the audio thread (atomic pointer exchange, retired buffers freed by a message-thread timer). Publishes transport atomics for the UI. State: clip path, preview toggle, editor size. |
| `src/PreviewSynth.h` | `juce::Synthesiser` preview: triangle voices for tonal channels, kick/noise one-shots for channel 10. Instrument only; "Preview sound" toggle. |
| `src/WebEditor.cpp` | `WebBrowserComponent` editor (macOS WKWebView, Windows WebView2). Page served from `juce_add_binary_data` through the resource provider. |
| `src/FocusRelease_*.{mm,cpp}` | Hands OS keyboard focus back to the host view (Space bar). |
| `src/HeadlessEditor.cpp` | Native stub editor for the Linux compile check (`JUCE_WEB_BROWSER=0`). It also works as the "native drag handle" fallback from the brief. |
| `ui/` | `index.html`, `app.js`, `pianoroll.js`, `style.css`: no build step, no framework. `sample.notes.json`: the default clip (4 bars of Am–F–C–G keys, bass and drums at 100 BPM). JUCE's own `webview-interop` JS is bundled from the JUCE checkout as `juce_interop.js`. |
| `tests/scheduler_test.cpp` | Scheduler behaviour tests (plain C++, no JUCE). |
| `tests/host_smoke.cpp` | Loads the built VST3s through JUCE's VST3 host and drives a fake transport over 100 bars, a seek and a stop. |

### The page

Status bar (host BPM, PPQ, bar.beat, time signature, loop, sounding notes, detected jumps), a text field, **Load clip…** (native `FileChooser` for `*.notes.json`), **Sample**, **Preview sound** (instrument only), **Play (internal)** (only shown when the host provides no transport, e.g. Standalone), the **Drag MIDI** handle, **Bridge latency** (50 sequential JS → C++ → JS round trips; it shows mean/median/p95/max) and a canvas piano roll with a moving playhead.

Native functions: `getInitialState`, `loadClip`, `loadSample`, `startDrag`, `ping`, `setPreview`, `setInternalPlay`, `submitText`, `releaseFocus`. C++ → JS events: `status` (30 Hz, from a `juce::Timer` reading atomics), `clip`, `error`.

### Threading

As in `docs/threading.md`: the audio thread reads the playhead, schedules from the pre-rendered list and writes MIDI with no allocation, locks, I/O or logging (the MIDI scratch buffer is reserved in `prepareToPlay`). The one exception is the preview: `juce::Synthesiser` takes its internal lock during rendering. Only the audio thread touches it after `prepareToPlay`, so the lock is never contended. Loading, rendering, MIDI export and state restore run on the message thread. `setStateInformation` from another thread defers the clip load with `AsyncUpdater`. Closing the editor changes nothing.

### Keyboard: typing vs. the DAW's Space bar

JUCE 9's `WebBrowserComponent` has **no key-forwarding option**. `juce_WebBrowserComponent_mac.mm` only maps Cmd+X/C/V/A/Z in `performKeyEquivalent:`, and the WebView2 backend only bridges focus traversal. So the spike does this:

1. While the text field has focus, it consumes keys normally, so the WebView reports them as handled and the host doesn't act on them (check 2).
2. With no editable element focused, the page **never calls `preventDefault`** and **can't scroll** (`overflow: hidden`), because a scroll counts as a handled key. `<button>`s never take focus (`mousedown` → `preventDefault`), because a focused button turns Space into a click.
3. On Space with nothing focused, on Esc in the field, and when the field blurs, the page calls `releaseFocus`. C++ unfocuses the JUCE components and moves OS focus to the host's view: `makeFirstResponder:` on the superview, or else the window, on macOS; `SetFocus(GetParent(hwnd))` on Windows.
   - macOS: WKWebView passes unhandled key events up the responder chain, so Space should reach the host even before focus is released.
   - Windows: WebView2 swallows unhandled keys. The first Space after clicking into the page may be lost, and later presses go to the host. If that isn't good enough, the fallback is JUCE-side key forwarding (from the brief).
4. The CMake option `FLOWSTATE_SPIKE_EDITOR_WANTS_KEYBOARD_FOCUS` (default `OFF`) sets JUCE's `EDITOR_WANTS_KEYBOARD_FOCUS`. If typing doesn't reach the field in a host, rebuild with `ON` and record the result. It trades against the Space bar.

### Drag-out

On `pointerdown` the page calls `startDrag`. C++ writes the clip to `<temp>/Flowstate Spike/<clip>.mid` (type 1, one track with tempo, time signature and all parts on their own channels, 960 PPQ, end-of-track at the clip length), then calls `DragAndDropContainer::performExternalDragDropOfFiles` with the editor as the source component. The drag has to start while the button is still down.

- macOS: JUCE uses `[[view window] currentEvent]`, which here is whatever event was current when the WKWebView script message arrived.
- Windows: JUCE runs OLE `DoDragDrop` on a worker thread.

Both are exactly what check 4 tests. The file is left in place, because some DAWs read it lazily after the drop.

## Build locally

Requirements: CMake 3.22+, Ninja (or Xcode / Visual Studio) and a C++20 compiler. JUCE 9.0.2 is fetched by CMake. To use a local checkout instead, pass `-DFLOWSTATE_JUCE_DIR=/path/to/JUCE`.

### macOS

```sh
cmake -S plugin/spike -B build/plugin-spike -G Ninja -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build/plugin-spike
ctest --test-dir build/plugin-spike --output-on-failure
```

Or use `-G Xcode` and `cmake --build build/plugin-spike --config Release`. JUCE ad-hoc signs the bundles.

### Windows (x64)

From an "x64 Native Tools Command Prompt for VS 2022":

```bat
cmake -S plugin/spike -B build/plugin-spike -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugin-spike
ctest --test-dir build/plugin-spike --output-on-failure
```

Or use `-G "Visual Studio 17 2022" -A x64` and `--config Release`. Configure downloads the `Microsoft.Web.WebView2` 1.0.3485.44 NuGet package (the version JUCE 9.0.2's `FindWebView2.cmake` names) into the build tree, and the plugin links `WebView2LoaderStatic.lib` (`JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1`), so there's no loader DLL to ship. To work offline, pass `-DFLOWSTATE_WEBVIEW2_PACKAGE_LOCATION=<dir containing Microsoft.Web.WebView2.*>`. At runtime the Evergreen WebView2 Runtime must be installed (it ships with Windows 11 and updated Windows 10). Without it, JUCE silently falls back to the IE backend, which has no resource provider, and the page won't load.

### Linux (compile check only)

There's no WebKitGTK here, and Linux isn't a target host. `FLOWSTATE_SPIKE_HEADLESS_CHECK` (default `ON` on Linux) builds both plugins with `JUCE_WEB_BROWSER=0` and the native stub editor, plus the tests:

```sh
cmake -S plugin/spike -B build/plugin-spike -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DFLOWSTATE_JUCE_DIR=/path/to/JUCE        # optional
cmake --build build/plugin-spike
ctest --test-dir build/plugin-spike --output-on-failure   # scheduler + VST3 host smoke
```

To build only the scheduler tests, with no JUCE at all, pass `-DFLOWSTATE_SPIKE_BUILD_PLUGIN=OFF`.

Artefacts land in `build/plugin-spike/<Target>_artefacts/Release/{VST3,AU,Standalone}/`.

## Get CI builds

`.github/workflows/plugin-spike.yml` runs on pushes that touch `plugin/**`, and on demand (Actions → plugin-spike → Run workflow). It builds on `macos-14` (universal arm64 + x86_64) and `windows-2022` (x64), runs the scheduler tests and the VST3 host smoke test, and uploads:

- `flowstate-spike-macos-universal`: zipped `Flowstate Spike.vst3`, `.component` and `.app`, plus `Flowstate Spike MIDI FX.vst3` and `.component`, all ad-hoc signed (`ditto` zips keep bundle permissions). The job also runs `auval` for information.
- `flowstate-spike-windows-x64`: `VST3/Flowstate Spike.vst3`, `VST3/Flowstate Spike MIDI FX.vst3` and `Standalone/Flowstate Spike.exe`.

Both include `sample.notes.json` and `COMMIT`. To download from the command line:

```sh
gh run list --workflow plugin-spike.yml
gh run download <run-id> --name flowstate-spike-macos-universal
```

## Install

| OS | Format | Path |
| --- | --- | --- |
| macOS | VST3 | `~/Library/Audio/Plug-Ins/VST3/` (or `/Library/Audio/Plug-Ins/VST3/`) |
| macOS | AU (instrument and MIDI FX) | `~/Library/Audio/Plug-Ins/Components/` |
| macOS | Standalone | anywhere, e.g. `/Applications/` |
| Windows | VST3 | `C:\Program Files\Common Files\VST3\` (copy the whole `.vst3` folder) |
| Windows | Standalone | anywhere |

**macOS Gatekeeper:** the builds are ad-hoc signed and not notarized, and until real signing exists, browsers quarantine them. After unzipping, run:

```sh
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/Flowstate Spike.component"   # repeat per bundle
killall -9 AudioComponentRegistrar 2>/dev/null; auval -a | grep -i flowstate   # AU rescan
```

Then rescan in the host: in Logic, use Plug-in Manager → Reset & Rescan Selection; in Live, turn VST3 plug-ins off and on in Settings → Plug-ins.

## Test procedure

Run every check in both briefs in each host, and fill in `docs/spikes/results.md` (host × check table, with the build's CI run or commit and notes on workarounds). The sample clip loads by default, so there's nothing to set up.

Spike 2 (`docs/spikes/webview-host.md`), with the instrument on a MIDI track:

1. **Load and scan**: scan, insert, open and close the editor 5 times, then remove the plugin and reinsert it.
2. **Typing**: click the text field and type text that includes DAW shortcut keys (Space, letters, numbers, Tab). Nothing may trigger in the DAW. **Send** echoes it from C++.
3. **Space bar**: click the piano roll (not the field), then press Space: the transport starts and stops. Also press Esc in the field, then Space. On Windows, note whether the first press is lost.
4. **Drag-out**: drag **Drag MIDI** onto an empty MIDI track at bar 3. The clip must land at the drop position with 4 bars and 3 channels of notes.
5. **Resize**: drag the corner (the minimum is 720 × 480), then check crispness on a Retina / 150–200 % display.
6. **Instances**: open 3 instances with their editors open, check responsiveness, and read memory per instance (Activity Monitor / Task Manager: host before and after).
7. **Bridge latency**: click **Bridge latency** and record the mean (the pass is under 5 ms).

Spike 3 (`docs/spikes/midi-out.md`):

1. **Sync**: loop the sample over 100 bars at 60, 120 and 180 BPM against a host metronome or click. The kick and crash must land on bar 1.
2. **Loop and seek**: set a host loop across a bar line, jump the playhead while playing, and watch `active` / `jumps` in the status bar. No note may hang.
3. **Stop**: stop mid-chord. Everything goes silent and `active` drops to 0.
4. **Routing (Ableton)**: on a second MIDI track with an instrument, set MIDI From: the Flowstate track, "Flowstate Spike" (post-FX) and monitor In.
5. **Logic**: put the MIDI FX variant in a software instrument track's MIDI FX slot and play: the track's instrument plays the clip. Separately, put the instrument variant on a track and confirm the preview voice is audible.
6. **Record**: record the routed MIDI into a clip and compare it with the dragged `.mid`. The notes must match.
7. **Tempo change**: automate a tempo ramp (e.g. 80 → 160 over 16 bars). Notes must stay on the grid.

Known limitations:

- Ableton has no third-party MIDI-effect slot, so the VST3 MIDI FX build is only useful in hosts that route VST3 MIDI output (e.g. Bitwig or Reaper).
- Logic ignores MIDI output from AU instruments, which is why the MIDI FX variant exists.
- A note that has already started when you seek into it isn't retriggered. It plays again on the next loop.
