#pragma once

// Transport-locked audition scheduler. Pure C++ (no JUCE) so it can be unit-tested anywhere.
//
// Message thread: build a RenderedClip with renderClip() (allocates, sorts).
// Audio thread:   AuditionScheduler::process() once per block. No allocation, locks or I/O.

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>

namespace flowstate::spike
{

/** A note in clip time. channel is 0-based (0..15), times are in quarter notes (PPQ). */
struct ClipNote
{
    double startPpq = 0.0;
    double lengthPpq = 0.0;
    int channel = 0;
    int pitch = 60;
    int velocity = 100;
};

/** A flat, pre-rendered note event. */
struct ClipEvent
{
    double ppq = 0.0;              // clip-local position in [0, lengthPpq)
    std::uint8_t channel = 0;      // 0..15
    std::uint8_t pitch = 0;        // 0..127
    std::uint8_t velocity = 0;     // 1..127 for note-on, 0 for note-off
    bool isNoteOn = false;
};

/** Immutable after construction; published to the audio thread by pointer. */
struct RenderedClip
{
    std::vector<ClipEvent> events; // sorted by ppq, note-offs before note-ons at equal ppq
    double lengthPpq = 0.0;
};

/** Pre-renders notes into a sorted event list. Notes are clipped to the loop length; a note-off
    that would land at or after the loop end is moved to ppq 0, where it closes the note at the
    wrap before the next iteration's note-ons (offs sort first). Message thread only. */
RenderedClip renderClip (const std::vector<ClipNote>& notes, double lengthPpq);

/** What the host told us about this block. */
struct BlockPosition
{
    bool isPlaying = false;
    bool hasPpq = false;
    double ppqStart = 0.0;     // PPQ at the first sample of the block
    double bpm = 120.0;
    double sampleRate = 44100.0;
    int numSamples = 0;
    bool isLooping = false;    // host loop/cycle active
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
};

struct ScheduledEvent
{
    int sampleOffset = 0;
    std::uint8_t channel = 0;  // 0..15
    std::uint8_t pitch = 0;
    std::uint8_t velocity = 0;
    bool isNoteOn = false;
};

/** Receives events in chronological order. Implementations must not allocate on the audio thread. */
class EventSink
{
public:
    virtual ~EventSink() = default;
    virtual void emit (const ScheduledEvent&) noexcept = 0;
};

class AuditionScheduler
{
public:
    /** Minimum PPQ error treated as a seek, regardless of block size (a 512th note). */
    static constexpr double minDiscontinuityPpq = 1.0 / 128.0;

    /** Forget all state without emitting anything (e.g. in prepareToPlay). */
    void reset() noexcept;

    /** Switch clip. If it differs from the current one, every sounding note is released at
        sampleOffset first. The pointer must stay valid until replaced. */
    void setClip (const RenderedClip* clip, EventSink& sink, int sampleOffset = 0) noexcept;

    /** Schedule one block. Emits note-ons/offs with sample offsets in [0, numSamples). */
    void process (const BlockPosition& position, EventSink& sink) noexcept;

    /** Release every sounding note at sampleOffset. */
    void allNotesOff (EventSink& sink, int sampleOffset) noexcept;

    int getNumActiveNotes() const noexcept;
    std::uint32_t getNumDiscontinuities() const noexcept { return discontinuities; }
    const RenderedClip* getClip() const noexcept { return clip; }

private:
    void scheduleRange (double fromPpq, double toPpq,
                        double blockOriginPpq, double samplesPerPpq,
                        int minSample, int endSample, EventSink& sink) noexcept;
    void emitEvent (const ClipEvent& e, int sampleOffset, EventSink& sink) noexcept;

    const RenderedClip* clip = nullptr;
    std::array<std::bitset<128>, 16> active {};
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    double lastSpanPpq = 0.0;
    std::uint32_t discontinuities = 0;
};

} // namespace flowstate::spike
