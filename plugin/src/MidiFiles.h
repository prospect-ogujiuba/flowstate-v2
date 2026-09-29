// Clip -> Standard MIDI File for drag-out and export. Message thread only.
#pragma once

#include "session/Controller.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace flowstate::plugin {

/** A type-1 MIDI file at the clip's PPQ: a conductor track (title, tempo, meter), then one named
    track per selected part. With splitDrums, a drum part becomes one track per sublane. */
juce::MidiFile clipToMidiFile(const fb::Clip& clip, const MidiMeta& meta,
                              const std::optional<std::vector<std::string>>& partIds, bool splitDrums);

/** A file name for the selection: "<title>", "<title> - <part>" or "<title> - <n> parts". */
juce::String midiFileName(const fb::Clip& clip, const MidiMeta& meta,
                          const std::optional<std::vector<std::string>>& partIds);

/** Writes to `file` atomically (temp file, then move). */
bool writeMidiFile(const juce::MidiFile& midi, const juce::File& file);

}  // namespace flowstate::plugin
