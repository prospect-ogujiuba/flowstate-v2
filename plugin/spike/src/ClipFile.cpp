#include "ClipFile.h"

namespace flowstate::spike
{

int ClipData::totalNotes() const noexcept
{
    int n = 0;

    for (const auto& p : parts)
        n += (int) p.notes.size();

    return n;
}

namespace
{
juce::Result fail (const juce::String& message) { return juce::Result::fail (message); }

bool isIntegral (const juce::var& v) { return v.isInt() || v.isInt64() || (v.isDouble() && std::floor ((double) v) == (double) v); }
} // namespace

juce::Result parseNotesJson (const juce::String& json, ClipData& out)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (json, root);

    if (parsed.failed())
        return fail ("Invalid JSON: " + parsed.getErrorMessage());

    if (! root.isObject())
        return fail ("Top level must be an object");

    ClipData clip;

    if (! isIntegral (root["ppq"]) || (int) root["ppq"] <= 0)
        return fail ("\"ppq\" must be a positive integer");

    if (! isIntegral (root["bars"]) || (int) root["bars"] <= 0 || (int) root["bars"] > 1024)
        return fail ("\"bars\" must be an integer in 1..1024");

    clip.ppq = (int) root["ppq"];
    clip.bars = (int) root["bars"];

    const auto& meter = root["meter"];

    if (meter.isArray() && meter.size() == 2)
    {
        clip.meterNum = (int) meter[0];
        clip.meterDen = (int) meter[1];
    }

    if (clip.meterNum <= 0 || clip.meterNum > 32 || ! juce::isPowerOfTwo (clip.meterDen) || clip.meterDen > 32)
        return fail ("\"meter\" must be [numerator, power-of-two denominator]");

    const auto expectedTicksPerBar = clip.ppq * 4 * clip.meterNum / clip.meterDen;
    clip.ticksPerBar = root.hasProperty ("ticksPerBar") ? (int) root["ticksPerBar"] : expectedTicksPerBar;

    if (clip.ticksPerBar != expectedTicksPerBar)
        return fail ("\"ticksPerBar\" (" + juce::String (clip.ticksPerBar) + ") does not match ppq and meter ("
                     + juce::String (expectedTicksPerBar) + ")");

    clip.tempo = root.hasProperty ("tempo") ? (double) root["tempo"] : 120.0;

    if (! (clip.tempo >= 20.0 && clip.tempo <= 400.0))
        return fail ("\"tempo\" must be in 20..400");

    const auto* parts = root["parts"].getArray();

    if (parts == nullptr)
        return fail ("\"parts\" must be an array");

    const auto clipTicks = (juce::int64) clip.bars * clip.ticksPerBar;

    for (const auto& p : *parts)
    {
        ClipPart part;
        part.id = p["id"].toString();
        part.role = p["role"].toString();
        part.name = p["name"].toString();
        part.channel = (int) p["channel"];

        if (part.channel < 1 || part.channel > 16)
            return fail ("Part \"" + part.id + "\": channel must be 1..16");

        const auto* notes = p["notes"].getArray();

        if (notes == nullptr)
            return fail ("Part \"" + part.id + "\": \"notes\" must be an array");

        part.notes.reserve ((size_t) notes->size());

        for (const auto& n : *notes)
        {
            ClipNoteTicks note { (int) n["tick"], (int) n["dur"], (int) n["pitch"], (int) n["vel"] };

            if (note.tick < 0 || note.tick >= clipTicks || note.dur <= 0
                || note.pitch < 0 || note.pitch > 127 || note.vel < 1 || note.vel > 127)
                return fail ("Part \"" + part.id + "\": invalid note at tick " + juce::String (note.tick));

            part.notes.push_back (note);
        }

        clip.parts.push_back (std::move (part));
    }

    out = std::move (clip);
    return juce::Result::ok();
}

RenderedClip renderForAudition (const ClipData& clip)
{
    std::vector<ClipNote> notes;
    notes.reserve ((size_t) clip.totalNotes());
    const auto tpq = (double) clip.ppq;

    for (const auto& part : clip.parts)
        for (const auto& n : part.notes)
            notes.push_back ({ n.tick / tpq, n.dur / tpq, part.channel - 1, n.pitch, n.vel });

    return renderClip (notes, clip.lengthPpq());
}

