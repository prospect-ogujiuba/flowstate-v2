#pragma once

// Loading, converting and exporting `*.notes.json` clips (the `fs-realize --out-notes` format):
// {"ppq":960,"bars":N,"ticksPerBar":T,"tempo":bpm,"meter":[n,d],
//  "parts":[{"id","role","name","channel","notes":[{"tick","dur","pitch","vel"}]}]}
// channel is the 1-based MIDI channel (drums on 10). Message thread only.

#include "AuditionScheduler.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace flowstate::spike
{

struct ClipNoteTicks
{
    int tick = 0;
    int dur = 0;
    int pitch = 60;
    int vel = 100;
};

struct ClipPart
{
    juce::String id, role, name;
    int channel = 1; // 1-based
    std::vector<ClipNoteTicks> notes;
};

struct ClipData
{
    juce::String displayName;  // file name, for the UI and the exported .mid
    int ppq = 960;
    int bars = 4;
    int ticksPerBar = 3840;
    double tempo = 120.0;
    int meterNum = 4;
    int meterDen = 4;
    std::vector<ClipPart> parts;

    double lengthPpq() const noexcept { return (double) bars * ticksPerBar / ppq; }
    int totalNotes() const noexcept;
};

/** Parses and validates a notes JSON document. */
juce::Result parseNotesJson (const juce::String& json, ClipData& out);

/** Flattens every part into the audition event list (see renderClip). */
RenderedClip renderForAudition (const ClipData& clip);

/** A type-1 MIDI file with a single track: tempo, time signature, name and all notes
    (on each part's own channel). Ticks are the clip's PPQ. */
juce::MidiFile toMidiFile (const ClipData& clip);

/** JSON-friendly description for the WebView (notes as [tick, dur, pitch, vel] arrays). */
juce::var toVar (const ClipData& clip);

} // namespace flowstate::spike
