# Spike results: WebView host (spike 2) and MIDI out (spike 3)

Build under test: `plugin/spike` (see `plugin/spike/README.md` for the procedure). Record the CI run or commit in each row's notes.

Legend: `pass`, `fail`, `partial` (explain in notes), `n/a`, blank = not run.

## Spike 2: WebView UI inside real hosts (`docs/spikes/webview-host.md`)

| # | Check | Ableton Live 12 (VST3, macOS) | Ableton Live 12 (VST3, Windows) | Logic Pro (AU) | FL Studio (VST3) | Bitwig (VST3) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Load and scan | | | | | |
| 2 | Typing | | | | | |
| 3 | Space bar | | | | | |
| 4 | Drag-out | | | | | |
| 5 | Resize / HiDPI | | | | | |
| 6 | Three instances, < 150 MB each | | | | | |
| 7 | Bridge latency < 5 ms (value) | | | | | |

## Spike 3: MIDI out and transport-locked audition (`docs/spikes/midi-out.md`)

| # | Check | Ableton Live 12 (VST3, macOS) | Ableton Live 12 (VST3, Windows) | Logic Pro (AU instrument) | Logic Pro (AU MIDI FX) | FL Studio / Bitwig |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Sync (100 bars, 60–180 BPM) | | | | | |
| 2 | Loop and seek | | | | | |
| 3 | Stop | | | | | |
| 4 | Routing (Ableton "MIDI From") | | pass | n/a | n/a | |
| 5 | Logic (MIDI FX drives instrument; preview audible) | n/a | n/a | | | n/a |
| 6 | Record | | | | | |
| 7 | Tempo change | | | | | |

## Environment

| Host | Version | OS / CPU | Plugin build (CI run / commit) | Tester | Date |
| --- | --- | --- | --- | --- | --- |
| Ableton Live 12 | 12 | Windows x64 | plugin-spike run 36504878591 (commit 7a68147) | owner | 2026-09-29 |

## Notes and workarounds

- Ableton routing (Windows): the receiving track's MIDI From second dropdown must be set to the plugin entry ("Flowstate Spike"), not "Post FX". "Post FX" carries the MIDI going into the source track's devices, not the plugin's output. Onboarding and docs must say this explicitly.
