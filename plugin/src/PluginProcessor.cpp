#include "PluginProcessor.h"

#include "BuildId.h"
#include "FlowstateLibrary.h"
#include "MidiFiles.h"
#include "PreviewSynth.h"

namespace flowstate::plugin {

namespace {

std::string toJson(const auto& value) {
    nlohmann::json j;
    fb::to_json(j, value);
    return j.dump();
}

double ppqPerBar(int numerator, int denominator) { return numerator * 4.0 / std::max(1, denominator); }

// Audition output: MIDI out on the part's channel (or the forced one), and the preview synth on the
// part's own channel, so drums stay on 10 for it. Both buffers are reserved in prepareToPlay.
struct AuditionSink final : EventSink {
    AuditionSink(juce::MidiBuffer& out, juce::MidiBuffer* synth, int forcedChannel)
        : out_(out), synth_(synth), forced_(forcedChannel) {}

    void emit(const ScheduledEvent& e) noexcept override {
        const auto own = static_cast<int>(e.channel) + 1;
        const auto message = [&](int ch) {
            return e.isNoteOn ? juce::MidiMessage::noteOn(ch, static_cast<int>(e.pitch), static_cast<juce::uint8>(e.velocity))
                              : juce::MidiMessage::noteOff(ch, static_cast<int>(e.pitch));
        };
        out_.addEvent(message(forced_ > 0 ? forced_ : own), e.sampleOffset);
        if (synth_ != nullptr) synth_->addEvent(message(own), e.sampleOffset);
    }

    juce::MidiBuffer& out_;
    juce::MidiBuffer* synth_;
    int forced_;
};

constexpr size_t kMidiScratchBytes = 64 * 1024;  // far more than one block of audition can need

}  // namespace

FlowstateProcessor::FlowstateProcessor()
#if JucePlugin_IsMidiEffect
    : juce::AudioProcessor(BusesProperties()),
#else
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#endif
      session(juce::Uuid().toDashedString().toStdString(), kBuildId),
      controller(session, *this) {
    controller.onChanged = [this] { sessionChanged(); };
    controller.onEvent = [this](const fb::PluginEvent& event) {
        if (eventListener) eventListener(toJson(event));
    };
    service = std::make_unique<ServiceClient>(
        session.instanceId(),
        [this](const std::string& id, const fb::ServiceEvent& e) { controller.serviceEvent(id, e); },
        [this](const std::string& id, std::optional<fb::ErrorInfo> e) { controller.serviceEnded(id, std::move(e)); });
    configurePreviewSynth(synth);
    snapshotState();
    startTimerHz(30);
}

FlowstateProcessor::~FlowstateProcessor() {
    stopTimer();
    cancelPendingUpdate();
    controller.onChanged = nullptr;
    controller.onEvent = nullptr;
    controller.cancelAll();
    service.reset();
    // The host has stopped calling processBlock by now; `audition` deletes every clip it holds.
}

// ---- Audio thread ----------------------------------------------------------------------------------

bool FlowstateProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
#if JucePlugin_IsMidiEffect
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
#else
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty() && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
#endif
}

void FlowstateProcessor::prepareToPlay(double sampleRate, int) {
    synth.setCurrentPlaybackSampleRate(sampleRate);
    synth.allNotesOff(0, false);
    outMidi.ensureSize(kMidiScratchBytes);
    synthMidi.ensureSize(kMidiScratchBytes);
    scheduler.reset();
    freeRunPpq = 0.0;
}

void FlowstateProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = audio.getNumSamples();
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;

    // 1. Host sync.
    bool hasHost = false, playing = false, recording = false, looping = false;
    double ppq = 0.0, bpm = 120.0, loopStart = 0.0, loopEnd = 0.0;
    int sigNum = 4, sigDen = 4;
    if (auto* head = getPlayHead()) {
        if (const auto info = head->getPosition()) {
            if (const auto pos = info->getPpqPosition()) {
                hasHost = true;
                ppq = *pos;
            }
            playing = info->getIsPlaying();
            recording = info->getIsRecording();
            bpm = info->getBpm().orFallback(120.0);
            looping = info->getIsLooping();
            if (const auto loop = info->getLoopPoints()) {
                loopStart = loop->ppqStart;
                loopEnd = loop->ppqEnd;
            }
            if (const auto sig = info->getTimeSignature()) {
                sigNum = sig->numerator;
                sigDen = sig->denominator;
            }
        }
    }
    if (bpm <= 0.0) bpm = 120.0;

    // 2. Capture every incoming note, always on (Capture.h). The capture clock runs at the
    //    current tempo whether or not the host plays; host PPQ is kept when it does.
    const double ppqPerSample = bpm / 60.0 / sampleRate;
    for (const auto m : midi) {
        const auto msg = m.getMessage();
        if (!msg.isNoteOnOrOff()) continue;
        const double offset = m.samplePosition * ppqPerSample;
        captureRing.push({captureClockPpq + offset, hasHost && playing ? ppq + offset : -1.0,
                          static_cast<std::uint8_t>(msg.getChannel() - 1), static_cast<std::uint8_t>(msg.getNoteNumber()),
                          static_cast<std::uint8_t>(msg.isNoteOn() ? msg.getVelocity() : 0)});
    }
    captureClockPpq += numSamples * ppqPerSample;

    // 3. MIDI passes through untouched (a MIDI FX must never swallow what the user plays), and the
    //    audition joins it: the newest rendered clip is queued (it takes over on the next bar line),
    //    then this block is scheduled from the host's position, or from the plugin's own clock
    //    while the host is stopped and free-run is on.
    outMidi.clear();
    outMidi.addEvents(midi, 0, numSamples, 0);
    synthMidi.clear();
    if (const auto* next = audition.takeIncoming()) scheduler.queueClip(next);

    BlockPosition pos;
    pos.sampleRate = sampleRate;
    pos.numSamples = numSamples;
    pos.bpm = bpm;
    const bool hostRolling = hasHost && playing;
    if (!hostRolling && freeRun.load(std::memory_order_relaxed)) {
        pos.hasPpq = true;
        pos.isPlaying = true;
        pos.ppqStart = freeRunPpq;
        freeRunPpq += numSamples * ppqPerSample;
    } else {
        freeRunPpq = 0.0;  // free-run starts again from bar 1
        pos.hasPpq = hasHost;
        pos.isPlaying = playing;
        pos.ppqStart = ppq;
        pos.isLooping = looping;
        pos.loopStartPpq = loopStart;
        pos.loopEndPpq = loopEnd;
    }
    const bool previewOn = isInstrumentVariant && previewEnabled.load(std::memory_order_relaxed);
    AuditionSink sink(outMidi, previewOn ? &synthMidi : nullptr, forcedChannel.load(std::memory_order_relaxed));
    scheduler.process(pos, sink);
    audition.release(scheduler.getClip(), scheduler.getQueued());

    // 4. The preview synth (instrument only).
    audio.clear();
    if constexpr (isInstrumentVariant) {
        if (previewWasOn && !previewOn) synth.allNotesOff(0, false);
        previewWasOn = previewOn;
        if (previewOn && audio.getNumChannels() > 0) synth.renderNextBlock(audio, synthMidi, 0, numSamples);
    }

    // Copy rather than swap: the scratch keeps its reserved size, and the wrapper's buffer is a
    // long-lived member that stops growing after the first busy blocks.
    midi.clear();
    midi.addEvents(outMidi, 0, -1, 0);

    // 5. Publish for the message thread.
    stHasHost.store(hasHost, std::memory_order_relaxed);
    stPlaying.store(playing, std::memory_order_relaxed);
    stRecording.store(recording, std::memory_order_relaxed);
    stLooping.store(looping, std::memory_order_relaxed);
    stPpq.store(ppq, std::memory_order_relaxed);
    stBpm.store(bpm, std::memory_order_relaxed);
    stLoopStart.store(loopStart, std::memory_order_relaxed);
    stLoopEnd.store(loopEnd, std::memory_order_relaxed);
    stSigNum.store(sigNum, std::memory_order_relaxed);
    stSigDen.store(sigDen, std::memory_order_relaxed);
    stClock.store(captureClockPpq, std::memory_order_relaxed);
    stActive.store(scheduler.getNumActiveNotes(), std::memory_order_relaxed);
    stSwitches.store(scheduler.getNumSwitches(), std::memory_order_relaxed);
}

void FlowstateProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    // MIDI passes through; bypass must not leave audition notes hanging downstream.
    AuditionSink sink(midi, nullptr, forcedChannel.load(std::memory_order_relaxed));
    scheduler.allNotesOff(sink, 0);
    scheduler.reset();
    synth.allNotesOff(0, false);
    audio.clear();
}

