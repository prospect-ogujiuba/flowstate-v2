# Spike 3: MIDI out and transport-locked audition

**Question:** can Flowstate play a realized clip through the user's own instruments, locked to the host transport, in the hosts that matter?

**Build:** the same `plugin/spike`, in two variants:

- **Instrument:** `IS_SYNTH`, `NEEDS_MIDI_OUTPUT`. It plays the clip through MIDI out, plus a sine preview voice so it's audible with zero routing.
- **AU MIDI FX:** `IS_MIDI_EFFECT` (Logic's MIDI FX slot). It plays the clip into the track's own instrument.

The spike loads a clip from a `*.notes.json` file (the `fs-realize --out-notes` format) and loops it over its length. Playback is scheduled from `PositionInfo` PPQ in `processBlock`, from a pre-rendered note list, with no allocation.

## Checks

| # | Check | Pass when |
| --- | --- | --- |
| 1 | Sync | Loop start lines up with bar 1 of the host; stays locked across 100 bars at 60–180 BPM |
| 2 | Loop and seek | Host loop and playhead jumps re-sync on the next block; no stuck notes |
| 3 | Stop | Transport stop sends note-offs; nothing hangs |
| 4 | Routing (Ableton) | A second MIDI track set to "MIDI From: Flowstate" plays the clip through its instrument |
| 5 | Logic | The MIDI FX variant drives the track's instrument; the instrument variant's preview voice is audible |
| 6 | Record | Recording the MIDI into a clip produces the same notes |
| 7 | Tempo change | A tempo automation ramp keeps notes on the grid |

Record results in `docs/spikes/results.md`.
