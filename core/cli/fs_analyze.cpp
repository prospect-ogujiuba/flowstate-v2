// fs-analyze: MIDI file -> analysis JSON (lane, key, grid, descriptors, score IR, fidelity) and a
// normalized copy of the MIDI. The library importer (library/) runs it once per pack entry.
#include "flowstate/analyze.h"
#include "flowstate/theory.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

void usage() {
    std::cerr << "usage: fs-analyze --in clip.mid [--lane chords|bass|melody|drums] [--key \"Eb minor\"] [--title T]\n"
                 "                  [--name N] [--style tag1,tag2] [--out-json analysis.json] [--out-mid normalized.mid]\n"
                 "Exit codes: 0 analyzed, 3 the clip can't be imported (reason in the JSON), 2 bad arguments, 1 I/O.\n";
}

bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    std::string in, outJson, outMid, lane, style, key;
    flowstate::AnalyzeOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= argc) {
                std::cerr << "fs-analyze: " << a << " needs a value\n";
                return false;
            }
            dst = argv[++i];
            return true;
        };
        bool ok = true;
        if (a == "--in") ok = value(in);
        else if (a == "--out-json") ok = value(outJson);
        else if (a == "--out-mid") ok = value(outMid);
        else if (a == "--lane") ok = value(lane);
        else if (a == "--title") ok = value(options.title);
        else if (a == "--name") ok = value(options.partName);
        else if (a == "--style") ok = value(style);
        else if (a == "--key") ok = value(key);
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            std::cerr << "fs-analyze: unknown argument " << a << "\n";
            usage();
            return 2;
        }
        if (!ok) return 2;
    }
    if (in.empty()) {
        usage();
        return 2;
    }
    if (!lane.empty()) {
        options.declaredRole = flowstate::roleFromString(lane);
        if (!options.declaredRole) {
            std::cerr << "fs-analyze: unknown lane " << lane << "\n";
            return 2;
        }
    }
    if (!key.empty()) {
        // "Eb minor", "D dorian"
        const auto space = key.find(' ');
        const int tonic = space == std::string::npos ? -1 : flowstate::pitchClassFromName(key.substr(0, space));
        const auto mode = space == std::string::npos ? std::nullopt : flowstate::modeFromString(key.substr(space + 1));
        if (tonic < 0 || !mode) {
            std::cerr << "fs-analyze: --key wants a tonic and a mode, e.g. \"Eb minor\"\n";
            return 2;
        }
        options.keyTonic = tonic;
        options.keyMode = mode;
    }
    std::stringstream tags(style);
    for (std::string t; std::getline(tags, t, ',');)
        if (!t.empty()) options.style.push_back(t);

    std::ifstream f(in, std::ios::binary);
    if (!f) {
        std::cerr << "fs-analyze: cannot read " << in << "\n";
        return 1;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto analysis = flowstate::analyzeMidi(bytes, options);
    const std::string json = flowstate::analysisJson(analysis) + "\n";
    if (outJson.empty()) {
        std::cout << json;
    } else if (!writeFile(outJson, json)) {
        std::cerr << "fs-analyze: cannot write " << outJson << "\n";
        return 1;
    }
    if (analysis.ok && !outMid.empty() &&
        !writeFile(outMid, std::string(analysis.normalizedMidi.begin(), analysis.normalizedMidi.end()))) {
        std::cerr << "fs-analyze: cannot write " << outMid << "\n";
        return 1;
    }
    return analysis.ok ? 0 : 3;
}