HostSnapshot FlowstateProcessor::hostSnapshot() const noexcept {
    HostSnapshot h;
    h.hasHost = stHasHost.load(std::memory_order_relaxed);
    h.playing = stPlaying.load(std::memory_order_relaxed);
    h.recording = stRecording.load(std::memory_order_relaxed);
    h.ppq = stPpq.load(std::memory_order_relaxed);
    h.bpm = stBpm.load(std::memory_order_relaxed);
    h.meterNumerator = stSigNum.load(std::memory_order_relaxed);
    h.meterDenominator = stSigDen.load(std::memory_order_relaxed);
    h.looping = stLooping.load(std::memory_order_relaxed);
    h.loopStartPpq = stLoopStart.load(std::memory_order_relaxed);
    h.loopEndPpq = stLoopEnd.load(std::memory_order_relaxed);
    return h;
}

// ---- Editor --------------------------------------------------------------------------------------

juce::AudioProcessorEditor* FlowstateProcessor::createEditor() { return createFlowstateEditor(*this); }

void FlowstateProcessor::setEditorSize(int w, int h) noexcept {
    editorWidth = std::max(720, w);
    editorHeight = std::max(480, h);
}

// ---- State -----------------------------------------------------------------------------------------

void FlowstateProcessor::snapshotState() {
    auto snapshot = std::make_shared<const std::string>(toJson(session.save()));
    const juce::SpinLock::ScopedLockType lock(stateLock);
    sessionSnapshot = std::move(snapshot);
}

void FlowstateProcessor::getStateInformation(juce::MemoryBlock& dest) {
    std::shared_ptr<const std::string> snapshot;
    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        snapshot = sessionSnapshot;
    }
    const auto blob = encodeStateRaw(*snapshot, editorWidth.load(), editorHeight.load());
    dest.replaceAll(blob.data(), blob.size());
}

void FlowstateProcessor::setStateInformation(const void* data, int size) {
    if (data == nullptr || size <= 0) return;
    std::string error;
    auto decoded = decodeState(std::string(static_cast<const char*>(data), static_cast<size_t>(size)), error);
    if (!decoded) {
        DBG("Flowstate: ignoring saved state (" << error << ")");
        return;
    }
    editorWidth = decoded->editorWidth;
    editorHeight = decoded->editorHeight;

    // A get right after this set must return what was set, even before the message thread applies it.
    auto snapshot = std::make_shared<const std::string>(toJson(decoded->session));
    auto pending = std::make_shared<const PluginState>(std::move(*decoded));
    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        sessionSnapshot = std::move(snapshot);
        pendingState = std::move(pending);
    }
    if (juce::MessageManager::existsAndIsCurrentThread())
        applyPendingState();
    else
        triggerAsyncUpdate();
}

void FlowstateProcessor::handleAsyncUpdate() { applyPendingState(); }

void FlowstateProcessor::applyPendingState() {
    std::shared_ptr<const PluginState> pending;
    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        pending = std::move(pendingState);
        pendingState.reset();
    }
    if (pending == nullptr) return;
    std::vector<std::string> warnings;
    session.restore(pending->session, warnings);
    for ([[maybe_unused]] const auto& w : warnings) DBG("Flowstate: restore: " << w);
    sessionChanged();
}

void FlowstateProcessor::sessionChanged() {
    snapshotState();
    refreshAudition();
    sendChangeMessage();
}

// Renders what this instance plays and hands it to the audio thread, unless it is what already
// plays: an unchanged clip mustn't cut sustained notes at the next bar line.
void FlowstateProcessor::refreshAudition() {
    previewEnabled = session.previewSynth();
    freeRun = session.audition().freeRun;
    forcedChannel = session.midiOut().channel.value_or(0);

    const auto source = controller.audition();
    auto rendered = source.clip ? renderAudition(*source.clip, source.filter) : RenderedClip{};
    if (lastAudition && *lastAudition == rendered) return;
    lastAudition = rendered;
    audition.publish(std::make_unique<RenderedClip>(std::move(rendered)));
}

// ---- Bridge ----------------------------------------------------------------------------------------

std::string FlowstateProcessor::handleBridgeCommand(const std::string& json) {
    applyPendingState();  // a restore that hasn't landed yet must not be overwritten by a command
    return controller.handleJson(json);
}

std::string FlowstateProcessor::sessionEventJson() {
    return toJson(fb::PluginEvent{fb::SessionChanged{controller.view()}});
}

std::string FlowstateProcessor::transportEventJson() {
    return toJson(fb::PluginEvent{fb::TransportTick{hostSnapshot().toTransport()}});
}

// ---- Message-thread housekeeping -------------------------------------------------------------------

