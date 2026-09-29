#include "MidiFiles.h"

#include <algorithm>
#include <map>

namespace flowstate::plugin {

namespace {

bool selected(const fb::ClipPart& part, const std::optional<std::vector<std::string>>& partIds) {
    return !partIds || std::find(partIds->begin(), partIds->end(), part.partId) != partIds->end();
}

juce::MidiMessageSequence track(const juce::String& name, int channel, const std::vector<fb::ClipNote>& notes) {
    juce::MidiMessageSequence seq;
    auto title = juce::MidiMessage::textMetaEvent(3, name);
    title.setTimeStamp(0.0);
    seq.addEvent(title);
    for (const auto& n : notes) {
        seq.addEvent(juce::MidiMessage::noteOn(channel, n.pitch, static_cast<juce::uint8>(n.vel)), n.tick);
        seq.addEvent(juce::MidiMessage::noteOff(channel, n.pitch), n.tick + n.dur);
    }
    seq.updateMatchedPairs();
    seq.sort();
    return seq;
}

}  // namespace

juce::MidiFile clipToMidiFile(const fb::Clip& clip, const MidiMeta& meta,
                              const std::optional<std::vector<std::string>>& partIds, bool splitDrums) {
    juce::MidiFile file;
    file.setTicksPerQuarterNote(clip.ppq);

    juce::MidiMessageSequence conductor;
    conductor.addEvent(juce::MidiMessage::textMetaEvent(3, juce::String::fromUTF8(meta.title.c_str())), 0);
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / std::max(1.0, meta.tempo))), 0);
    conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(meta.meterNumerator, meta.meterDenominator), 0);
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(static_cast<double>(clip.ticksPerBar) * clip.bars);
    conductor.addEvent(end);
    file.addTrack(conductor);

    for (const auto& part : clip.parts) {
        if (!selected(part, partIds)) continue;
        const auto name = juce::String::fromUTF8(part.name.empty() ? part.partId.c_str() : part.name.c_str());
        if (splitDrums && part.voices && !part.voices->empty()) {
            std::map<int, std::string> sublaneOf;
            for (const auto& v : *part.voices) sublaneOf[v.pitch] = v.sublane;
            std::map<std::string, std::vector<fb::ClipNote>> bySublane;
            for (const auto& n : part.notes) {
                const auto it = sublaneOf.find(n.pitch);
                bySublane[it != sublaneOf.end() ? it->second : std::string("other")].push_back(n);
            }
            for (const auto& [sublane, notes] : bySublane)
                file.addTrack(track(name + " - " + juce::String(sublane).replace("_", " "), part.channel, notes));
        } else {
            file.addTrack(track(name, part.channel, part.notes));
        }
    }
    return file;
}

juce::String midiFileName(const fb::Clip& clip, const MidiMeta& meta,
                          const std::optional<std::vector<std::string>>& partIds) {
    juce::String name = juce::String::fromUTF8(meta.title.c_str());
    int count = 0;
    const fb::ClipPart* only = nullptr;
    for (const auto& p : clip.parts)
        if (selected(p, partIds)) {
            ++count;
            only = &p;
        }
    if (partIds && count == 1 && only != nullptr)
        name << " - " << juce::String::fromUTF8(only->name.empty() ? only->partId.c_str() : only->name.c_str());
    else if (partIds)
        name << " - " << count << " parts";
    return juce::File::createLegalFileName(name);
}

bool writeMidiFile(const juce::MidiFile& midi, const juce::File& file) {
    const auto temp = file.getSiblingFile(file.getFileName() + ".tmp");
    temp.deleteFile();
    {
        juce::FileOutputStream os(temp);
        if (!os.openedOk() || !midi.writeTo(os, 1)) return false;
    }
    return temp.moveFileTo(file);
}

}  // namespace flowstate::plugin
