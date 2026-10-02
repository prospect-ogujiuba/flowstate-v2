#include "session/Audition.h"

#include <algorithm>
#include <cmath>

namespace flowstate::plugin {

RenderedClip renderClip(const std::vector<AuditionNote>& notes, double lengthPpq, double barPpq) {
    RenderedClip out;
    out.lengthPpq = lengthPpq;
    out.barPpq = barPpq > 0.0 ? barPpq : 4.0;
    if (!(lengthPpq > 0.0)) return out;

    out.events.reserve(notes.size() * 2);
    for (const auto& n : notes) {
        if (n.channel < 0 || n.channel > 15 || n.pitch < 0 || n.pitch > 127) continue;
        if (!(n.lengthPpq > 0.0) || n.startPpq < 0.0 || n.startPpq >= lengthPpq) continue;

        const auto ch = static_cast<std::uint8_t>(n.channel);
        const auto pitch = static_cast<std::uint8_t>(n.pitch);
        const auto vel = static_cast<std::uint8_t>(std::clamp(n.velocity, 1, 127));
        auto end = n.startPpq + n.lengthPpq;
        if (end >= lengthPpq) end = 0.0;  // closes at the loop wrap, before the next iteration's note-ons

        out.events.push_back({n.startPpq, ch, pitch, vel, true});
        out.events.push_back({end, ch, pitch, 0, false});
    }

    std::sort(out.events.begin(), out.events.end(), [](const ClipEvent& a, const ClipEvent& b) {
        if (a.ppq != b.ppq) return a.ppq < b.ppq;
        if (a.isNoteOn != b.isNoteOn) return !a.isNoteOn;  // offs first
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.pitch < b.pitch;
    });
    return out;
}

RenderedClip renderAudition(const fb::Clip& clip, const AuditionFilter& filter) {
    const double ticksPerQuarter = clip.ppq > 0 ? static_cast<double>(clip.ppq) : 960.0;
    const double barPpq = clip.ticksPerBar / ticksPerQuarter;
    int startBar = 1, endBar = std::max(1, clip.bars);
    if (filter.loop) {
        startBar = std::clamp(filter.loop->startBar, 1, endBar);
        endBar = std::clamp(filter.loop->endBar, startBar, endBar);
    }
    const auto fromTick = static_cast<std::int64_t>(startBar - 1) * clip.ticksPerBar;
    const auto toTick = static_cast<std::int64_t>(endBar) * clip.ticksPerBar;

    const auto stateOf = [&](const std::string& partId) -> const fb::PartState* {
        for (const auto& s : filter.parts)
            if (s.partId == partId) return &s;
        return nullptr;
    };
    bool anySolo = false;
    for (const auto& p : clip.parts)
        if (const auto* s = stateOf(p.partId); s && s->solo) anySolo = true;

    std::vector<AuditionNote> notes;
    for (const auto& p : clip.parts) {
        if (filter.role && p.role != *filter.role) continue;
        const auto* s = stateOf(p.partId);
        if (s && s->muted) continue;
        if (anySolo && !(s && s->solo)) continue;
        for (const auto& n : p.notes) {
            if (n.tick < fromTick || n.tick >= toTick) continue;
            notes.push_back({static_cast<double>(n.tick - fromTick) / ticksPerQuarter, n.dur / ticksPerQuarter,
                             p.channel - 1, n.pitch, n.vel});
        }
    }
    return renderClip(notes, static_cast<double>(toTick - fromTick) / ticksPerQuarter, barPpq);
}

// ---- Scheduler -----------------------------------------------------------------------------------

void AuditionScheduler::reset() noexcept {
    for (auto& ch : active) ch.reset();
    wasPlaying = false;
    expectedNextPpq = 0.0;
    lastSpanPpq = 0.0;
}

void AuditionScheduler::setClip(const RenderedClip* newClip, EventSink& sink, int sampleOffset) noexcept {
    queued = nullptr;
    if (newClip == clip) return;
    allNotesOff(sink, sampleOffset);
    clip = newClip;
}

void AuditionScheduler::queueClip(const RenderedClip* newClip) noexcept { queued = newClip; }

void AuditionScheduler::switchToQueued(EventSink& sink, int sampleOffset) noexcept {
    if (queued == nullptr) return;
    allNotesOff(sink, sampleOffset);
    clip = queued;
    queued = nullptr;
    ++switches;
}

void AuditionScheduler::allNotesOff(EventSink& sink, int sampleOffset) noexcept {
    for (int ch = 0; ch < 16; ++ch) {
        auto& bits = active[static_cast<size_t>(ch)];
        if (bits.none()) continue;
        for (int p = 0; p < 128; ++p) {
            if (bits.test(static_cast<size_t>(p))) {
                sink.emit({sampleOffset, static_cast<std::uint8_t>(ch), static_cast<std::uint8_t>(p), 0, false});
                bits.reset(static_cast<size_t>(p));
            }
        }
    }
}

int AuditionScheduler::getNumActiveNotes() const noexcept {
    int n = 0;
    for (const auto& ch : active) n += static_cast<int>(ch.count());
    return n;
}

void AuditionScheduler::process(const BlockPosition& pos, EventSink& sink) noexcept {
    const auto usable = pos.isPlaying && pos.hasPpq && pos.bpm > 0.0 && pos.sampleRate > 0.0 && pos.numSamples > 0 &&
                        std::isfinite(pos.ppqStart) && std::isfinite(pos.bpm);

    if (!usable) {
        if (wasPlaying || getNumActiveNotes() > 0) allNotesOff(sink, 0);  // transport stopped
        switchToQueued(sink, 0);  // nothing sounds, so a queued clip needn't wait for a bar line
        wasPlaying = false;
        return;
    }

    const auto samplesPerPpq = pos.sampleRate * 60.0 / pos.bpm;
    const auto n = pos.numSamples;
    const auto a = pos.ppqStart;
    const auto span = static_cast<double>(n) / samplesPerPpq;
    const auto b = a + span;

    // Continuous playback: start exactly where the previous block ended so small host jitter
    // and tempo ramps neither drop nor double events. A larger jump is a seek or loop.
    auto from = a;
    if (wasPlaying) {
        const auto tolerance = std::max(minDiscontinuityPpq, 0.5 * std::max(span, lastSpanPpq));
        if (std::abs(a - expectedNextPpq) > tolerance) {
            allNotesOff(sink, 0);
            ++discontinuities;
            switchToQueued(sink, 0);
        } else {
            from = expectedNextPpq;
        }
    } else {
        switchToQueued(sink, 0);  // starting from silence
    }
    // Nothing audible to wait for.
    if (clip == nullptr || clip->events.empty()) switchToQueued(sink, 0);

    // Host loop that wraps inside this block (hosts that don't split blocks at the loop point).
    const auto loopLen = pos.loopEndPpq - pos.loopStartPpq;
    const auto wrapsInBlock = pos.isLooping && loopLen > 0.0 && a < pos.loopEndPpq && b > pos.loopEndPpq;

    if (wrapsInBlock) {
        const auto split = std::clamp(static_cast<int>(std::floor((pos.loopEndPpq - a) * samplesPerPpq + 1.0e-9)), 0, n);
        scheduleSegment(from, pos.loopEndPpq, a, samplesPerPpq, 0, split, sink);
        if (split < n) {
            allNotesOff(sink, split);
            const auto origin2 = pos.loopStartPpq - static_cast<double>(split) / samplesPerPpq;  // ppq at sample 0
            const auto end2 = pos.loopStartPpq + static_cast<double>(n - split) / samplesPerPpq;
            scheduleSegment(pos.loopStartPpq, end2, origin2, samplesPerPpq, split, n, sink);
            expectedNextPpq = end2;
        } else {
            expectedNextPpq = pos.loopStartPpq;
        }
    } else {
        scheduleSegment(from, b, a, samplesPerPpq, 0, n, sink);
        expectedNextPpq = b;
    }

    lastSpanPpq = span;
    wasPlaying = true;
}

void AuditionScheduler::scheduleSegment(double fromPpq, double toPpq, double blockOriginPpq, double samplesPerPpq,
                                        int minSample, int endSample, EventSink& sink) noexcept {
    if (queued != nullptr && endSample > minSample) {
        // The next bar line of the playing clip, in host time (bar 1 starts at ppq 0).
        const auto bar = clip != nullptr && clip->barPpq > 0.0 ? clip->barPpq : 4.0;
        const auto boundary = std::ceil(std::max(fromPpq, 0.0) / bar - 1.0e-9) * bar;
        if (boundary < toPpq) {
            scheduleRange(fromPpq, boundary, blockOriginPpq, samplesPerPpq, minSample, endSample, sink);
            const auto at = std::clamp(static_cast<int>(std::floor((boundary - blockOriginPpq) * samplesPerPpq + 1.0e-6)),
                                       minSample, endSample - 1);
            switchToQueued(sink, at);
            scheduleRange(boundary, toPpq, blockOriginPpq, samplesPerPpq, at, endSample, sink);
            return;
        }
    }
    scheduleRange(fromPpq, toPpq, blockOriginPpq, samplesPerPpq, minSample, endSample, sink);
}

void AuditionScheduler::scheduleRange(double fromPpq, double toPpq, double blockOriginPpq, double samplesPerPpq,
                                      int minSample, int endSample, EventSink& sink) noexcept {
    if (clip == nullptr || clip->events.empty() || !(clip->lengthPpq > 0.0) || endSample <= minSample) return;

    fromPpq = std::max(fromPpq, 0.0);  // nothing plays in pre-roll / count-in (negative ppq)
    if (!(toPpq > fromPpq)) return;

    const auto len = clip->lengthPpq;
    const auto& events = clip->events;
    auto k = std::floor(fromPpq / len);

    // Bounded: a block never spans more than a handful of loop iterations (len >= 1 beat in
    // practice); the cap guards against pathological lengths.
    for (int guard = 0; guard < 4096; ++guard, k += 1.0) {
        const auto base = k * len;
        if (base >= toPpq) break;

        const auto lo = std::clamp(std::max(fromPpq, base) - base, 0.0, len);
        const auto hi = std::clamp(std::min(toPpq, base + len) - base, 0.0, len);
        if (hi <= lo) continue;

        auto it = std::lower_bound(events.begin(), events.end(), lo, [](const ClipEvent& e, double v) { return e.ppq < v; });
        for (; it != events.end() && it->ppq < hi; ++it) {
            const auto globalPpq = base + it->ppq;
            const auto offset = std::clamp(static_cast<int>(std::floor((globalPpq - blockOriginPpq) * samplesPerPpq + 1.0e-6)),
                                           minSample, endSample - 1);
            emitEvent(*it, offset, sink);
        }
    }
}

void AuditionScheduler::emitEvent(const ClipEvent& e, int sampleOffset, EventSink& sink) noexcept {
    auto& bits = active[e.channel];
    if (e.isNoteOn) {
        if (bits.test(e.pitch))  // retrigger: never stack two note-ons for the same key
            sink.emit({sampleOffset, e.channel, e.pitch, 0, false});
        sink.emit({sampleOffset, e.channel, e.pitch, e.velocity, true});
        bits.set(e.pitch);
    } else {
        if (!bits.test(e.pitch)) return;  // e.g. the wrap note-off on the first iteration
        sink.emit({sampleOffset, e.channel, e.pitch, 0, false});
        bits.reset(e.pitch);
    }
}

// ---- Hand-off ------------------------------------------------------------------------------------

AuditionHandoff::~AuditionHandoff() {
    delete incoming_.exchange(nullptr);
    for (auto*& c : held_) {
        delete c;
        c = nullptr;
    }
    collect();
}

void AuditionHandoff::publish(std::unique_ptr<RenderedClip> clip) {
    // If the audio thread never picked up the previous one, it is still ours to delete.
    delete incoming_.exchange(clip.release(), std::memory_order_acq_rel);
}

int AuditionHandoff::collect() {
    int n = 0;
    for (auto& slot : retired_)
        if (auto* c = slot.exchange(nullptr, std::memory_order_acq_rel)) {
            delete c;
            ++n;
        }
    return n;
}

const RenderedClip* AuditionHandoff::takeIncoming() noexcept {
    auto free = std::find(held_.begin(), held_.end(), nullptr);
    if (free == held_.end()) return nullptr;  // wait until release() makes room
    auto* next = incoming_.exchange(nullptr, std::memory_order_acq_rel);
    if (next != nullptr) *free = next;
    return next;
}

void AuditionHandoff::release(const RenderedClip* inUse, const RenderedClip* alsoInUse) noexcept {
    for (auto*& c : held_) {
        if (c == nullptr || c == inUse || c == alsoInUse) continue;
        for (auto& slot : retired_) {
            // Only this thread fills an empty slot; the message thread only empties them.
            if (slot.load(std::memory_order_acquire) == nullptr) {
                slot.store(c, std::memory_order_release);
                c = nullptr;
                break;
            }
        }
    }
}

int AuditionHandoff::heldCount() const noexcept {
    return static_cast<int>(std::count_if(held_.begin(), held_.end(), [](const RenderedClip* c) { return c != nullptr; }));
}

}  // namespace flowstate::plugin
