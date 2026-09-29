#include "PluginProcessor.h"

#include "FlowstateSpikeUi.h"
#include "PreviewSynth.h"

namespace flowstate::spike
{

namespace
{
/** Adapts scheduler output to a pre-reserved MidiBuffer. */
struct MidiBufferSink final : EventSink
{
    explicit MidiBufferSink (juce::MidiBuffer& b) : buffer (b) {}

    void emit (const ScheduledEvent& e) noexcept override
    {
        const auto ch = (int) e.channel + 1;
        buffer.addEvent (e.isNoteOn ? juce::MidiMessage::noteOn (ch, (int) e.pitch, (juce::uint8) e.velocity)
                                    : juce::MidiMessage::noteOff (ch, (int) e.pitch),
                         e.sampleOffset);
    }

    juce::MidiBuffer& buffer;
};

constexpr size_t midiScratchBytes = 64 * 1024; // far more than one block of audition can need
} // namespace

//==================================================================================================
FlowstateSpikeProcessor::FlowstateSpikeProcessor()
   #if JucePlugin_IsMidiEffect
    : juce::AudioProcessor (BusesProperties())
   #else
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
   #endif
{
    configurePreviewSynth (synth);
    loadBundledSample();
    startTimer (250); // retires old audition buffers
}

FlowstateSpikeProcessor::~FlowstateSpikeProcessor()
{
    stopTimer();
    cancelPendingUpdate();

    // The host has stopped calling processBlock by now.
    delete incoming.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete current;
    current = nullptr;
}

//==================================================================================================
bool FlowstateSpikeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
   #else
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
   #endif
}

void FlowstateSpikeProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    synth.allNotesOff (0, false);
    outMidi.ensureSize (midiScratchBytes);
    scheduler.reset();
}

void FlowstateSpikeProcessor::releaseResources()
{
    scheduler.reset();
}

void FlowstateSpikeProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = audio.getNumSamples();

    outMidi.clear();
    outMidi.addEvents (midi, 0, numSamples, 0); // pass incoming MIDI through
    MidiBufferSink sink { outMidi };

    // 1. Pick up a freshly rendered clip (lock-free hand-off, see header).
    if (retired.load (std::memory_order_acquire) == nullptr)
    {
        if (auto* next = incoming.exchange (nullptr, std::memory_order_acq_rel))
        {
            scheduler.setClip (next, sink, 0);
            retired.store (current, std::memory_order_release);
            current = next;
        }
    }

    // 2. Where is the host?
    BlockPosition pos;
    pos.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRateHz;
    pos.numSamples = numSamples;

    auto hasHost = false;
    int sigNum = 4, sigDen = 4;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto info = hostPlayHead->getPosition())
        {
            if (const auto ppq = info->getPpqPosition())
            {
                hasHost = true;
                pos.hasPpq = true;
                pos.ppqStart = *ppq;
                pos.isPlaying = info->getIsPlaying();
                pos.bpm = info->getBpm().orFallback (120.0);
                pos.isLooping = info->getIsLooping();

                if (const auto loop = info->getLoopPoints())
                {
                    pos.loopStartPpq = loop->ppqStart;
                    pos.loopEndPpq = loop->ppqEnd;
                }

                if (const auto sig = info->getTimeSignature())
                {
                    sigNum = sig->numerator;
                    sigDen = sig->denominator;
                }
            }
        }
    }

    const auto internal = ! hasHost && internalPlaying.load (std::memory_order_relaxed);

    if (! hasHost)
    {
        if (resetInternalPpq.exchange (false))
            internalPpq = 0.0;

        pos.hasPpq = true;
        pos.isPlaying = internal;
        pos.bpm = 120.0;
        pos.ppqStart = internalPpq;
    }

    // 3. Schedule audition MIDI for this block.
    scheduler.process (pos, sink);

    if (internal)
        internalPpq += (double) numSamples / (pos.sampleRate * 60.0 / pos.bpm);

    // 4. Preview voice (instrument only).
    audio.clear();

    if constexpr (isInstrumentVariant)
    {
        const auto previewOn = previewEnabled.load (std::memory_order_relaxed);

        if (previewWasOn && ! previewOn)
            synth.allNotesOff (0, false);

        previewWasOn = previewOn;

        if (previewOn && audio.getNumChannels() > 0)
            synth.renderNextBlock (audio, outMidi, 0, numSamples);
    }

    // Copy rather than swap: our scratch keeps its reserved 64 KB, and the wrapper's buffer is a
    // long-lived member that stops growing after the first busy blocks.
    midi.clear();
    midi.addEvents (outMidi, 0, -1, 0);

    // 5. Publish for the UI.
    stHasHost.store (hasHost, std::memory_order_relaxed);
    stInternal.store (internal, std::memory_order_relaxed);
    stPlaying.store (pos.isPlaying, std::memory_order_relaxed);
    stPpq.store (pos.ppqStart, std::memory_order_relaxed);
    stBpm.store (pos.bpm, std::memory_order_relaxed);
    stLooping.store (pos.isLooping, std::memory_order_relaxed);
    stLoopStart.store (pos.loopStartPpq, std::memory_order_relaxed);
    stLoopEnd.store (pos.loopEndPpq, std::memory_order_relaxed);
    stSigNum.store (sigNum, std::memory_order_relaxed);
    stSigDen.store (sigDen, std::memory_order_relaxed);
    stActive.store (scheduler.getNumActiveNotes(), std::memory_order_relaxed);
    stDiscontinuities.store (scheduler.getNumDiscontinuities(), std::memory_order_relaxed);
}

