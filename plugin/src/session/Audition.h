// Audition: the transport-locked scheduler from the Phase 0 spike (plugin/spike), fed by core's
// realizations, with bar-quantized switching between clips. JUCE-free, so it is unit-tested anywhere.
//
// Message thread: renderAudition() (allocates, sorts), then AuditionHandoff::publish().
// Audio thread:   AuditionHandoff::takeIncoming(), AuditionScheduler::queueClip() and process(),
//                 then AuditionHandoff::release(). No allocation, locks or I/O.
#pragma once

#include "flowstate/bridge.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace flowstate::plugin {

namespace fb = flowstate::bridge;

// A note in clip time. channel is 0-based (0..15), times are in quarter notes (PPQ).
struct AuditionNote {
    double startPpq = 0.0;
    double lengthPpq = 0.0;
    int channel = 0;
    int pitch = 60;
    int velocity = 100;
};

// A flat, pre-rendered note event.
struct ClipEvent {
    double ppq = 0.0;          // clip-local position in [0, lengthPpq)
    std::uint8_t channel = 0;  // 0..15
    std::uint8_t pitch = 0;    // 0..127
    std::uint8_t velocity = 0; // 1..127 for note-on, 0 for note-off
    bool isNoteOn = false;

    bool operator==(const ClipEvent&) const = default;
};

// Immutable after construction; published to the audio thread by pointer.
struct RenderedClip {
    std::vector<ClipEvent> events;  // sorted by ppq, note-offs before note-ons at equal ppq
    double lengthPpq = 0.0;
    double barPpq = 4.0;            // bar length; a queued clip takes over on a multiple of it

    bool operator==(const RenderedClip&) const = default;
};

// Pre-renders notes into a sorted event list. Notes are clipped to the loop length; a note-off
// that would land at or after the loop end is moved to ppq 0, where it closes the note at the
// wrap before the next iteration's note-ons (offs sort first).
RenderedClip renderClip(const std::vector<AuditionNote>& notes, double lengthPpq, double barPpq = 4.0);

// What an instance plays of a realized clip.
struct AuditionFilter {
    std::optional<fb::BarRange> loop;   // bars to loop; null = the whole clip
    std::optional<fb::Role> role;       // MIDI out "send: <role> only"; null = all parts
    std::vector<fb::PartState> parts;   // mute and solo
};

// A realized clip as this instance plays it: muted parts and other roles left out, solo honoured,
// the loop range moved to the start. Each part keeps its own channel (drums on 10); a forced output
// channel is applied when the MIDI is written, so the preview synth still hears drums as drums.
RenderedClip renderAudition(const fb::Clip& clip, const AuditionFilter& filter);

// What the host told us about this block.
struct BlockPosition {
    bool isPlaying = false;
    bool hasPpq = false;
    double ppqStart = 0.0;  // PPQ at the first sample of the block
    double bpm = 120.0;
    double sampleRate = 44100.0;
    int numSamples = 0;
    bool isLooping = false;  // host loop/cycle active
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
};

struct ScheduledEvent {
    int sampleOffset = 0;
    std::uint8_t channel = 0;  // 0..15
    std::uint8_t pitch = 0;
    std::uint8_t velocity = 0;
    bool isNoteOn = false;
};

// Receives events in chronological order. Implementations must not allocate on the audio thread.
class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void emit(const ScheduledEvent&) noexcept = 0;
};

class AuditionScheduler {
public:
    // Minimum PPQ error treated as a seek, regardless of block size (a 512th note).
    static constexpr double minDiscontinuityPpq = 1.0 / 128.0;

    // Forget all state without emitting anything (e.g. in prepareToPlay).
    void reset() noexcept;

    // Switch clip now. If it differs from the current one, every sounding note is released at
    // sampleOffset first. The pointer must stay valid until replaced. Drops a queued clip.
    void setClip(const RenderedClip* clip, EventSink& sink, int sampleOffset = 0) noexcept;

    // Queue a clip to take over on the next bar line of the playing clip. It takes over at once
    // when the transport is stopped, nothing is playing, or the host seeks. A clip queued earlier
    // and not yet playing is dropped. The pointer must stay valid until it is no longer
    // getClip() or getQueued().
    void queueClip(const RenderedClip* clip) noexcept;

    // Schedule one block. Emits note-ons/offs with sample offsets in [0, numSamples).
    void process(const BlockPosition& position, EventSink& sink) noexcept;

    // Release every sounding note at sampleOffset.
    void allNotesOff(EventSink& sink, int sampleOffset) noexcept;

    int getNumActiveNotes() const noexcept;
    std::uint32_t getNumDiscontinuities() const noexcept { return discontinuities; }
    std::uint32_t getNumSwitches() const noexcept { return switches; }
    const RenderedClip* getClip() const noexcept { return clip; }
    const RenderedClip* getQueued() const noexcept { return queued; }

private:
    void switchToQueued(EventSink& sink, int sampleOffset) noexcept;
    void scheduleSegment(double fromPpq, double toPpq, double blockOriginPpq, double samplesPerPpq, int minSample,
                         int endSample, EventSink& sink) noexcept;
    void scheduleRange(double fromPpq, double toPpq, double blockOriginPpq, double samplesPerPpq, int minSample,
                       int endSample, EventSink& sink) noexcept;
    void emitEvent(const ClipEvent& e, int sampleOffset, EventSink& sink) noexcept;

    const RenderedClip* clip = nullptr;
    const RenderedClip* queued = nullptr;
    std::array<std::bitset<128>, 16> active{};
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    double lastSpanPpq = 0.0;
    std::uint32_t discontinuities = 0;
    std::uint32_t switches = 0;
};

// Lock-free hand-off of rendered clips from the message thread to the audio thread, and back for
// deletion. The message thread publishes (silence is an empty clip); an unconsumed clip it
// replaces is deleted right there, since the audio thread never saw it. The audio thread takes
// the newest clip, holds the few it may still play (current and queued), and retires the rest
// into fixed slots that the message thread empties. Nothing is allocated or freed on the audio
// thread.
class AuditionHandoff {
public:
    AuditionHandoff() = default;
    // Deletes everything: call only once the audio thread has stopped.
    ~AuditionHandoff();
    AuditionHandoff(const AuditionHandoff&) = delete;
    AuditionHandoff& operator=(const AuditionHandoff&) = delete;

    // ---- Message thread
    void publish(std::unique_ptr<RenderedClip> clip);
    // Deletes retired clips. Returns how many.
    int collect();

    // ---- Audio thread
    // The newest published clip, or null if there is none (or no room to hold it yet).
    const RenderedClip* takeIncoming() noexcept;
    // Retires held clips other than these two (the scheduler's current and queued clips). A clip
    // stays held while every retire slot is full, until the message thread collects.
    void release(const RenderedClip* inUse, const RenderedClip* alsoInUse) noexcept;

    int heldCount() const noexcept;

private:
    static constexpr std::size_t kHeld = 4;
    static constexpr std::size_t kRetired = 8;

    std::atomic<RenderedClip*> incoming_{nullptr};
    std::array<RenderedClip*, kHeld> held_{};  // audio thread only
    std::array<std::atomic<RenderedClip*>, kRetired> retired_{};
};

}  // namespace flowstate::plugin
