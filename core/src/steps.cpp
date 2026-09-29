#include "flowstate/steps.h"

#include <algorithm>

namespace flowstate {

char StepPattern::at(int blockBar, int step) const {
    if (bars.empty() || step < 0 || step >= stepsPerBar) return '.';
    const auto& bar = bars[static_cast<std::size_t>(blockBar) % bars.size()];
    return bar[static_cast<std::size_t>(step)];
}

namespace {

std::string fitBar(std::string bar, int stepsPerBar, int barIndex, const std::string& where,
                   std::vector<std::string>& warnings) {
    const auto n = static_cast<std::size_t>(stepsPerBar);
    if (bar.size() == n) return bar;
    const std::string label = where + ": bar " + std::to_string(barIndex + 1) + " has " +
                              std::to_string(bar.size()) + " steps, expected " + std::to_string(n);
    if (bar.empty()) {
        warnings.push_back(label + "; treated as rests");
        return std::string(n, '.');
    }
    if (bar.size() < n) {
        if (n % bar.size() == 0) {
            warnings.push_back(label + "; repeated to fill the bar");
            std::string out;
            while (out.size() < n) out += bar;
            return out;
        }
        warnings.push_back(label + "; padded with rests");
        bar.append(n - bar.size(), '.');
        return bar;
    }
    warnings.push_back(label + "; truncated");
    return bar.substr(0, n);
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
    for (std::size_t i = 0; i < raw.size(); ++i)
        pat.bars.push_back(fitBar(raw[i], pat.stepsPerBar, static_cast<int>(i), where, warnings));
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