void FlowstateSpikeProcessor::processBlockBypassed (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    // Bypass must not leave notes hanging downstream: release what we started, pass input through.
    MidiBufferSink sink { midi };
    scheduler.allNotesOff (sink, 0);
    scheduler.reset();
    synth.allNotesOff (0, false);
    audio.clear();
}

//==================================================================================================
juce::AudioProcessorEditor* FlowstateSpikeProcessor::createEditor()
{
    return createFlowstateSpikeEditor (*this);
}

//==================================================================================================
void FlowstateSpikeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("FlowstateSpike");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("clipPath", getClipPath());
    xml.setAttribute ("preview", previewEnabled.load() ? 1 : 0);
    xml.setAttribute ("editorWidth", editorWidth.load());
    xml.setAttribute ("editorHeight", editorHeight.load());
    copyXmlToBinary (xml, dest);
}

void FlowstateSpikeProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr || ! xml->hasTagName ("FlowstateSpike"))
        return;

    previewEnabled = xml->getIntAttribute ("preview", 1) != 0;
    editorWidth = juce::jmax (720, xml->getIntAttribute ("editorWidth", 960));
    editorHeight = juce::jmax (480, xml->getIntAttribute ("editorHeight", 600));

    {
        const juce::SpinLock::ScopedLockType lock (pathLock);
        pendingRestorePath = xml->getStringAttribute ("clipPath");
        pendingRestore = true;
    }

    // Clip loading (file I/O, allocation) belongs on the message thread.
    if (juce::MessageManager::existsAndIsCurrentThread())
        applyRestoredState();
    else
        triggerAsyncUpdate();
}

void FlowstateSpikeProcessor::handleAsyncUpdate()
{
    applyRestoredState();
}

void FlowstateSpikeProcessor::applyRestoredState()
{
    juce::String path;

    {
        const juce::SpinLock::ScopedLockType lock (pathLock);

        if (! pendingRestore)
            return;

        path = pendingRestorePath;
        pendingRestore = false;
    }

    if (path.isEmpty())
    {
        loadBundledSample();
        return;
    }

    const juce::File file (path);

    if (loadClipFromFile (file).failed())
    {
        const auto why = lastError;
        loadBundledSample();
        lastError = "Saved clip could not be loaded (" + why + "); using the bundled sample.";
        sendChangeMessage();
    }
}

//==================================================================================================
juce::String FlowstateSpikeProcessor::getClipPath() const
{
    const juce::SpinLock::ScopedLockType lock (pathLock);
    return clipPath;
}

juce::Result FlowstateSpikeProcessor::loadClipFromFile (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! file.existsAsFile())
    {
        lastError = "File not found: " + file.getFullPathName();
        return juce::Result::fail (lastError);
    }

    if (file.getSize() > 16 * 1024 * 1024)
    {
        lastError = "File too large";
        return juce::Result::fail (lastError);
    }

    ClipData data;
    const auto r = parseNotesJson (file.loadFileAsString(), data);

    if (r.failed())
    {
        lastError = file.getFileName() + ": " + r.getErrorMessage();
        return juce::Result::fail (lastError);
    }

    data.displayName = file.getFileName();
    return installClip (std::move (data), file.getFullPathName());
}

