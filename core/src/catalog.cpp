#include "flowstate/catalog.h"

#include "flowstate/theory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace flowstate {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string w;
    for (char c : lower(text) + " ") {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    return out;
}

std::set<int> scalePcs(int tonic, Mode mode) {
    const Scale s = makeScale(mod12(tonic), mode);
    std::set<int> out;
    for (int iv : s.intervals) out.insert(mod12(s.tonic + iv));
    return out;
}

}  // namespace

double keyFit(int tonicA, Mode modeA, int tonicB, Mode modeB) {
    const auto a = scalePcs(tonicA, modeA), b = scalePcs(tonicB, modeB);
    std::vector<int> inter, uni;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(inter));
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(uni));
    const double jaccard = uni.empty() ? 0.0 : static_cast<double>(inter.size()) / static_cast<double>(uni.size());
    return jaccard * 0.85 + (jaccard == 1.0 ? 0.05 : 0.0) + (mod12(tonicA) == mod12(tonicB) ? 0.1 : 0.0);
}

double tempoFit(double t, double lo, double hi) {
    auto inside = [&](double x) { return x >= lo && x <= hi; };
    if (inside(t)) return 1.0;
    if (inside(t * 2.0) || inside(t / 2.0)) return 0.8;
    const double gap = t < lo ? lo - t : t - hi;
    return std::max(0.0, 1.0 - gap / 40.0) * 0.75;
}

CatalogResult searchCatalog(const std::vector<CatalogItem>& items, const CatalogSearch& search) {
    const auto query = words(search.text);
    std::set<std::string> genres;
    for (const auto& g : search.genres) genres.insert(lower(g));

    struct Scored {
        std::size_t index;
        double score;
    };
    std::vector<Scored> found;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const CatalogItem& it = items[i];
        if (it.library ? !search.includeLibrary : !search.includeAi) continue;
        if (!search.roles.empty() &&
            std::none_of(it.roles.begin(), it.roles.end(), [&](Role r) {
                return std::find(search.roles.begin(), search.roles.end(), r) != search.roles.end();
            }))
            continue;
        if (!genres.empty() &&
            std::none_of(it.genres.begin(), it.genres.end(), [&](const std::string& g) { return genres.count(lower(g)) > 0; }))
            continue;
        if (search.fit && (it.meterNumerator != search.fit->meterNumerator || it.meterDenominator != search.fit->meterDenominator))
            continue;

        // Every query word must match; a title match counts double.
        const std::string title = lower(it.title);
        std::string rest = lower(it.feel + " " + it.producer + " " + it.prompt);
        for (const auto& g : it.genres) rest += " " + lower(g);
        for (const auto& t : it.tags) rest += " " + lower(t);
        for (Role r : it.roles) rest += std::string(" ") + toString(r);
        double score = 0.0;
        bool all = true;
        for (const auto& w : query) {
            if (title.find(w) != std::string::npos) score += 2.0;
            else if (rest.find(w) != std::string::npos) score += 1.0;
            else {
                all = false;
                break;
            }
        }
        if (!all) continue;
        if (search.fit) {
            const double kf = it.tonic && it.mode ? keyFit(*it.tonic, *it.mode, search.fit->tonic, search.fit->mode) : 0.5;
            score += 2.0 * kf + 2.0 * tempoFit(search.fit->tempo, it.tempoMin, it.tempoMax);
        }
        found.push_back({i, score});
    }
    std::stable_sort(found.begin(), found.end(), [&](const Scored& a, const Scored& b) {
        if (std::abs(a.score - b.score) > 1e-9) return a.score > b.score;
        const auto& x = items[a.index];
        const auto& y = items[b.index];
        if (x.createdAtMs != y.createdAtMs) return x.createdAtMs > y.createdAtMs;
        return x.id < y.id;
    });
    CatalogResult r;
    r.total = static_cast<int>(found.size());
    const auto from = static_cast<std::size_t>(std::max(0, search.offset));
    for (std::size_t k = from; k < found.size() && r.hits.size() < static_cast<std::size_t>(std::max(0, search.limit)); ++k)
        r.hits.push_back({found[k].index, found[k].score});
    return r;
}

}  // namespace flowstate
