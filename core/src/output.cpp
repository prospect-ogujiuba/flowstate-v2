#include "flowstate/output.h"

#include "flowstate/groove.h"

#include <algorithm>
#include <map>

#include <nlohmann/json.hpp>

namespace flowstate {

using nlohmann::ordered_json;

std::string notesJson(const Realization& r, int indent) {
    ordered_json j;
    j["ppq"] = r.ppq;
    j["bars"] = r.bars;
    j["ticksPerBar"] = r.ticksPerBar;
    j["tempo"] = r.tempo;
    j["meter"] = {r.meterNumerator, r.meterDenominator};
    ordered_json parts = ordered_json::array();
    for (const auto& p : r.parts) {
        ordered_json pj;
        pj["id"] = p.id;
        pj["role"] = toString(p.role);
        pj["name"] = p.name;
        pj["channel"] = p.channel + 1;
        ordered_json notes = ordered_json::array();
        for (const auto& n : p.notes) {
            ordered_json nj;
            nj["tick"] = n.tick;
            nj["dur"] = n.dur;
            nj["pitch"] = n.pitch;
            nj["vel"] = n.vel;
            if (!n.sublane.empty()) nj["sublane"] = n.sublane;
            notes.push_back(nj);
        }
        pj["notes"] = notes;
        if (p.role == Role::Drums) {
            // Fixed voice -> GM note -> v1 sublane table for per-sublane export.
            ordered_json voices = ordered_json::array();
            for (int v = 0; v <= static_cast<int>(DrumVoice::Cowbell); ++v) {
                const auto voice = static_cast<DrumVoice>(v);
                const int note = drumVoiceNote(voice);
                voices.push_back({{"voice", toString(voice)}, {"note", note}, {"sublane", drumSublaneForNote(note)}});
            }
            pj["voices"] = voices;
        }
        parts.push_back(pj);
    }
    j["parts"] = parts;
    return j.dump(indent);
}

std::string reportJson(const Realization& r, int indent) {
    ordered_json j;
    j["warnings"] = r.warnings;
    ordered_json ook = ordered_json::array();
    for (const auto& o : r.outOfKey) ook.push_back({{"part", o.part}, {"tick", o.tick}, {"pitch", o.pitch}});
    j["outOfKey"] = ook;

    ordered_json stats;
    std::size_t total = 0;
    ordered_json parts = ordered_json::array();
    for (const auto& p : r.parts) {
        ordered_json pj;
        pj["id"] = p.id;
        pj["role"] = toString(p.role);
        pj["notes"] = p.notes.size();
        total += p.notes.size();
        if (!p.notes.empty()) {
            int lo = 127, hi = 0;
            for (const auto& n : p.notes) {
                lo = std::min(lo, n.pitch);
                hi = std::max(hi, n.pitch);
            }
            pj["minPitch"] = lo;
            pj["maxPitch"] = hi;
        } else {
            pj["minPitch"] = nullptr;
            pj["maxPitch"] = nullptr;
        }
        pj["rangeLow"] = p.low;
        pj["rangeHigh"] = p.high;
        std::size_t outOfKey = 0;
        for (const auto& o : r.outOfKey)
            if (o.part == p.id) ++outOfKey;
        pj["outOfKey"] = outOfKey;
        if (p.role == Role::Drums) {
            std::map<std::string, std::size_t> sub;
            for (const auto& n : p.notes) sub[n.sublane]++;
            ordered_json sj = ordered_json::array();
            for (const auto& [name, count] : sub) sj.push_back({{"sublane", name}, {"notes", count}});
            pj["sublanes"] = sj;
        }
        parts.push_back(pj);
    }
    stats["totalNotes"] = total;
    stats["clipEndTick"] = r.clipEnd();
    stats["parts"] = parts;
    j["stats"] = stats;
    return j.dump(indent);
}

std::uint64_t notesChecksum(const Realization& r) {
    std::string canon;
    for (const auto& p : r.parts) {
        canon += p.id + ":" + std::to_string(p.channel) + ";";
        for (const auto& n : p.notes)
            canon += std::to_string(n.tick) + "," + std::to_string(n.dur) + "," + std::to_string(n.pitch) + "," +
                     std::to_string(n.vel) + ";";
    }
    return fnv1a(canon);
}

}  // namespace flowstate