juce::Result FlowstateSpikeProcessor::loadBundledSample()
{
    JUCE_ASSERT_MESSAGE_THREAD

    ClipData data;
    const auto r = parseNotesJson (juce::String::fromUTF8 (FlowstateSpikeUi::sample_notes_json,
                                                           FlowstateSpikeUi::sample_notes_jsonSize),
                                   data);

    if (r.failed())
    {
        jassertfalse; // the bundled sample must always parse
        lastError = "Bundled sample: " + r.getErrorMessage();
        return juce::Result::fail (lastError);
    }

    data.displayName = "sample.notes.json";
    return installClip (std::move (data), {});
}

juce::Result FlowstateSpikeProcessor::installClip (ClipData&& data, const juce::String& path)
{
    publishRendered (std::make_unique<RenderedClip> (renderForAudition (data)));
    clip = std::move (data);
    lastError.clear();

    {
        const juce::SpinLock::ScopedLockType lock (pathLock);
        clipPath = path;
    }

    sendChangeMessage();
    return juce::Result::ok();
}

void FlowstateSpikeProcessor::publishRendered (std::unique_ptr<RenderedClip> rendered)
{
    // If the audio thread never picked up the previous one, it is still ours to delete.
    delete incoming.exchange (rendered.release(), std::memory_order_acq_rel);
}

void FlowstateSpikeProcessor::timerCallback()
{
    delete retired.exchange (nullptr, std::memory_order_acq_rel);
}

juce::File FlowstateSpikeProcessor::writeClipToTempMidi (int partIndex) const
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Flowstate Spike");

    if (! dir.createDirectory())
        return {};

    auto base = clip.displayName.upToLastOccurrenceOf (".notes", false, true)
                                .upToLastOccurrenceOf (".json", false, true);
    if (base.isEmpty())
        base = "Flowstate clip";

    if (partIndex >= 0 && partIndex < (int) clip.parts.size())
    {
        const auto& part = clip.parts[(size_t) partIndex];
        base << " - " << (part.name.isNotEmpty() ? part.name : part.id);
    }

    base = juce::File::createLegalFileName (base);

    const auto file = dir.getChildFile (base + ".mid");
    const auto temp = dir.getChildFile (base + ".mid.tmp");
    temp.deleteFile();

    {
        juce::FileOutputStream os (temp);

        if (! os.openedOk() || ! toMidiFile (clip, partIndex).writeTo (os, 1))
            return {};
    }

    if (! temp.moveFileTo (file)) // replaces any previous export
        return {};

    return file;
}

void FlowstateSpikeProcessor::setPreviewEnabled (bool on) noexcept
{
    previewEnabled = on;
}

void FlowstateSpikeProcessor::setInternalTransportPlaying (bool on) noexcept
{
    if (on && ! internalPlaying.load())
        resetInternalPpq = true;

    internalPlaying = on;
}

FlowstateSpikeProcessor::Status FlowstateSpikeProcessor::getStatus() const noexcept
{
    Status s;
    s.hasHostTransport = stHasHost.load (std::memory_order_relaxed);
    s.internalTransport = stInternal.load (std::memory_order_relaxed);
    s.playing = stPlaying.load (std::memory_order_relaxed);
    s.ppq = stPpq.load (std::memory_order_relaxed);
    s.bpm = stBpm.load (std::memory_order_relaxed);
    s.looping = stLooping.load (std::memory_order_relaxed);
    s.loopStart = stLoopStart.load (std::memory_order_relaxed);
    s.loopEnd = stLoopEnd.load (std::memory_order_relaxed);
    s.sigNum = stSigNum.load (std::memory_order_relaxed);
    s.sigDen = stSigDen.load (std::memory_order_relaxed);
    s.activeNotes = stActive.load (std::memory_order_relaxed);
    s.discontinuities = stDiscontinuities.load (std::memory_order_relaxed);
    return s;
}

} // namespace flowstate::spike

//==================================================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new flowstate::spike::FlowstateSpikeProcessor();
}
