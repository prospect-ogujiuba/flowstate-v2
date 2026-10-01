# Score IR — `flowstate.score.v0`

The score IR is Flowstate's central contract. The model writes it, `core` realizes it into notes, the UI edits it, and sessions persist it. It is symbolic and musical: it describes harmony, motifs, rhythm and patterns, not raw note events (literal notes are an escape hatch).

The machine-readable source of truth is `schema/score.ts` (a Zod schema). `schema/score.v0.schema.json` is generated from it. This document explains the semantics.

`v0` means spike-grade: fields may change until Gate A passes. After that, changes are versioned.

## Design rules

1. **The model owns musical choices; the realizer owns correctness.** Key, meter, length and range are enforced by `core`, never trusted from the IR.
2. **Structured-output friendly.** Only objects with fixed keys, arrays, enums, strings, numbers and booleans. No maps keyed by data, and no recursion.
3. **Text patterns for rhythm.** Rhythm is written as step strings (`"x..x..x."`), because models write and edit them reliably and people can read them.
4. **Deterministic.** The same IR and seed always realize to identical notes.

## Time model

- Bars and beats are **1-based** in the IR: bar 1, beat 1 is the clip start.
- A beat is the meter's denominator unit: a quarter note in 4/4, an eighth note in 6/8.
- `core` works in ticks at **960 PPQ**. A 6/8 bar is 6 eighth notes = 3 quarter notes = 2880 ticks.
- A clip is half-open: `[0, bars × ticksPerBar)`. Notes are clipped to the clip end, never wrapped around (this is v1's timing contract).
- **Step strings:** a part's `grid` is the number of steps per beat (4 = sixteenths in 4/4, 3 = triplet eighths, 2 = eighths). One bar of steps has `beatsPerBar × grid` characters. `|` separates bars in a multi-bar pattern. A pattern shorter than its block repeats. Spaces are ignored, so they can be used for readability.

## Top level

| Field | Type | Meaning |
| --- | --- | --- |
| `ir` | `"flowstate.score.v0"` | Schema id |
| `title` | string | Short name for the idea ("Dusty neo-soul loop") |
| `context` | Context | Tempo, meter, key, length, feel |
| `form` | Section[] | Sections with an energy level for each |
| `harmony` | string (or Chord[]) | The chord timeline shared by every part |
| `motifs` | Motif[] | Reusable melodic cells |
| `parts` | Part[] | The instruments and what they play |

### Context

| Field | Type | Meaning |
| --- | --- | --- |
| `tempo` | number | BPM (informational; the host tempo wins in the plugin) |
| `meterNumerator`, `meterDenominator` | int | e.g. 4/4, 3/4, 6/8, 7/8 |
| `tonic` | enum | `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B` |
| `mode` | enum | `major minor dorian phrygian lydian mixolydian locrian harmonic_minor melodic_minor major_pentatonic minor_pentatonic blues` |
| `bars` | int | Clip length in bars. Authoritative. |
| `swing` | number | 0 = straight, 0.5 = heavy swing, applied to off-beat steps |
| `style` | string[] | Free tags ("neo-soul", "lofi", "trap"), used to pick groove templates |

### Section (`form`)

`{ name, startBar, bars, energy }`. `energy` ranges from 0 to 1. It scales velocity, and it scales density for parts with `density: "auto"`. Sections must tile `1..bars` without gaps. When `form` is empty, the realizer treats the whole clip as one section at energy 0.6.

### Chord (`harmony`)

Write `harmony` as a compact string of chords, played one after another from bar 1, beat 1:

```
"harmony": "Dm9:4 | G13:4 | Em7:4 | Fmaj7:2 A7#9:2"
```

- Each token is `<symbol>:<beats>`, with the duration as a decimal or a fraction. `r:<beats>` is a stretch with no chord. Tokens are separated by spaces.
- Positions are implied: each chord starts where the previous token ended. With 4 beats per bar, `Fmaj7:2 A7#9:2` in the fourth bar puts A7#9 on beat 3.
- A `|` token is a visual mark (e.g. at a bar line) and has no timing effect.
- The total must not run past the clip (`bars × meterNumerator` beats).

**Older form**, still accepted: an array of `{ bar, beat, beats, symbol }`, with `bar` and `beat` 1-based. The two forms realize identically.

- `symbol` is a chord symbol: root, then quality, then extensions and alterations, then an optional slash bass. Examples: `C`, `Cm`, `C7`, `Cmaj7`, `Cm7`, `Cm9`, `C9`, `C11`, `C13`, `Cm11`, `Cdim`, `Cdim7`, `Cm7b5`, `Caug`, `Csus2`, `Csus4`, `C7sus4`, `C6`, `Cm6`, `C69`, `Cadd9`, `C7b9`, `C7#9`, `C7#11`, `C7b13`, `C5`, `C/E`, `Fm9/Ab`.
- A chord lasts `beats` beats. Gaps (rests in the string) mean no chord, so chord parts rest. Overlaps are an error; the later chord wins and a warning is logged.

### Motif

`{ id, notes }`. Write `notes` as a compact string of tokens, played one after another:

```
"notes": "5:.75! 4:.25 b3:1 | r:.5 1+:1/3 #2-:1/3 r:1/3 8:1.5"
```

- Each token is `<pitch>:<beats>`, with an optional `!` for an accent (raises velocity). Tokens are separated by spaces.
- `<pitch>` is a 1-based scale degree of the current mode (1 = tonic), or `r` for a rest. Degrees above 7 or below 1 wrap into neighbouring octaves (8 = tonic, one octave up; 0 = the 7th degree, one octave down).
  - A `b` or `#` prefix alters the degree by a semitone, for chromatic notes that are intended.
  - Each trailing `+` or `-` moves it an octave up or down from the part's register centre (`5+`, `1--`).
- `<beats>` is the duration in beats: a decimal (`1`, `.5`, `1.5`) or a fraction (`1/3` for a triplet).
- Onsets are implied: each note starts where the previous token ended. Leave gaps with rests. Notes in a motif don't overlap.
- A `|` token is a visual mark (e.g. at a bar line) and has no timing effect.

**Older form**, still accepted: `notes: MotifNote[]` with `MotifNote = { degree, octave, alter, beat, beats, accent }`. `beat` is the onset from the motif start (0-based) and `alter` is −1, 0 or +1. The two forms realize identically.

## Part

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | Unique per score (`"keys"`, `"bass"`) |
| `role` | enum | `chords pad arp bass melody counter drums` |
| `name` | string | Display name / instrument hint ("Rhodes", "808") |
| `low`, `high` | note name | Register range (`"C2"`, `"G5"`); C4 = MIDI 60 |
| `grid` | int | Steps per beat for this part's step strings |
| `velocity` | int | Base velocity 1–127 before energy and accents |
| `blocks` | Block[] | What the part plays over bar ranges; blocks must not overlap |

### Block

A block covers `startBar..endBar` (inclusive). Only the fields that matter for the part's role are read. Leave the others out; a field that is absent and one that is `null` mean the same.

| Field | Roles | Meaning |
| --- | --- | --- |
| `startBar`, `endBar` | all | Bar range, inclusive |
| `rhythm` | chords, pad, arp, bass, melody, counter | Step string: see tokens below |
| `voicing` | chords, pad | `close open drop2 drop3 rootless shell spread quartal power` |
| `arpPattern` | arp | `up down updown random chord_tones` |
| `motif` | melody, counter | Id of the motif to play |
| `transforms` | melody, counter | Applied in order: `transpose:+2` (degrees), `invert`, `retrograde`, `displace:+0.5` (beats), `augment`, `diminish`, `octave:+1` |
| `repeatEvery` | melody, counter | Motif repeat period in beats; 0 = once at the block start |
| `groove` | drums | A named groove from the table below: an idiomatic pattern with its own feel. Lanes in `drums` replace the groove's lane for the same voice, so you can keep the groove and rewrite one voice. |
| `drums` | drums | `[{ voice, steps }]`: one step string per voice |
| `fill` | drums | `none snare_roll tom_run kick_build crash_end half_time_break`: a fill in the block's last bar |
| `notes` | any | Literal notes `[{ bar, beat, beats, pitch, velocity }]`. They are added on top of the block content, then quantized and range-checked. `pitch` is a note name. |
| `articulation` | pitched | `legato normal staccato` |

### Step tokens

| Token | chords / pad / arp | bass | drums | melody / counter (with `rhythm`) |
| --- | --- | --- | --- | --- |
| `.` | rest | rest | rest | rest |
| `-` | hold previous | hold previous | — | hold previous |
| `x` | hit | chord root | hit | next scale step in the line |
| `X` | accented hit | accented root | accent | accented next step |
| `g` | — | ghost root | ghost note | — |
| `R` `3` `5` `7` `8` | — | root, third, fifth, seventh, octave of the current chord | — | chord tone |
| `a` | — | chromatic approach into the next chord's root | — | — |

For melody, if `motif` is set, `rhythm` is ignored and the motif's own rhythm is used. If only `rhythm` is set, the realizer draws a stepwise line over chord tones. That is the instant-sketch path.

### Drum voices

`kick snare clap rim closed_hat pedal_hat open_hat low_tom mid_tom high_tom crash ride ride_bell shaker tambourine cowbell`. These map to General MIDI on channel 10. They also map to v1's seven sublanes (kick, snare, clap_rim, hats, toms, cymbals, aux_kit) for per-sublane export.

### Grooves

A drums block can call a groove by name instead of (or as well as) writing every lane. Each groove is for one meter; in another meter it is ignored. Prefer the groove that fits the style, then vary it: rewrite a lane in `drums`, add a `fill`, or use another groove in a later block for a new section.

| Groove | Meter | What it plays |
| --- | --- | --- |
| `four_on_floor` | 4/4 | House: kick on every beat, clap on 2 and 4, off-beat open hats, 16th shaker |
| `tech_house` | 4/4 | Tech house: four-on-the-floor kick, clap on 2 and 4, rolling 16th hats, syncopated rim |
| `boom_bap` | 4/4 | Boom bap: swung kick pattern, laid-back snare on 2 and 4, accented 8th hats |
| `lofi` | 4/4 | Lofi: sparse, lazy boom bap; soft kick, late snare, ghosted hats |
| `trap` | 4/4 | Trap: half-time snare on 3, sparse kicks, 8th hats with 16th rolls |
| `drill` | 4/4 | UK drill: half-time snare on 3 with a late extra snare, sliding kicks, skippy hats |
| `dembow` | 4/4 | Reggaeton dembow: kick on every beat, snare on the 3-3-2 dembow accents, 8th hats |
| `afrobeats` | 4/4 | Afrobeats: tresillo kick, rim clave, soft clap on 2 and 4, busy 16th shaker, conga-style toms |
| `funk` | 4/4 | Funk: syncopated kick, backbeat snare with ghost notes, 16th hats, open hat on the and of 4 |
| `rock` | 4/4 | Rock: kick on 1 and 3 with pushes, snare on 2 and 4, 8th hats, crash on the first downbeat |
| `pop` | 4/4 | Pop: kick on 1 and the and of 2 and 3, clap and tambourine on 2 and 4, 8th hats |
| `ballad` | 4/4 | Ballad: soft kick on 1 and 3, cross-stick on 2 and 4, gentle 8th hats |
| `neo_soul` | 4/4 | Neo-soul: behind-the-beat pocket, late snare and hats, ghosted 16ths |
| `dnb` | 4/4 | Drum and bass: two-step kick and snare with a ghost, 8th hats, 16th shaker |
| `half_time` | 4/4 | Half-time: kick on 1, snare on 3, 8th hats; slow hip hop and builds |
| `cinematic_toms` | 4/4 | Cinematic: big low-tom ostinato over kick on 1 and 3, crash on the first downbeat |
| `sparse_pulse` | 4/4 | Ambient: a soft pulse; kick on 1, ride quarter notes, light shaker |
| `jazz_swing` | 4/4 | Jazz swing (triplet grid): spang-a-lang ride, hi-hat foot on 2 and 4, feathered kick |
| `brush_swing` | 4/4 | Brushes (triplet grid): swirling brush snare, feathered kick, hi-hat foot on 2 and 4 |
| `jazz_waltz` | 3/4 | Jazz waltz (triplet grid): skipping ride, hi-hat foot on 2 and 3, feathered kick |
| `waltz` | 3/4 | Waltz: kick on 1, cross-stick on 2 and 3, 8th hats |
| `six_eight` | 6/8 | 6/8: kick on 1, snare on 4, every eighth on the hats; ballads and folk |
| `gospel_shuffle` | 6/8 | Gospel 6/8: kick on 1 with a push, snare backbeat on 4 with a ghost, accented hats |

## Realization (what `core` guarantees)

1. The output is exactly `bars` long, with every note inside the clip.
2. Pitched notes stay inside `[low, high]`. Out-of-range notes are folded by octave.
3. `bass`, `melody` and `counter` are monophonic: an overlapping note is truncated at the next onset.
4. Pitches fit the key and mode unless they come from a chord symbol, an `alter`, or a literal note. The report lists every out-of-key note.
5. Chord voicings minimise voice movement between consecutive chords within the chosen voicing family.
6. `swing` delays off-beat steps. Humanization (timing ±, velocity ±) comes from the seed. The same IR and seed always give the same notes.
7. Output formats: SMF type 1 with one track per part (drums on channel 10), plus a JSON note list and a realization report (warnings, adherence measures).

## Resolved details (as implemented in `core`)

These are the behaviours that matter when writing an IR. The full list is in `core/README.md`.

- **Bar length:** a bar should have `beatsPerBar × grid` steps. A bar whose length is a whole multiple or divisor of that (up to 8 times as long) is a resolution: its steps are spread evenly over the bar, as if the part's grid were that fine. `"x...x..."` on a 16-step bar is a hit on beats 1 and 3, and 32 steps on a 16-step bar are 32nd notes. Other short bars are padded with rests and other long bars truncated. Unknown tokens become rests. Each case warns. Without `|`, the string is cut into bar-sized chunks. Holds (`-`) continue across bar lines.
  - **Update 2026-10-01:** a short bar whose length divided the bar used to be tiled (`"x..."` over 16 steps became 4 repeats), and long bars were always truncated. Every model tested wrote half- and double-resolution bars meaning the same rhythm, so they are now read that way. An older score that relied on tiling plays differently.
- **Swing:** delays off-beat steps by `swing × one step length` (0 to 0.75). Only even grids swing; triplet grids never do. Motif notes swing too; literal notes don't.
- **Energy:** velocity × `1 + (energy − 0.6) × 0.5`, clamped to 0.6–1.25.
- **Chords and pads:** a held hit that crosses a chord change is re-struck at the change. Holds stop at chord gaps. With no rhythm, each chord is struck once and held. Default voicing is `close` for chords and `open` for pads. If a voicing doesn't fit the range, the next fallback is tried, then close, with a warning.
- **Bass:** `7` on a chord without a 7th uses the scale's 7th. `a` approaches the next chord's root by a semitone; after the last chord it approaches the first (the clip loops). A slash bass is the root for `R x X g`.
- **Motifs:** in 5- and 6-note scales, degrees wrap by the scale size. In the compact string, a token that can't be read is skipped with a warning; one with a readable duration but an unknown pitch becomes a rest of that length. `augment` and `diminish` take an optional factor (`augment:1.5`). A motif that leaves the range is shifted whole by octaves first, so its contour survives.
- **Fills** cover the last `max(1, numerator/2)` beats of the block's last bar (`half_time_break` covers the whole bar) and replace the snare, tom and hat hits there.
- **Drums** ignore `low`/`high`. `-` in a drum lane is a rest.
- **Literal notes:** `bar` is the absolute clip bar. Timing is exact, not quantized.
- **Overlapping blocks:** the later block in IR order owns the shared bars. On overlapping chords, the later onset wins.
- **Harmony:** empty harmony means an implicit tonic chord, with a warning. `C2` is read as Csus2 and `C4` as Csus4. Dominant and major `11` chords are voiced without the 3rd.
- **Out of key:** tones from chord symbols, `alter`, literal notes and bass approaches count as intentional and are not reported.

## Example

```json
{
  "ir": "flowstate.score.v0",
  "title": "Late-night dorian loop",
  "context": { "tempo": 88, "meterNumerator": 4, "meterDenominator": 4, "tonic": "D", "mode": "dorian", "bars": 4, "swing": 0.15, "style": ["neo-soul"] },
  "form": [ { "name": "A", "startBar": 1, "bars": 4, "energy": 0.55 } ],
  "harmony": "Dm9:4 | G13:4 | Em7:4 | Fmaj7:2 A7#9:2",
  "motifs": [ { "id": "m1", "notes": "5:.75! 4:.25 3:1 r:.5 1:1.5" } ],
  "parts": [
    { "id": "keys", "role": "chords", "name": "Rhodes", "low": "A2", "high": "D5", "grid": 4, "velocity": 76,
      "blocks": [ { "startBar": 1, "endBar": 4, "rhythm": "x--- ..x- .x-- ....", "voicing": "rootless" } ] },
    { "id": "bass", "role": "bass", "name": "Finger bass", "low": "E1", "high": "G2", "grid": 4, "velocity": 90,
      "blocks": [ { "startBar": 1, "endBar": 4, "rhythm": "R--- ..5. R-.. ..a." } ] },
    { "id": "lead", "role": "melody", "name": "Flute", "low": "D4", "high": "A5", "grid": 4, "velocity": 84,
      "blocks": [ { "startBar": 1, "endBar": 2, "motif": "m1", "repeatEvery": 4 },
                  { "startBar": 3, "endBar": 4, "motif": "m1", "transforms": ["transpose:+2", "displace:+0.5"], "repeatEvery": 4 } ] },
    { "id": "drums", "role": "drums", "name": "Kit", "low": "C1", "high": "C6", "grid": 4, "velocity": 96,
      "blocks": [ { "startBar": 1, "endBar": 4, "fill": "snare_roll", "drums": [
        { "voice": "kick", "steps": "x.....x. ..x..... " },
        { "voice": "snare", "steps": "....X... ....X..g" },
        { "voice": "closed_hat", "steps": "x.x.x.x. x.x.xgx." } ] } ] }
  ]
}
```

In the example, block fields that do not apply are left out. Every other key is required.
