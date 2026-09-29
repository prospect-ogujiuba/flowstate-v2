#pragma once

// Minimal preview voices so the instrument variant is audible with zero routing.
// Tones (every channel but 10): band-limited-enough triangle with an ADSR.
// Drums (channel 10): kick = pitch-swept sine, everything else = short noise burst.
// All rendering is allocation-free.

#include <juce_audio_basics/juce_audio_basics.h>

namespace flowstate::spike
{

struct ToneSound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int ch) override { return ch != 10; }
};

struct DrumSound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int ch) override { return ch == 10; }
};

class ToneVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<ToneSound*> (s) != nullptr; }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        phase = 0.0;
        increment = juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
        level = 0.08f * velocity;
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({ 0.004f, 0.25f, 0.55f, 0.12f });
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;

        const auto channels = out.getNumChannels();

        for (int i = start; i < start + num; ++i)
        {
            const auto tri = (float) (4.0 * std::abs (phase - 0.5) - 1.0);
            phase += increment;

            if (phase >= 1.0)
                phase -= 1.0;

            const auto s = tri * level * adsr.getNextSample();

            for (int c = 0; c < channels; ++c)
                out.addSample (c, i, s);

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    double phase = 0.0, increment = 0.0;
    float level = 0.0f;
    juce::ADSR adsr;
};

class DrumVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<DrumSound*> (s) != nullptr; }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        const auto sr = getSampleRate();
        isKick = note == 35 || note == 36;
        isHat = note == 42 || note == 44 || note == 46;
        remaining = (int) (sr * (isKick ? 0.18 : isHat ? 0.045 : 0.12));
        total = remaining;
        level = 0.3f * velocity;
        phase = 0.0;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (! allowTailOff) // one-shot: note-off lets the hit ring out
        {
            remaining = 0;
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;

        const auto sr = getSampleRate();
        const auto channels = out.getNumChannels();

        for (int i = start; i < start + num && remaining > 0; ++i, --remaining)
        {
            const auto t = 1.0f - (float) remaining / (float) total; // 0 -> 1
            const auto env = (1.0f - t) * (1.0f - t);
            float s;

            if (isKick)
            {
                const auto freq = 45.0 + 110.0 * (1.0 - t) * (1.0 - t);
                phase += juce::MathConstants<double>::twoPi * freq / sr;
                s = (float) std::sin (phase);
            }
            else
            {
                s = random.nextFloat() * 2.0f - 1.0f;

                if (isHat) // crude high-pass
                {
                    const auto hp = s - last;
                    last = s;
                    s = hp * 0.5f;
                }
            }

            s *= env * level;

            for (int c = 0; c < channels; ++c)
                out.addSample (c, i, s);
        }

        if (remaining <= 0)
            clearCurrentNote();
    }

private:
    bool isKick = false, isHat = false;
    int remaining = 0, total = 1;
    float level = 0.0f, last = 0.0f;
    double phase = 0.0;
    juce::Random random { 0x5eed };
};

inline void configurePreviewSynth (juce::Synthesiser& synth)
{
    synth.clearVoices();
    synth.clearSounds();

    for (int i = 0; i < 16; ++i)
        synth.addVoice (new ToneVoice());

    for (int i = 0; i < 6; ++i)
        synth.addVoice (new DrumVoice());

    synth.addSound (new ToneSound());
    synth.addSound (new DrumSound());
}

} // namespace flowstate::spike
