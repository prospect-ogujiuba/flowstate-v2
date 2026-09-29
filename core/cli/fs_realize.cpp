// fs-realize: score IR JSON -> MIDI + note list + report.
#include "flowstate/output.h"
#include "flowstate/realize.h"
#include "flowstate/smf.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void usage() {
    std::cerr << "usage: fs-realize --in score.json [--seed N] [--out-mid out.mid]\n"
                 "                  [--out-notes notes.json] [--out-report report.json] [--no-humanize]\n";
}

bool parseSeed(const std::string& s, std::uint64_t& out) {
    if (s.empty()) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        std::uint64_t next = v * 10 + static_cast<std::uint64_t>(c - '0');
        if (next / 10 != v) return false;  // overflow
        v = next;
    }
    out = v;
    return true;
}

bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    std::string in, outMid, outNotes, outReport;
    flowstate::RealizeOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= argc) {
                std::cerr << "fs-realize: " << a << " needs a value\n";
                return false;
            }
            dst = argv[++i];
            return true;
        };
        if (a == "--in") {
            if (!value(in)) return 2;
        } else if (a == "--out-mid") {
            if (!value(outMid)) return 2;
        } else if (a == "--out-notes") {
            if (!value(outNotes)) return 2;
        } else if (a == "--out-report") {
            if (!value(outReport)) return 2;
        } else if (a == "--seed") {
            std::string s;
            if (!value(s)) return 2;
            if (!parseSeed(s, options.seed)) {
                std::cerr << "fs-realize: --seed must be a non-negative integer\n";
                return 2;
            }
        } else if (a == "--no-humanize") {
            options.humanize = false;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            std::cerr << "fs-realize: unknown argument " << a << "\n";
            usage();
            return 2;
        }
    }
    if (in.empty()) {
        usage();
        return 2;
    }
    std::ifstream f(in, std::ios::binary);
    if (!f) {
        std::cerr << "fs-realize: cannot read " << in << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << f.rdbuf();

    flowstate::Realization r;
    try {
        r = flowstate::realizeJson(ss.str(), options);
    } catch (const flowstate::IrError& e) {
        std::cerr << "fs-realize: invalid score: " << e.what() << "\n";
        return 1;
    }

    if (!outMid.empty()) {
        auto bytes = flowstate::writeSmf(r);
        if (!writeFile(outMid, std::string(bytes.begin(), bytes.end()))) {
            std::cerr << "fs-realize: cannot write " << outMid << "\n";
            return 1;
        }
    }
    if (!outNotes.empty() && !writeFile(outNotes, flowstate::notesJson(r) + "\n")) {
        std::cerr << "fs-realize: cannot write " << outNotes << "\n";
        return 1;
    }
    if (!outReport.empty() && !writeFile(outReport, flowstate::reportJson(r) + "\n")) {
        std::cerr << "fs-realize: cannot write " << outReport << "\n";
        return 1;
    }

    std::cout << "realized \"" << r.title << "\": " << r.bars << " bars, " << r.meterNumerator << "/"
              << r.meterDenominator << ", clip end tick " << r.clipEnd() << "\n";
    for (const auto& p : r.parts)
        std::cout << "  " << p.id << " (" << flowstate::toString(p.role) << ", ch " << p.channel + 1
                  << "): " << p.notes.size() << " notes\n";
    std::cout << "  out-of-key: " << r.outOfKey.size() << ", warnings: " << r.warnings.size() << "\n";
    for (const auto& w : r.warnings) std::cout << "  warning: " << w << "\n";
    return 0;
}
