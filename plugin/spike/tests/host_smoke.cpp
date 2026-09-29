// Loads the built Flowstate Spike VST3 binaries through JUCE's VST3 host and drives them with a
// fake transport: bar-1 alignment, looping, seek, stop, preview audio, state round trip.
// Usage: flowstate_spike_host_smoke <path/to/Flowstate Spike.vst3> [<path/to/... MIDI FX.vst3>]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <map>

namespace
{
int failures = 0;

#define EXPECT(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s -- ", #cond); \
    std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct FakePlayHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setIsLooping (false);
        return p;
    }

    bool playing = true;
    double ppq = 0.0, bpm = 100.0;
};

std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& path, double sr, int bs)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, path);

    if (types.isEmpty())
    {
        std::printf ("  no plugin types found in %s\n", path.toRawUTF8());
        return {};
    }

    juce::String error;
    auto instance = format.createInstanceFromDescription (*types[0], sr, bs, error);

    if (instance == nullptr)
        std::printf ("  load failed: %s\n", error.toRawUTF8());

    return instance;
}

struct Counts
{
    int ons = 0, offs = 0, badPairs = 0;
    std::map<int, bool> down;
    std::vector<std::pair<long long, int>> onTimes; // abs sample, channel*128+pitch

    void add (const juce::MidiBuffer& midi, long long blockStart)
    {
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();

            if (! msg.isNoteOnOrOff())
                continue;

            const auto key = (msg.getChannel() - 1) * 128 + msg.getNoteNumber();
            auto& d = down[key];

            if (msg.isNoteOn())
            {
                if (d) ++badPairs;
                ++ons;
                onTimes.push_back ({ blockStart + m.samplePosition, key });
            }
            else
            {
                if (! d) ++badPairs;
                ++offs;
            }

            d = msg.isNoteOn();
        }
    }

    int stuck() const
    {
        int n = 0;
        for (auto& [k, d] : down) n += d ? 1 : 0;
        return n;
    }
};

void runVariant (const juce::String& path, bool expectAudio)
{
    std::printf ("%s\n", path.toRawUTF8());
    constexpr double sr = 48000.0;
    constexpr int bs = 480;

    auto plugin = load (path, sr, bs);
    EXPECT (plugin != nullptr, "plugin loads");

    if (plugin == nullptr)
        return;

    EXPECT (plugin->producesMidi(), "producesMidi");
    std::printf ("  name=%s outputs=%d midiFx=%d\n", plugin->getName().toRawUTF8(),
                 plugin->getTotalNumOutputChannels(), plugin->isMidiEffect() ? 1 : 0);

    FakePlayHead head;
    plugin->setPlayHead (&head);
    plugin->prepareToPlay (sr, bs);


    juce::AudioBuffer<float> audio (juce::jmax (2, plugin->getTotalNumOutputChannels()), bs);
    juce::MidiBuffer midi;
    Counts counts;
    long long sample = 0;
    float peak = 0.0f;
    const auto spp = sr * 60.0 / head.bpm;

    auto block = [&]
    {
        audio.clear();
        midi.clear();
        plugin->processBlock (audio, midi);
        counts.add (midi, sample);
        peak = juce::jmax (peak, audio.getMagnitude (0, bs));
        sample += bs;

        if (head.playing)
            head.ppq += bs / spp;
    };

    // 100 bars = 25 loops of the 4-bar sample (109 notes per loop).
    while (head.ppq < 400.0)
        block();

    std::printf ("  100 bars: ons=%d offs=%d (%.2f per loop) peak=%.3f\n", counts.ons, counts.offs,
                 counts.ons / 25.0, peak);
    // The last block crosses bar 101, so loop 26's downbeat (7 notes at tick 0) may be included.
    EXPECT (counts.ons >= 25 * 109 && counts.ons <= 25 * 109 + 7, "ons %d, expected 25 x 109 (+7)", counts.ons);

    // Bar 1 alignment and drift: every loop's first note-on lands on the loop start sample.
    int misaligned = 0;
    std::map<int, int> firstPerLoop;

    for (auto& [t, key] : counts.onTimes)
    {
        const auto loop = (int) std::floor (((double) t + 0.1 * spp) / (16.0 * spp)); // loop whose start is within 0.1 beat

        if (firstPerLoop.count (loop) == 0)
        {
            firstPerLoop[loop] = 1;
            const auto expected = (long long) std::floor (loop * 16.0 * spp + 1.0e-6);
            if (std::llabs (t - expected) > 1) { ++misaligned; if (misaligned <= 3) std::printf ("  loop %d first on at %lld, expected %lld\n", loop, t, expected); }
        }
    }

    EXPECT (misaligned == 0, "%d loops start off bar 1", misaligned);
    EXPECT (counts.badPairs == 0, "%d stacked on/off", counts.badPairs);

    // Seek back mid-bar, play a bit, stop: nothing may stay down.
    head.ppq = 5.5;
    for (int i = 0; i < 50; ++i) block();
    head.playing = false;
    block();
    block();
    EXPECT (counts.stuck() == 0, "%d stuck notes after stop", counts.stuck());
    EXPECT (counts.badPairs == 0, "%d stacked on/off after seek", counts.badPairs);

    if (expectAudio)
        EXPECT (peak > 0.01f, "preview voice audible (peak %.4f)", peak);
    else
        EXPECT (peak == 0.0f, "MIDI FX is silent (peak %.4f)", peak);

    // State round trip into a fresh instance.
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    EXPECT (state.getSize() > 0, "state not empty");

    auto second = load (path, sr, bs);

    if (second != nullptr)
    {
        second->setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock state2;
        second->getStateInformation (state2);
        EXPECT (state == state2, "state round trip is stable");
    }

    plugin->releaseResources();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    if (argc < 2)
    {
        std::printf ("usage: %s <instrument.vst3> [<midifx.vst3>]\n", argv[0]);
        return 2;
    }

    runVariant (argv[1], true);

    if (argc > 2)
        runVariant (argv[2], false);

    std::printf ("%s (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
