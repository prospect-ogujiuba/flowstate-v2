#pragma once

#include "session/Capture.h"
#include "session/Controller.h"
#include "session/PluginState.h"
#include "session/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <string>

namespace flowstate::plugin {

// What an open editor can do for the controller: things that need a native view.
class EditorActions {
public:
    virtual ~EditorActions() = default;
    virtual std::optional<fb::ErrorInfo> startDrag(const juce::File& midiFile) = 0;
    virtual void releaseFocus() = 0;
};

/** Owns the Session and everything else; the editor is a view (docs/threading.md rule 5).

    Threads
      audio   : processBlock. Reads the playhead, publishes host sync atomics, pushes incoming
                MIDI into the capture ring, passes MIDI through. No allocation, locks or I/O.
      message : the Session and Controller (bridge commands), capture drain, state restore.
      any     : getStateInformation reads an immutable snapshot of the encoded session, swapped
                under a short lock after every change (rule 6). setStateInformation decodes on
                the calling thread and applies on the message thread. */
class FlowstateProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster,
                                 private juce::Timer,
                                 private juce::AsyncUpdater,
                                 private Platform {
public:
    FlowstateProcessor();
    ~FlowstateProcessor() override;

    // ---- AudioProcessor ---------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
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
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- Message thread ---------------------------------------------------------------------------
    static constexpr bool isInstrumentVariant = JucePlugin_IsSynth != 0;

    // A bridge command (JSON) in, a Reply (JSON) out.
    std::string handleBridgeCommand(const std::string& json);
    // The current Session view and Transport, as bridge events (JSON).
    std::string sessionEventJson();
    std::string transportEventJson();

    void setEditorActions(EditorActions* actions) { editorActions = actions; }
    juce::Point<int> getEditorSize() const noexcept { return {editorWidth.load(), editorHeight.load()}; }
    void setEditorSize(int w, int h) noexcept;

    Session& getSession() { return session; }
    HostSnapshot hostSnapshot() const noexcept;
    // Drains the capture ring now (the timer does it at 30 Hz); for tests.
    void pumpCapture();

private:
    // Platform (for the Controller)
    std::int64_t nowMs() override;
    HostSnapshot host() override { return hostSnapshot(); }
    int captureBars() override;
    std::optional<fb::ErrorInfo> startDrag(const fb::Clip&, const MidiMeta&,
                                           const std::optional<std::vector<std::string>>&, bool) override;
    std::optional<fb::ErrorInfo> exportMidi(const fb::Clip&, const MidiMeta&,
                                            const std::optional<std::vector<std::string>>&, bool) override;
    void releaseFocus(fb::FocusReason) override;
    std::optional<std::vector<std::uint8_t>> libraryResource(const std::string& name) override;

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void sessionChanged();
    void snapshotState();
    void applyPendingState();

    // ---- Message thread ---------------------------------------------------------------------------
    Session session;
    Controller controller;
    CaptureHistory captureHistory;
    EditorActions* editorActions = nullptr;
    std::unique_ptr<juce::FileChooser> exportChooser;
    int lastCaptureBars = 0;
    fb::EffectiveContext lastContext{};

    // State snapshot (any thread reads, message thread writes)
    juce::SpinLock stateLock;
    std::shared_ptr<const std::string> sessionSnapshot;  // encoded SavedSession
    std::shared_ptr<const PluginState> pendingState;     // decoded, applied on the message thread

    // ---- Audio thread -----------------------------------------------------------------------------
    CaptureRing captureRing;
    double captureClockPpq = 0.0;

    // Published by the audio thread
    std::atomic<bool> stHasHost{false}, stPlaying{false}, stRecording{false}, stLooping{false};
    std::atomic<double> stPpq{0.0}, stBpm{120.0}, stLoopStart{0.0}, stLoopEnd{0.0}, stClock{0.0};
    std::atomic<int> stSigNum{4}, stSigDen{4};

    std::atomic<int> editorWidth{960}, editorHeight{600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlowstateProcessor)
};

/** Implemented by WebEditor.cpp or HeadlessEditor.cpp, depending on the build. */
juce::AudioProcessorEditor* createFlowstateEditor(FlowstateProcessor&);

}  // namespace flowstate::plugin
