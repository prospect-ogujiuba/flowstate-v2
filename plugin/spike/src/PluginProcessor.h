#pragma once

#include "AuditionScheduler.h"
#include "ClipFile.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace flowstate::spike
{

/** Owns all spike state: the loaded clip, the audition schedule and the preview synth.
    The editor is a view; closing it changes nothing here (docs/threading.md rule 5).

    Threads
      audio   : processBlock / processBlockBypassed. Reads the playhead, schedules audition,
                publishes transport atomics. No allocation, locks or I/O (the preview uses
                juce::Synthesiser, whose internal lock is never contended: only the audio
                thread touches it after prepareToPlay).
      message : loading clips, rendering the audition buffer, state restore, MIDI export.
      any     : getStateInformation / setStateInformation (short lock around the clip path).

    Audition buffer hand-off (lock-free, no allocation on the audio thread):
      message thread renders a new RenderedClip and exchanges it into `incoming`
        (an unconsumed older one is deleted right there: the audio thread never saw it);
      audio thread, when `retired` is empty, takes `incoming` and parks the clip it was
        playing in `retired`;
      a message-thread timer deletes whatever sits in `retired`. */
class FlowstateSpikeProcessor final : public juce::AudioProcessor,
                                      public juce::ChangeBroadcaster,
                                      private juce::Timer,
                                      private juce::AsyncUpdater
{
public:
    FlowstateSpikeProcessor();
    ~FlowstateSpikeProcessor() override;

    //==============================================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect != 0; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================================
    // Message-thread API (used by the editors)
    static constexpr bool isInstrumentVariant = JucePlugin_IsSynth != 0;

    juce::Result loadClipFromFile (const juce::File&);
    juce::Result loadBundledSample();
    const ClipData& getClip() const noexcept { return clip; }
    juce::String getClipPath() const;      // empty = bundled sample
    juce::String getLastError() const { return lastError; }

    /** Writes the loaded clip to a temp .mid (stable path per clip name) for drag-out. */
    juce::File writeClipToTempMidi() const;

    void setPreviewEnabled (bool) noexcept;
    bool isPreviewEnabled() const noexcept { return previewEnabled.load(); }

    /** Standalone / hosts without a playhead: an internal 120 bpm transport. */
    void setInternalTransportPlaying (bool) noexcept;

    struct Status
    {
        bool hasHostTransport = false;
        bool playing = false;
        double ppq = 0.0;
        double bpm = 120.0;
        int sigNum = 4, sigDen = 4;
        bool looping = false;
        double loopStart = 0.0, loopEnd = 0.0;
        bool internalTransport = false;
        std::uint32_t discontinuities = 0;
        int activeNotes = 0;
    };

    Status getStatus() const noexcept;

    juce::Point<int> getEditorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize (int w, int h) noexcept { editorWidth = w; editorHeight = h; }

private:
    void timerCallback() override;
    void handleAsyncUpdate() override;

    juce::Result installClip (ClipData&&, const juce::String& path);
    void publishRendered (std::unique_ptr<RenderedClip>);
    void applyRestoredState();

    //==============================================================================================
    // Message thread
    ClipData clip;
    juce::String lastError;

    // Shared: clip path is read by getStateInformation on any thread.
    mutable juce::SpinLock pathLock;
    juce::String clipPath;
    juce::String pendingRestorePath;
    bool pendingRestore = false;

    // Hand-off (see class comment)
    std::atomic<RenderedClip*> incoming { nullptr };
    std::atomic<RenderedClip*> retired { nullptr };

    // Audio thread only
    RenderedClip* current = nullptr;
    AuditionScheduler scheduler;
    juce::Synthesiser synth;
    juce::MidiBuffer outMidi;
    bool previewWasOn = true;
    double internalPpq = 0.0;
    double sampleRateHz = 44100.0;

    // Published by the audio thread, read by the UI timer
    std::atomic<bool> stHasHost { false }, stPlaying { false }, stLooping { false }, stInternal { false };
    std::atomic<double> stPpq { 0.0 }, stBpm { 120.0 }, stLoopStart { 0.0 }, stLoopEnd { 0.0 };
    std::atomic<int> stSigNum { 4 }, stSigDen { 4 }, stActive { 0 };
    std::atomic<std::uint32_t> stDiscontinuities { 0 };

    // Controls (message thread -> audio thread)
    std::atomic<bool> previewEnabled { true };
    std::atomic<bool> internalPlaying { false };
    std::atomic<bool> resetInternalPpq { false };
    std::atomic<int> editorWidth { 960 }, editorHeight { 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlowstateSpikeProcessor)
};

/** Implemented by WebEditor.cpp or HeadlessEditor.cpp, depending on the build. */
juce::AudioProcessorEditor* createFlowstateSpikeEditor (FlowstateSpikeProcessor&);

} // namespace flowstate::spike
