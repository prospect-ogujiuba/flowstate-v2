#pragma once

#include "ServiceClient.h"
#include "session/Audition.h"
#include "session/Capture.h"
#include "session/Controller.h"
#include "session/PluginState.h"
#include "session/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
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
                MIDI into the capture ring, passes MIDI through, schedules the audition into MIDI
                out and the preview synth. No allocation, locks or I/O (the preview synth is a
                juce::Synthesiser, whose internal lock only the audio thread takes after
                prepareToPlay, so it is never contended).
      message : the Session and Controller (bridge commands), capture drain, state restore,
                rendering the audition (core realizations) and handing it over.
      network : the agent service client (ServiceClient), whose events arrive on the message thread.
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
    // Receives every PluginEvent other than `session` and `transport` (generation progress and
    // notices), as JSON. The open editor sets it; with no editor the events are dropped, and the
    // session carries the state they report.
    void setEventListener(std::function<void(const std::string&)> listener) { eventListener = std::move(listener); }
    juce::Point<int> getEditorSize() const noexcept { return {editorWidth.load(), editorHeight.load()}; }
    void setEditorSize(int w, int h) noexcept;

    Session& getSession() { return session; }
    Controller& getController() { return controller; }
    HostSnapshot hostSnapshot() const noexcept;
    // Drains the capture ring now (the timer does it at 30 Hz); for tests.
    void pumpCapture();
    // Replaces the OS keychain (tests); null means none.
    void useKeyStore(std::unique_ptr<KeyStore> store) { keys = std::move(store); }

    // Audio-thread counters, for tests and diagnostics.
    int auditionActiveNotes() const noexcept { return stActive.load(std::memory_order_relaxed); }
    std::uint32_t auditionSwitches() const noexcept { return stSwitches.load(std::memory_order_relaxed); }

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
    std::optional<fb::ErrorInfo> startPlan(const std::string& streamId, const fb::PlanRequest& request,
                                           const std::optional<std::string>& providerKey) override;
    std::optional<fb::ErrorInfo> startEdit(const std::string& streamId, const fb::EditRequest& request,
                                           const std::optional<std::string>& providerKey) override;
    void cancelStream(const std::string& streamId) override;
    void checkService() override;
    KeyStore* keyStore() override { return keys.get(); }

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void sessionChanged();
    void snapshotState();
    void applyPendingState();
    void refreshAudition();

    // ---- Message thread ---------------------------------------------------------------------------
    Session session;
    Controller controller;
    CaptureHistory captureHistory;
    EditorActions* editorActions = nullptr;
    std::unique_ptr<juce::FileChooser> exportChooser;
    int lastCaptureBars = 0;
    fb::EffectiveContext lastContext{};
    std::function<void(const std::string&)> eventListener;
    std::unique_ptr<ServiceClient> service;
    std::unique_ptr<KeyStore> keys;  // the OS keychain for BYOK keys (P1-12); null on Linux
    std::optional<RenderedClip> lastAudition;  // what was last handed to the audio thread

    // State snapshot (any thread reads, message thread writes)
    juce::SpinLock stateLock;
    std::shared_ptr<const std::string> sessionSnapshot;  // encoded SavedSession
    std::shared_ptr<const PluginState> pendingState;     // decoded, applied on the message thread

    // ---- Audio thread -----------------------------------------------------------------------------
    CaptureRing captureRing;
    double captureClockPpq = 0.0;
    AuditionHandoff audition;  // message thread publishes and collects; audio thread takes and releases
    AuditionScheduler scheduler;
    juce::Synthesiser synth;
    juce::MidiBuffer outMidi, synthMidi;  // reserved in prepareToPlay
    bool previewWasOn = true;
    double freeRunPpq = 0.0;

    // Controls (message thread -> audio thread)
    std::atomic<bool> previewEnabled{true}, freeRun{false};
    std::atomic<int> forcedChannel{0};  // 1..16, or 0 = each part's own channel

    // Published by the audio thread
    std::atomic<bool> stHasHost{false}, stPlaying{false}, stRecording{false}, stLooping{false};
    std::atomic<double> stPpq{0.0}, stBpm{120.0}, stLoopStart{0.0}, stLoopEnd{0.0}, stClock{0.0};
    std::atomic<int> stSigNum{4}, stSigDen{4}, stActive{0};
    std::atomic<std::uint32_t> stSwitches{0};

    std::atomic<int> editorWidth{960}, editorHeight{600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlowstateProcessor)
};

/** Implemented by WebEditor.cpp or HeadlessEditor.cpp, depending on the build. */
juce::AudioProcessorEditor* createFlowstateEditor(FlowstateProcessor&);

}  // namespace flowstate::plugin
