#include "session/Library.h"

#include "flowstate/midi_read.h"
#include "flowstate/theory.h"

#include <algorithm>

namespace flowstate::plugin {

Library Library::parse(const std::string& catalogJson) {
    Library lib;
    fb::from_json(nlohmann::json::parse(catalogJson), lib.catalog_);
    return lib;
}

const fb::LibraryClip* Library::find(const std::string& entryId) const {
    for (const auto& c : catalog_.clips)
        if (c.entry.id == entryId) return &c;
    return nullptr;
}

bool isAiResult(const fb::LineageNode& node) { return !node.entryId && node.kind != fb::NodeKind::Sketch; }

fb::CatalogEntry entryForNode(const fb::LineageNode& node) {
    const auto& s = node.score;
    const auto ctx = s.value("context", nlohmann::json::object());
    fb::CatalogEntry e;
    e.id = "node:" + node.id;
    e.origin = fb::CatalogOrigin::Ai;
    e.title = s.value("title", std::string());
    if (const auto parts = s.find("parts"); parts != s.end() && parts->is_array())
        for (const auto& p : *parts)
            if (const auto r = fb::parseRole(p.value("role", std::string())); r && std::find(e.roles.begin(), e.roles.end(), *r) == e.roles.end())
                e.roles.push_back(*r);
    if (const auto style = ctx.find("style"); style != ctx.end() && style->is_array())
        for (const auto& t : *style)
            if (t.is_string()) e.genres.push_back(t.get<std::string>());
    e.tonic = fb::parseTonic(ctx.value("tonic", std::string()));
    e.mode = fb::parseMode(ctx.value("mode", std::string()));
    if (!e.tonic || !e.mode) {
        e.tonic.reset();
        e.mode.reset();
    }
    e.keyNote = e.tonic ? std::optional<std::string>("From the plan") : std::nullopt;
    e.tempo = ctx.value("tempo", 120.0);
    e.tempoMin = std::max(20.0, e.tempo - 8.0);
    e.tempoMax = std::min(400.0, e.tempo + 8.0);
    e.meterNumerator = std::clamp(ctx.value("meterNumerator", 4), 1, 32);
    e.meterDenominator = std::clamp(ctx.value("meterDenominator", 4), 1, 32);
    e.bars = std::clamp(ctx.value("bars", 4), 1, 64);
    e.nodeId = node.id;
    e.prompt = node.prompt;
    e.kind = node.kind;
    e.createdAtMs = node.createdAtMs;
    return e;
}

CatalogItem catalogItem(const fb::CatalogEntry& e) {
    CatalogItem it;
    it.id = e.id;
    it.library = e.origin == fb::CatalogOrigin::Library;
    it.title = e.title;
    for (const auto r : e.roles)
        if (const auto role = roleFromString(fb::toString(r))) it.roles.push_back(*role);
    it.genres = e.genres;
    it.tags = e.tags;
    it.feel = e.feel.value_or("");
    it.producer = e.credit ? e.credit->producer : "";
    it.prompt = e.prompt.value_or("");
    if (e.tonic && e.mode) {
        it.tonic = pitchClassFromName(fb::toString(*e.tonic));
        it.mode = modeFromString(fb::toString(*e.mode));
    }
    it.tempo = e.tempo;
    it.tempoMin = e.tempoMin;
    it.tempoMax = e.tempoMax;
    it.meterNumerator = e.meterNumerator;
    it.meterDenominator = e.meterDenominator;
    it.createdAtMs = e.createdAtMs.value_or(0);
    return it;
}

std::optional<fb::Clip> clipFromMidi(const std::vector<std::uint8_t>& bytes, const fb::CatalogEntry& entry, std::string& error) {
    const auto read = readSmf(bytes);
    if (!read.data) {
        error = read.error->message;
        return std::nullopt;
    }
    const int ticksPerBar = kPpq * 4 / entry.meterDenominator * entry.meterNumerator;
    fb::Clip clip;
    clip.ppq = kPpq;
    clip.bars = entry.bars;
    clip.ticksPerBar = ticksPerBar;
    fb::ClipPart part;
    part.role = entry.roles.empty() ? fb::Role::Chords : entry.roles.front();
    part.partId = fb::toString(part.role);
    part.name = entry.title;
    const bool drums = part.role == fb::Role::Drums;
    part.channel = drums ? 10 : 1;
    for (const auto& n : read.data->notes)
        part.notes.push_back({static_cast<int>(n.tick), static_cast<int>(std::max<Tick>(1, n.dur)), n.pitch, n.vel});
    if (drums) part.voices = drumVoiceMap();
    clip.parts.push_back(std::move(part));
    return clip;
}

}  // namespace flowstate::plugin
