#include "flowstate/constraints.h"

#include <algorithm>

namespace flowstate {

void sortNotes(std::vector<RawNote>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const RawNote& a, const RawNote& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.pitch < b.pitch;
    });
}

void clipToClip(std::vector<RawNote>& notes, Tick clipEnd) {
    std::vector<RawNote> out;
    out.reserve(notes.size());
    for (auto n : notes) {
        if (n.tick < 0 || n.tick >= clipEnd) continue;
        Tick end = std::min(n.tick + n.dur, clipEnd);
        n.dur = end - n.tick;
        if (n.dur <= 0) continue;
        out.push_back(n);
    }
    notes.swap(out);
}

int foldPitch(int pitch, int low, int high) {
    if (high - low < 11) {
        // Too narrow for every pitch class: fold, then clamp.
        while (pitch < low) pitch += 12;
        while (pitch > high) pitch -= 12;
        return std::clamp(pitch, low, high);
    }
    while (pitch < low) pitch += 12;
    while (pitch > high) pitch -= 12;
    return pitch;
}

int foldRange(std::vector<RawNote>& notes, int low, int high) {
    int unfit = 0;
    for (auto& n : notes) {
        if (n.fixedPitch) continue;
        int f = foldPitch(n.pitch, low, high);
        if (mod12(f) != mod12(n.pitch)) ++unfit;
        n.pitch = f;
    }
    return unfit;
}

void dedupe(std::vector<RawNote>& notes) {
    sortNotes(notes);
    std::vector<RawNote> out;
    out.reserve(notes.size());
    for (const auto& n : notes) {
        bool merged = false;
        for (auto it = out.rbegin(); it != out.rend() && it->tick == n.tick; ++it) {
            if (it->pitch == n.pitch) {
                it->vel = std::max(it->vel, n.vel);
                it->dur = std::max(it->dur, n.dur);
                it->justified = it->justified || n.justified;
                merged = true;
                break;
            }
        }
        if (!merged) out.push_back(n);
    }
    // Same pitch overlapping a later onset: cut at that onset.
    for (std::size_t i = 0; i < out.size(); ++i) {
        for (std::size_t j = i + 1; j < out.size(); ++j) {
            if (out[j].tick >= out[i].tick + out[i].dur) break;
            if (out[j].pitch == out[i].pitch) {
                out[i].dur = out[j].tick - out[i].tick;
                break;
            }
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const RawNote& n) { return n.dur <= 0; }), out.end());
    notes.swap(out);
}

void enforceMonophony(std::vector<RawNote>& notes, bool keepLowest) {
    sortNotes(notes);
    std::vector<RawNote> out;
    out.reserve(notes.size());
    for (const auto& n : notes) {
        if (!out.empty() && out.back().tick == n.tick) {
            // Simultaneous onset: keep one (sorted ascending by pitch).
            if (!keepLowest) out.back() = n;
            continue;
        }
        out.push_back(n);
    }
    for (std::size_t i = 0; i + 1 < out.size(); ++i) {
        Tick limit = out[i + 1].tick - out[i].tick;
        if (out[i].dur > limit) out[i].dur = limit;
    }
    notes.swap(out);
}

void enforceMinLength(std::vector<RawNote>& notes, Tick clipEnd, bool monophonic) {
    sortNotes(notes);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        auto& n = notes[i];
        if (n.dur >= kMinNoteTicks) continue;
        Tick room = clipEnd - n.tick;
        if (monophonic && i + 1 < notes.size()) room = std::min(room, notes[i + 1].tick - n.tick);
        n.dur = std::max<Tick>(n.dur, std::min<Tick>(kMinNoteTicks, room));
    }
    notes.erase(std::remove_if(notes.begin(), notes.end(), [](const RawNote& n) { return n.dur <= 0; }),
                notes.end());
}

std::vector<const RawNote*> findOutOfKey(const std::vector<RawNote>& notes, const Scale& scale) {
    std::vector<const RawNote*> out;
    for (const auto& n : notes)
        if (!n.justified && !scale.contains(n.pitch)) out.push_back(&n);
    return out;
}

}  // namespace flowstate
