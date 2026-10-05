# core

`flowstate_core` is Flowstate v2's music engine: a JUCE-free C++20 static library that realizes a
`flowstate.score.v0` score IR (see `../docs/ir-spec.md`, shape in `../schema/src/score.ts`) into
MIDI, deterministically. `fs-realize` is its command-line front end. It also reads MIDI back into
IR (`fs-analyze`, the analyzer behind the built-in library).

The library uses only the standard library (plus nlohmann/json, which stays inside the `.cpp`
files), so it can also build with Emscripten and MSVC. Randomness comes from an in-house SplitMix64
seeded per part, never from `<random>` distributions, so the same IR and seed produce byte-identical
output on every platform.

`sketchJson` (`flowstate/sketch.h`) writes the instant sketch: rule-based IR from the session context
alone, which the plugin plays the moment Generate is pressed while the model writes (roadmap P1-10).

## Build and test

From the repo root:

```sh
cmake -S core -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

FetchContent downloads nlohmann/json 3.12.0 and doctest 2.5.3 (header-only). Options:
`FLOWSTATE_CORE_BUILD_TESTS`, `FLOWSTATE_CORE_BUILD_CLI` (both ON).

## CLI

```sh
build/core/fs-realize --in score.json [--seed N] [--out-mid out.mid] \
    [--out-notes notes.json] [--out-report report.json] [--no-humanize]
```

- `out.mid`: SMF type 1 at 960 PPQ. Track 0 holds the title, tempo and time signature, then there
  is one track per part, named after the part. Drums are on channel 10; the other parts take
  channels 1, 2, ... and skip 10.
- `notes.json`: `{"ppq","bars","ticksPerBar","tempo","meter":[n,d],"parts":[{"id","role","name",
  "channel","notes":[{"tick","dur","pitch","vel","sublane"?}]}]}`. `channel` is 1-based. Drum parts
  also carry `voices`, the fixed voice → GM note → v1 sublane table.
- `report.json`: `{"warnings":[...],"outOfKey":[{"part","tick","pitch"}],"stats":{"totalNotes",
  "clipEndTick","parts":[{"id","role","notes","minPitch","maxPitch","rangeLow","rangeHigh",
  "outOfKey","sublanes"?}]}}`.

`fs-analyze` is the MIDI→IR analyzer behind the built-in library (`../docs/library.md`):

```sh
build/core/fs-analyze --in clip.mid [--lane chords|bass|melody|drums] [--key "Eb minor"] [--title T] \
    [--name N] [--style tag1,tag2] [--out-json analysis.json] [--out-mid normalized.mid]
```

It prints (or writes) the lane scores and evidence, the key or the reason it is blank, the grid, the
descriptors, the harmony it found, the score IR and the IR's fidelity to the original. `--out-mid`
writes the normalized clip. It exits with 3 when the clip can't be imported, with the reason in the
JSON (`error.code`, `error.message`).

The CLI exits with 1 and a clear message on invalid JSON, a wrong `ir` id, or missing required
structure. It exits with 2 on bad arguments. Recoverable problems (such as wrong-length step
strings, overlaps or unknown transforms) are repaired and listed as warnings.

## Module map

| Module | Responsibility |
| --- | --- |
| `ir.{h,cpp}` | IR types, JSON parsing with clamping and warnings, validation (form tiling, block overlaps) |
| `theory.{h,cpp}` | Note names, scales/modes, degree arithmetic, diatonic chords |
| `chord_parser.cpp` | Chord-symbol parser: qualities, 6/7/9/11/13, sus/add, alterations, `alt`, slash bass |
| `timing.{h,cpp}` | Time model: 960 PPQ, beat = denominator unit, bar/beat/step → tick |
| `steps.{h,cpp}` | Step strings: `\|` bars, spaces, repeat/pad/truncate repair, hold expansion |
| `groove.{h,cpp}` | SplitMix64, swing time-warp, energy/accent/ghost velocity, metric weights |
| `harmony.{h,cpp}` | Chord timeline in ticks (overlaps trimmed, gaps kept, implicit tonic when empty) |
| `voicing.{h,cpp}` | Voicing families and the Viterbi voice-leading search |
| `constraints.{h,cpp}` | Clip, range fold, dedupe, monophony, minimum length, out-of-key report |
| `realize.{h,cpp}` | Orchestration: per-part environment, literal notes, humanize, constraint pipeline, channels |
| `realize_chords.cpp` | Chords and pad: hits, re-strikes at chord changes, voice-led voicings |
| `realize_arp.cpp` | Arp patterns over a voice-led chord-tone pool |
| `realize_bass.cpp` | Bass tokens `R 3 5 7 8 a x X g` with low, smooth root anchoring |
| `realize_melody.cpp` | Motifs (degrees, alter, transforms, repeatEvery) and the rhythm-only sketch line |
| `realize_drums.cpp` | Drum lanes, GM mapping, sublanes, fills, groove dynamics |
| `density.cpp` | Part density and the density knob: thins or fills step patterns and motifs by metric weight |
| `transform.{h,cpp}` | The Studio's tweaks, IR JSON in and out: register, transpose, humanize, simplify, intensify, revoice |
| `smf.{h,cpp}` | Standard MIDI File writer |
| `output.{h,cpp}` | Note list and report JSON, golden checksum |
| `midi_read.{h,cpp}` | Standard MIDI File reader (type 0/1, PPQ), rescaled to 960 PPQ |
| `analyze.{h,cpp}` | MIDI→IR analyzer: profile and lane (v1's measures), key, grid, chord naming, IR per lane, fidelity, descriptors |
| `catalog.{h,cpp}` | Catalog search over library clips and AI results; key and tempo fit |

## Realization notes

- Pipeline for each part: role realizer (patterns thinned or filled by the part's density), then
  literal notes, range fold, exact dedupe, humanize (±8 ms timing, ±6 velocity at the default
  amount, scaled by the part's `humanize`, seeded; downbeat kick and snare get a quarter of the timing jitter),
  clip to `[0, bars × ticksPerBar)`, dedupe, monophony (bass, melody, counter), then a 1/64-note
  minimum length.
- Voicing: each family generates shapes, and every octave placement inside `[low, high]` becomes a
  candidate. A Viterbi pass then minimises total semitone movement, with extra weight on the top
  voice and penalties for muddy low intervals, and pulls the first chord to the centre of the range.
  Rootless voicings use A/B forms (3-5/13-7-9, 7-9-3-5/13). A slash bass goes under pad and chord
  voicings only when the range allows it; the bass part always uses the slash bass as its root.
- Swing is a time-warp inside each pair of steps, so onsets and note ends move together.
