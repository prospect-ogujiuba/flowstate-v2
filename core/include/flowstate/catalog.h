// Catalog search: library clips and AI results in one list, filtered and ranked. Ranking by fit to
// the session (key, tempo, meter) is theory, so it lives here. Semantics: docs/library.md.
#pragma once

#include "flowstate/ir.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowstate {

struct CatalogItem {
    std::string id;
    bool library = true;  // false = an AI result
    std::string title;
    std::vector<Role> roles;
    std::vector<std::string> genres;
    std::vector<std::string> tags;
    std::string feel;
    std::string producer;
    std::string prompt;
    std::optional<int> tonic;  // pitch class; unset = key not known
    std::optional<Mode> mode;
    double tempo = 120.0;
    double tempoMin = 112.0;
    double tempoMax = 128.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
    std::int64_t createdAtMs = 0;
};

struct CatalogFit {
    int tonic = 0;
    Mode mode = Mode::Major;
    double tempo = 120.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
};

struct CatalogSearch {
    std::string text;               // every word must match (case-insensitive) somewhere
    bool includeLibrary = true;
    bool includeAi = true;
    std::vector<Role> roles;        // empty = any
    std::vector<std::string> genres;  // empty = any
    std::optional<CatalogFit> fit;  // rank by fit, and leave out other meters
    int limit = 20;
    int offset = 0;
};

struct CatalogHit {
    std::size_t index = 0;  // into the items
    double score = 0.0;
};

struct CatalogResult {
    int total = 0;  // matches before limit and offset
    std::vector<CatalogHit> hits;
};

// 0..1. Shared scale tones (Jaccard of the two scales' pitch classes), plus a little for the same
// tonic: the same key is 1, its relative 0.9, a fifth away about 0.65, a tritone away low.
double keyFit(int tonicA, Mode modeA, int tonicB, Mode modeB);
// 0..1. 1 inside the clip's tempo range, 0.8 at half or double time, falling off over 40 BPM.
double tempoFit(double sessionTempo, double tempoMin, double tempoMax);

// Deterministic: ties go to the newer AI result, then to the id.
CatalogResult searchCatalog(const std::vector<CatalogItem>& items, const CatalogSearch& search);

}  // namespace flowstate
