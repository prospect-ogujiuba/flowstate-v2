// The built-in MIDI library as the plugin sees it: the generated LibraryCatalog
// (library/catalog/catalog.json, bundled into the plugin) plus the glue that puts library clips and
// AI results into one catalog for search, preview, use and drag. JUCE-free. Search and ranking are
// core's (flowstate/catalog.h). Semantics: docs/library.md.
#pragma once

#include "flowstate/catalog.h"
#include "session/Session.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowstate::plugin {

class Library {
public:
    // An empty library (no bundled catalog, e.g. a test or a stripped build).
    Library() = default;
    // Throws fb::ParseError or nlohmann::json::exception on a malformed catalog.
    static Library parse(const std::string& catalogJson);

    const std::vector<fb::LibraryClip>& clips() const { return catalog_.clips; }
    const std::vector<fb::LibraryPack>& packs() const { return catalog_.packs; }
    const fb::LibraryClip* find(const std::string& entryId) const;

private:
    fb::LibraryCatalog catalog_;
};

// The catalog entry for an AI result (a lineage node), read from its score.
fb::CatalogEntry entryForNode(const fb::LineageNode& node);

// Lineage nodes that are AI results: not sketches, and not started from a library clip (those are
// listed as the clip itself).
bool isAiResult(const fb::LineageNode& node);

// core's search view of an entry.
CatalogItem catalogItem(const fb::CatalogEntry& entry);

// A library clip's normalized MIDI as a Clip, for drag-out. Nullopt with `error` set when it can't
// be read.
std::optional<fb::Clip> clipFromMidi(const std::vector<std::uint8_t>& bytes, const fb::CatalogEntry& entry,
                                     std::string& error);

}  // namespace flowstate::plugin
