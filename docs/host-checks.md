# Host check results: the product plugin

Results of the manual DAW checks in `testing-plugin.md` (section 6), per host and build. `pass`, `fail` (see the notes), `n/a` (the host can't do it), or empty (not run yet). The Phase 0 spike results are in `spikes/results.md`.

| # | Check | Ableton Live 12 (VST3, Windows) | Ableton Live 12 (VST3, macOS) | Logic Pro (AU) | FL Studio or Bitwig (VST3, Windows) |
| --- | --- | --- | --- | --- | --- |
| 1 | Loads | pass | | | |
| 2 | Follows the host (tempo, meter) | pass | | | |
| 3 | Transport (play, stop, loop) | pass | | | |
| 4 | Space bar reaches the DAW | pass | | | |
| 5 | Typing doesn't trigger DAW shortcuts | pass | | | |
| 6 | Prompt reply (service not connected) | pass | | | |
| 7 | Resize, persists, 720×480 minimum | pass | | | |
| 8 | State survives save and reopen | pass | | | |
| 9 | MIDI FX pass-through | n/a (note 1) | | | |
| 10 | Multiple instances | pass | | | |
| 11 | Drag disabled with no idea | pass | | | |
| 12 | Component gallery (P1-8) | pass (note 2) | | | |

## Environment

| Host | Version | OS | Build | Tester | Date |
| --- | --- | --- | --- | --- | --- |
| Ableton Live 12 Suite | 12.4.6 | Windows 11 | `1b76d05-5` (CI run 36793886874) | owner | 2026-10-01 |

## Notes

1. Ableton lists **Flowstate MIDI FX** (VST3) as an audio effect ("Insert audio effects after instruments"), then shows "This VST3 plug-in could not be opened" and "Some plug-ins are disabled". The MIDI FX variant has no audio buses, by design for Logic's MIDI FX slot. Ableton has no slot for third-party MIDI effects and doesn't open a VST3 without audio buses. pluginval and the host smoke test load it fine. In Ableton, the instrument variant (**Flowstate**) is the one to use: it sends MIDI to other tracks through **MIDI From** (spike 3, check 4). Open question: whether to stop installing the MIDI FX VST3 on Windows, or keep it for hosts with MIDI effect slots (Bitwig, FL Studio). Decide in P1-7 (MIDI out) or P1-14 (installers).
2. Gallery (Windows, Ableton): all sub-checks pass: the look, the Tab walk with the focus ring, knobs by keys and drag, the settings sheet's Escape and focus return, and Space reaching the DAW with a button focused.