namespace
{
    void addMeta (juce::MidiMessageSequence& track, juce::MidiMessage m)
    {
        m.setTimeStamp (0);
        track.addEvent (m);
    }

    void addTimingMeta (juce::MidiMessageSequence& track, const ClipData& clip)
    {
        addMeta (track, juce::MidiMessage::tempoMetaEvent ((int) std::lround (60'000'000.0 / clip.tempo)));
        addMeta (track, juce::MidiMessage::timeSignatureMetaEvent (clip.meterNum, clip.meterDen));
    }

    void addNotes (juce::MidiMessageSequence& track, const ClipPart& part, double clipTicks)
    {
        for (const auto& n : part.notes)
        {
            const auto end = std::min ((double) n.tick + n.dur, clipTicks);
            track.addEvent (juce::MidiMessage::noteOn (part.channel, n.pitch, (juce::uint8) n.vel), (double) n.tick);
            track.addEvent (juce::MidiMessage::noteOff (part.channel, n.pitch), end);
        }
    }

    // End of track at the clip length so DAWs size the clip to whole bars.
    void finish (juce::MidiMessageSequence& track, double clipTicks)
    {
        track.addEvent (juce::MidiMessage::endOfTrack(), clipTicks);
        track.sort();
        track.updateMatchedPairs();
    }

    juce::String partTrackName (const ClipPart& part)
    {
        return part.name.isNotEmpty() ? part.name : (part.role.isNotEmpty() ? part.role : part.id);
    }
}

juce::MidiFile toMidiFile (const ClipData& clip, int partIndex)
{
    const auto clipTicks = (double) clip.bars * clip.ticksPerBar;
    juce::MidiFile file;
    file.setTicksPerQuarterNote (clip.ppq);

    if (partIndex >= 0 && partIndex < (int) clip.parts.size())
    {
        const auto& part = clip.parts[(size_t) partIndex];
        juce::MidiMessageSequence track;
        addMeta (track, juce::MidiMessage::textMetaEvent (3, partTrackName (part)));
        addTimingMeta (track, clip);
        addNotes (track, part, clipTicks);
        finish (track, clipTicks);
        file.addTrack (track);
        return file;
    }

    juce::MidiMessageSequence conductor;
    addMeta (conductor, juce::MidiMessage::textMetaEvent (3, clip.displayName.upToLastOccurrenceOf (".notes", false, true)));
    addTimingMeta (conductor, clip);
    finish (conductor, clipTicks);
    file.addTrack (conductor);

    for (const auto& part : clip.parts)
    {
        juce::MidiMessageSequence track;
        addMeta (track, juce::MidiMessage::textMetaEvent (3, partTrackName (part)));
        addNotes (track, part, clipTicks);
        finish (track, clipTicks);
        file.addTrack (track);
    }

    return file;
}

juce::var toVar (const ClipData& clip)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", clip.displayName);
    obj->setProperty ("ppq", clip.ppq);
    obj->setProperty ("bars", clip.bars);
    obj->setProperty ("ticksPerBar", clip.ticksPerBar);
    obj->setProperty ("tempo", clip.tempo);
    obj->setProperty ("meter", juce::Array<juce::var> { clip.meterNum, clip.meterDen });
    obj->setProperty ("lengthPpq", clip.lengthPpq());

    juce::Array<juce::var> parts;

    for (const auto& p : clip.parts)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("id", p.id);
        po->setProperty ("role", p.role);
        po->setProperty ("name", p.name);
        po->setProperty ("channel", p.channel);

        juce::Array<juce::var> notes;
        notes.ensureStorageAllocated ((int) p.notes.size());

        for (const auto& n : p.notes)
            notes.add (juce::Array<juce::var> { n.tick, n.dur, n.pitch, n.vel });

        po->setProperty ("notes", notes);
        parts.add (juce::var (po));
    }

    obj->setProperty ("parts", parts);
    return juce::var (obj);
}

} // namespace flowstate::spike