void FlowstateProcessor::pumpCapture() {
    captureRing.drain([this](const CapturedEvent& e) { captureHistory.add(e); });
    const auto h = hostSnapshot();
    captureHistory.trim(stClock.load(std::memory_order_relaxed), ppqPerBar(h.meterNumerator, h.meterDenominator));
}

int FlowstateProcessor::captureBars() {
    const auto h = hostSnapshot();
    return captureHistory.barsAvailable(stClock.load(std::memory_order_relaxed),
                                        ppqPerBar(h.meterNumerator, h.meterDenominator));
}

void FlowstateProcessor::timerCallback() {
    pumpCapture();
    audition.collect();

    // The UI's session view depends on capture and on the host's tempo and meter; push a new one
    // only when those change (transport itself goes out at 30 Hz from the editor).
    const auto bars = captureBars();
    const auto ctx = session.effectiveContext(hostSnapshot());
    const bool contextChanged = ctx.tempo != lastContext.tempo || ctx.meterNumerator != lastContext.meterNumerator ||
                                ctx.meterDenominator != lastContext.meterDenominator || ctx.timeFrom != lastContext.timeFrom;
    if (bars != lastCaptureBars || contextChanged) {
        lastCaptureBars = bars;
        lastContext = ctx;
        sendChangeMessage();
    }
}

// ---- Platform --------------------------------------------------------------------------------------

std::int64_t FlowstateProcessor::nowMs() { return juce::Time::currentTimeMillis(); }

// The built-in library (library/catalog), embedded by juce_add_binary_data and looked up by file name.
std::optional<std::vector<std::uint8_t>> FlowstateProcessor::libraryResource(const std::string& name) {
    for (int i = 0; i < FlowstateLibrary::namedResourceListSize; ++i) {
        const auto* resource = FlowstateLibrary::namedResourceList[i];
        if (name != FlowstateLibrary::getNamedResourceOriginalFilename(resource)) continue;
        int size = 0;
        const auto* data = static_cast<const std::uint8_t*>(static_cast<const void*>(FlowstateLibrary::getNamedResource(resource, size)));
        if (data == nullptr) return std::nullopt;
        return std::vector<std::uint8_t>(data, data + size);
    }
    return std::nullopt;
}

std::optional<fb::ErrorInfo> FlowstateProcessor::startDrag(const fb::Clip& clip, const MidiMeta& meta,
                                                           const std::optional<std::vector<std::string>>& partIds,
                                                           bool splitDrums) {
    if (editorActions == nullptr) return fb::ErrorInfo{fb::ErrorCode::Unavailable, "Open the Flowstate window to drag."};
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Flowstate");
    if (!dir.createDirectory()) return fb::ErrorInfo{fb::ErrorCode::Internal, "Could not create the temp folder."};
    const auto file = dir.getChildFile(midiFileName(clip, meta, partIds) + ".mid");
    if (!writeMidiFile(clipToMidiFile(clip, meta, partIds, splitDrums), file))
        return fb::ErrorInfo{fb::ErrorCode::Internal, "Could not write the MIDI file."};
    return editorActions->startDrag(file);
}

std::optional<fb::ErrorInfo> FlowstateProcessor::exportMidi(const fb::Clip& clip, const MidiMeta& meta,
                                                            const std::optional<std::vector<std::string>>& partIds,
                                                            bool splitDrums) {
    auto midi = std::make_shared<juce::MidiFile>(clipToMidiFile(clip, meta, partIds, splitDrums));
    const auto start = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                           .getChildFile(midiFileName(clip, meta, partIds) + ".mid");
    exportChooser = std::make_unique<juce::FileChooser>("Export MIDI", start, "*.mid");
    exportChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                   juce::FileBrowserComponent::warnAboutOverwriting,
                               [midi](const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file != juce::File()) writeMidiFile(*midi, file.withFileExtension("mid"));
                               });
    return std::nullopt;  // the chooser is open; the result isn't reported back
}

std::optional<fb::ErrorInfo> FlowstateProcessor::startPlan(const std::string& streamId, const fb::PlanRequest& request) {
    return service->startPlan(streamId, request);
}

std::optional<fb::ErrorInfo> FlowstateProcessor::startEdit(const std::string& streamId, const fb::EditRequest& request) {
    return service->startEdit(streamId, request);
}

void FlowstateProcessor::cancelStream(const std::string& streamId) { service->cancel(streamId); }

void FlowstateProcessor::releaseFocus(fb::FocusReason) {
    if (editorActions != nullptr) editorActions->releaseFocus();
}

}  // namespace flowstate::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new flowstate::plugin::FlowstateProcessor(); }
