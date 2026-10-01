#include <doctest/doctest.h>

#include "flowstate/catalog.h"

#include <string>
#include <vector>

using namespace flowstate;

namespace {

CatalogItem item(std::string id, bool library, std::string title, Role role, std::vector<std::string> genres,
                 std::optional<int> tonic, std::optional<Mode> mode, double lo, double hi, std::int64_t at = 0) {
    CatalogItem it;
    it.id = std::move(id);
    it.library = library;
    it.title = std::move(title);
    it.roles = {role};
    it.genres = std::move(genres);
    it.tonic = tonic;
    it.mode = mode;
    it.tempo = (lo + hi) / 2;
    it.tempoMin = lo;
    it.tempoMax = hi;
    it.createdAtMs = at;
    return it;
}

std::vector<std::string> ids(const std::vector<CatalogItem>& items, const CatalogResult& r) {
    std::vector<std::string> out;
    for (const auto& h : r.hits) out.push_back(items[h.index].id);
    return out;
}

const std::vector<CatalogItem> kItems = {
    item("lib:p/rnb-a", true, "R&B jazz chords 01", Role::Chords, {"rnb", "neo-soul"}, 0, Mode::Major, 112, 128),
    item("lib:p/rnb-b", true, "R&B jazz chords 02", Role::Chords, {"rnb", "neo-soul"}, 6, Mode::Major, 112, 128),
    item("lib:p/bass", true, "Hip-hop bass 02", Role::Bass, {"hip-hop"}, std::nullopt, std::nullopt, 82, 98),
    item("node:n1", false, "Dusty keys", Role::Chords, {"lofi"}, 9, Mode::Minor, 70, 86, 2000),
    item("node:n2", false, "Late bus", Role::Melody, {"lofi"}, 2, Mode::Dorian, 84, 100, 3000),
};

}  // namespace

TEST_CASE("keyFit and tempoFit: musical distance") {
    CHECK(keyFit(0, Mode::Major, 0, Mode::Major) == doctest::Approx(1.0));
    CHECK(keyFit(9, Mode::Minor, 0, Mode::Major) == doctest::Approx(0.9));  // relative minor: same notes
    CHECK(keyFit(7, Mode::Major, 0, Mode::Major) < 0.8);                     // a fifth away
    CHECK(keyFit(7, Mode::Major, 0, Mode::Major) > keyFit(6, Mode::Major, 0, Mode::Major));  // > a tritone away
    CHECK(tempoFit(120, 112, 128) == 1.0);
    CHECK(tempoFit(60, 112, 128) == doctest::Approx(0.8));  // half time
    CHECK(tempoFit(150, 112, 128) < tempoFit(132, 112, 128));
}

TEST_CASE("searchCatalog: library clips and AI results in one list, with filters") {
    CatalogSearch all;
    auto r = searchCatalog(kItems, all);
    CHECK(r.total == 5);
    // No ranking signal: the newest AI results first, then ids.
    CHECK(ids(kItems, r) == std::vector<std::string>{"node:n2", "node:n1", "lib:p/bass", "lib:p/rnb-a", "lib:p/rnb-b"});

    CatalogSearch libOnly;
    libOnly.includeAi = false;
    CHECK(searchCatalog(kItems, libOnly).total == 3);

    CatalogSearch chords;
    chords.roles = {Role::Chords};
    CHECK(searchCatalog(kItems, chords).total == 3);

    CatalogSearch lofi;
    lofi.genres = {"LoFi"};
    CHECK(ids(kItems, searchCatalog(kItems, lofi)) == std::vector<std::string>{"node:n2", "node:n1"});
}

TEST_CASE("searchCatalog: every word must match, and title matches rank first") {
    CatalogSearch s;
    s.text = "neo-soul chords";
    auto r = searchCatalog(kItems, s);
    CHECK(ids(kItems, r) == std::vector<std::string>{"lib:p/rnb-a", "lib:p/rnb-b"});
    s.text = "bass";
    CHECK(ids(kItems, searchCatalog(kItems, s)) == std::vector<std::string>{"lib:p/bass"});
    s.text = "nothing-like-this";
    CHECK(searchCatalog(kItems, s).total == 0);
}

TEST_CASE("searchCatalog: fit ranks by key and tempo, and drops other meters") {
    auto items = kItems;
    items.push_back(item("lib:p/waltz", true, "Waltz chords", Role::Chords, {"jazz"}, 0, Mode::Major, 112, 128));
    items.back().meterNumerator = 3;
    CatalogSearch s;
    s.roles = {Role::Chords};
    s.fit = CatalogFit{0, Mode::Major, 120.0, 4, 4};
    const auto r = searchCatalog(items, s);
    // C major at 120: rnb-a (C major, in range) beats rnb-b (F# major, a tritone away); the A minor AI clip
    // shares the notes but not the tempo. The 3/4 clip is left out.
    CHECK(ids(items, r) == std::vector<std::string>{"lib:p/rnb-a", "lib:p/rnb-b", "node:n1"});
    CHECK(r.hits[0].score > r.hits[1].score);
}

TEST_CASE("searchCatalog: limit and offset page through the total") {
    CatalogSearch s;
    s.limit = 2;
    s.offset = 2;
    const auto r = searchCatalog(kItems, s);
    CHECK(r.total == 5);
    CHECK(ids(kItems, r) == std::vector<std::string>{"lib:p/bass", "lib:p/rnb-a"});
}
