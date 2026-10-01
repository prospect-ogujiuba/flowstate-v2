#include "flowstate/steps.h"

#include <algorithm>

namespace flowstate {

char StepPattern::at(int blockBar, int step) const {
    if (bars.empty() || step < 0 || step >= stepsPerBar) return '.';
    const auto& bar = bars[static_cast<std::size_t>(blockBar) % bars.size()];
    return bar[static_cast<std::size_t>(step)];
}

namespace {

// Lengths that are whole multiples or divisors of the bar are a resolution, not a slip.
bool evenlyRelated(std::size_t len, std::size_t n) { return len > 0 && (n % len == 0 || len % n == 0); }

// Pads or truncates a bar whose length is not evenly related to `n`.
std::string fitBar(std::string bar, std::size_t n, const std::string& label, std::vector<std::string>& warnings) {
    if (bar.empty()) {
        warnings.push_back(label + "; treated as rests");
        return std::string(n, '.');
    }
    if (bar.size() < n) {
        warnings.push_back(label + "; padded with rests");
        bar.append(n - bar.size(), '.');
        return bar;
    }
    warnings.push_back(label + "; truncated");
    return bar.substr(0, n);
}

// Spreads each token over `k` steps: a struck or held step keeps sounding ('-'), a rest stays a rest.
std::string expand(const std::string& bar, std::size_t k) {
    std::string out;
    out.reserve(bar.size() * k);
    for (char t : bar) {
        out += t;
        out.append(k - 1, t == '.' ? '.' : '-');
    }
    return out;
}

}  // namespace

StepPattern parseSteps(const std::string& text, int stepsPerBar, const std::string& allowed,
                       const std::string& where, std::vector<std::string>& warnings) {
    StepPattern pat;
    pat.stepsPerBar = std::max(1, stepsPerBar);
    std::string clean;
    bool hasBarLines = false;
    bool badToken = false;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '|') {
            hasBarLines = true;
            clean += ch;
            continue;
        }
        if (allowed.find(ch) == std::string::npos) {
            badToken = true;
            clean += '.';
        } else {
            clean += ch;
        }
    }
    if (badToken) warnings.push_back(where + ": unsupported step tokens treated as rests");
    // Trailing/leading bar lines are formatting, not empty bars.
    while (!clean.empty() && clean.back() == '|') clean.pop_back();
    while (!clean.empty() && clean.front() == '|') clean.erase(clean.begin());
    if (clean.empty()) return pat;

    std::vector<std::string> raw;
    if (hasBarLines) {
        std::string cur;
        for (char ch : clean) {
            if (ch == '|') {
                raw.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        raw.push_back(cur);
    } else {
        const auto n = static_cast<std::size_t>(pat.stepsPerBar);
        if (clean.size() <= n) {
            raw.push_back(clean);
        } else {
            for (std::size_t i = 0; i < clean.size(); i += n) raw.push_back(clean.substr(i, n));
        }
    }
    // The pattern's resolution: the finest of the bar and every evenly related bar length (capped).
    const auto n = static_cast<std::size_t>(pat.stepsPerBar);
    constexpr std::size_t kMaxScale = 8;
    std::size_t resolution = n;
    for (const auto& bar : raw)
        if (evenlyRelated(bar.size(), n) && bar.size() > resolution && bar.size() / n <= kMaxScale)
            resolution = bar.size();
    pat.stepsPerBar = static_cast<int>(resolution);
    pat.scale = static_cast<int>(resolution / n);

    for (std::size_t i = 0; i < raw.size(); ++i) {
        std::string bar = raw[i];
        const std::string label = where + ": bar " + std::to_string(i + 1) + " has " + std::to_string(bar.size()) +
                                  " steps, expected " + std::to_string(n);
        if (bar.size() != n) {
            if (evenlyRelated(bar.size(), n) && resolution % bar.size() == 0)
                warnings.push_back(label + "; read as " + std::to_string(bar.size()) + " steps per bar");
            else
                bar = fitBar(bar, n, label, warnings);
        }
        pat.bars.push_back(bar.size() == resolution ? bar : expand(bar, resolution / bar.size()));
    }
    return pat;
}

std::vector<StepEvent> stepEvents(const StepPattern& pattern, int blockBars) {
    std::vector<StepEvent> events;
    if (pattern.empty()) return events;
    bool open = false;
    for (int bar = 0; bar < blockBars; ++bar) {
        for (int s = 0; s < pattern.stepsPerBar; ++s) {
            char t = pattern.at(bar, s);
            int global = bar * pattern.stepsPerBar + s;
            if (t == '.') {
                open = false;
            } else if (t == '-') {
                if (open) events.back().length++;
            } else {
                events.push_back({t, global, 1});
                open = true;
            }
        }
    }
    return events;
}

}  // namespace flowstate
