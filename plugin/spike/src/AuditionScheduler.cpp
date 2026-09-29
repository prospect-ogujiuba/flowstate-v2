#include "AuditionScheduler.h"

#include <algorithm>
#include <cmath>

namespace flowstate::spike
{

RenderedClip renderClip (const std::vector<ClipNote>& notes, double lengthPpq)
{
    RenderedClip out;
    out.lengthPpq = lengthPpq;

    if (! (lengthPpq > 0.0))
        return out;

    out.events.reserve (notes.size() * 2);

    for (const auto& n : notes)
    {
        if (n.channel < 0 || n.channel > 15 || n.pitch < 0 || n.pitch > 127)
            continue;

        if (! (n.lengthPpq > 0.0) || n.startPpq < 0.0 || n.startPpq >= lengthPpq)
            continue;

        const auto ch = (std::uint8_t) n.channel;
        const auto pitch = (std::uint8_t) n.pitch;
        const auto vel = (std::uint8_t) std::clamp (n.velocity, 1, 127);

        auto end = n.startPpq + n.lengthPpq;

        if (end >= lengthPpq)
            end = 0.0; // closes at the loop wrap, before the next iteration's note-ons

        out.events.push_back ({ n.startPpq, ch, pitch, vel, true });
        out.events.push_back ({ end, ch, pitch, 0, false });
    }

    std::sort (out.events.begin(), out.events.end(), [] (const ClipEvent& a, const ClipEvent& b)
    {
        if (a.ppq != b.ppq)            return a.ppq < b.ppq;
        if (a.isNoteOn != b.isNoteOn)  return ! a.isNoteOn; // offs first
        if (a.channel != b.channel)    return a.channel < b.channel;
        return a.pitch < b.pitch;
    });

    return out;
}

//==================================================================================================
void AuditionScheduler::reset() noexcept
{
    for (auto& ch : active)
        ch.reset();

    wasPlaying = false;
    expectedNextPpq = 0.0;
    lastSpanPpq = 0.0;
}

void AuditionScheduler::setClip (const RenderedClip* newClip, EventSink& sink, int sampleOffset) noexcept
{
    if (newClip == clip)
        return;

    allNotesOff (sink, sampleOffset);
    clip = newClip;
}

void AuditionScheduler::allNotesOff (EventSink& sink, int sampleOffset) noexcept
{
    for (int ch = 0; ch < 16; ++ch)
    {
        auto& bits = active[(size_t) ch];

        if (bits.none())
            continue;

        for (int p = 0; p < 128; ++p)
        {
            if (bits.test ((size_t) p))
            {
                sink.emit ({ sampleOffset, (std::uint8_t) ch, (std::uint8_t) p, 0, false });
                bits.reset ((size_t) p);
            }
        }
    }
}

int AuditionScheduler::getNumActiveNotes() const noexcept
{
    int n = 0;

    for (const auto& ch : active)
        n += (int) ch.count();

    return n;
}

void AuditionScheduler::process (const BlockPosition& pos, EventSink& sink) noexcept
{
    const auto usable = pos.isPlaying && pos.hasPpq
                     && pos.bpm > 0.0 && pos.sampleRate > 0.0 && pos.numSamples > 0
                     && std::isfinite (pos.ppqStart) && std::isfinite (pos.bpm);

    if (! usable)
    {
        if (wasPlaying || getNumActiveNotes() > 0)
            allNotesOff (sink, 0); // transport stopped

        wasPlaying = false;
        return;
    }

    const auto samplesPerPpq = pos.sampleRate * 60.0 / pos.bpm;
    const auto n = pos.numSamples;
    const auto a = pos.ppqStart;
    const auto span = (double) n / samplesPerPpq;
    const auto b = a + span;

    // Continuous playback: start exactly where the previous block ended so small host jitter
    // and tempo ramps neither drop nor double events. A larger jump is a seek or loop.
    auto from = a;

    if (wasPlaying)
    {
        const auto tolerance = std::max (minDiscontinuityPpq, 0.5 * std::max (span, lastSpanPpq));

        if (std::abs (a - expectedNextPpq) > tolerance)
        {
            allNotesOff (sink, 0);
            ++discontinuities;
        }
        else
        {
            from = expectedNextPpq;
        }
    }

    // Host loop that wraps inside this block (hosts that don't split blocks at the loop point).
    const auto loopLen = pos.loopEndPpq - pos.loopStartPpq;
    const auto wrapsInBlock = pos.isLooping && loopLen > 0.0
                           && a < pos.loopEndPpq && b > pos.loopEndPpq;

    if (wrapsInBlock)
    {
        const auto split = std::clamp ((int) std::floor ((pos.loopEndPpq - a) * samplesPerPpq + 1.0e-9), 0, n);
        scheduleRange (from, pos.loopEndPpq, a, samplesPerPpq, 0, split, sink);

        if (split < n)
        {
            allNotesOff (sink, split);

            const auto origin2 = pos.loopStartPpq - (double) split / samplesPerPpq; // ppq at sample 0
            const auto end2 = pos.loopStartPpq + (double) (n - split) / samplesPerPpq;
            scheduleRange (pos.loopStartPpq, end2, origin2, samplesPerPpq, split, n, sink);
            expectedNextPpq = end2;
        }
        else
        {
            expectedNextPpq = pos.loopStartPpq;
        }
    }
    else
    {
        scheduleRange (from, b, a, samplesPerPpq, 0, n, sink);
        expectedNextPpq = b;
    }

    lastSpanPpq = span;
    wasPlaying = true;
}

void AuditionScheduler::scheduleRange (double fromPpq, double toPpq,
                                       double blockOriginPpq, double samplesPerPpq,
                                       int minSample, int endSample, EventSink& sink) noexcept
{
    if (clip == nullptr || clip->events.empty() || ! (clip->lengthPpq > 0.0) || endSample <= minSample)
        return;

    fromPpq = std::max (fromPpq, 0.0); // nothing plays in pre-roll / count-in (negative ppq)

    if (! (toPpq > fromPpq))
        return;

    const auto len = clip->lengthPpq;
    const auto& events = clip->events;
    auto k = std::floor (fromPpq / len);

    // Bounded: a block never spans more than a handful of loop iterations (len >= 1 beat in
    // practice); the cap guards against pathological lengths.
    for (int guard = 0; guard < 4096; ++guard, k += 1.0)
    {
        const auto base = k * len;

        if (base >= toPpq)
            break;

        const auto lo = std::clamp (std::max (fromPpq, base) - base, 0.0, len);
        const auto hi = std::clamp (std::min (toPpq, base + len) - base, 0.0, len);

        if (hi <= lo)
            continue;

        auto it = std::lower_bound (events.begin(), events.end(), lo,
                                    [] (const ClipEvent& e, double v) { return e.ppq < v; });

        for (; it != events.end() && it->ppq < hi; ++it)
        {
            const auto globalPpq = base + it->ppq;
            const auto offset = std::clamp ((int) std::floor ((globalPpq - blockOriginPpq) * samplesPerPpq + 1.0e-6),
                                            minSample, endSample - 1);
            emitEvent (*it, offset, sink);
        }
    }
}

void AuditionScheduler::emitEvent (const ClipEvent& e, int sampleOffset, EventSink& sink) noexcept
{
    auto& bits = active[e.channel];

    if (e.isNoteOn)
    {
        if (bits.test (e.pitch)) // retrigger: never stack two note-ons for the same key
            sink.emit ({ sampleOffset, e.channel, e.pitch, 0, false });

        sink.emit ({ sampleOffset, e.channel, e.pitch, e.velocity, true });
        bits.set (e.pitch);
    }
    else
    {
        if (! bits.test (e.pitch))
            return; // e.g. the wrap note-off on the first iteration

        sink.emit ({ sampleOffset, e.channel, e.pitch, 0, false });
        bits.reset (e.pitch);
    }
}

} // namespace flowstate::spike
