#include <doctest/doctest.h>

#include "flowstate/realize.h"
#include "flowstate/smf.h"
#include "test_util.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace flowstate;

namespace {

struct Track {
    std::string name;
    int noteOns = 0;
    int noteOffs = 0;
    int channel = -1;
    std::uint32_t endTick = 0;
    bool tempo = false;
    bool timeSig = false;
    std::vector<std::uint8_t> timeSigData;
};

std::uint32_t be(const std::vector<std::uint8_t>& d, std::size_t at, int n) {
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | d[at + static_cast<std::size_t>(i)];
    return v;
}

std::uint32_t varLen(const std::vector<std::uint8_t>& d, std::size_t& pos) {
    std::uint32_t v = 0;
    while (true) {
        std::uint8_t b = d[pos++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
}

std::vector<Track> parse(const std::vector<std::uint8_t>& d, int& format, int& ppq) {
    REQUIRE(d.size() >= 14);
    REQUIRE(std::string(d.begin(), d.begin() + 4) == "MThd");
    format = static_cast<int>(be(d, 8, 2));
    const auto ntracks = be(d, 10, 2);
    ppq = static_cast<int>(be(d, 12, 2));
    std::vector<Track> tracks;
    std::size_t pos = 14;
    for (std::uint32_t t = 0; t < ntracks; ++t) {
        REQUIRE(std::string(d.begin() + static_cast<long>(pos), d.begin() + static_cast<long>(pos) + 4) == "MTrk");
        const auto len = be(d, pos + 4, 4);
        std::size_t p = pos + 8;
        const std::size_t end = p + len;
        Track tr;
        std::uint32_t tick = 0;
        while (p < end) {
            tick += varLen(d, p);
            std::uint8_t status = d[p++];
            if (status == 0xFF) {
                std::uint8_t type = d[p++];
                auto n = varLen(d, p);
                if (type == 0x03) tr.name.assign(d.begin() + static_cast<long>(p), d.begin() + static_cast<long>(p + n));
                if (type == 0x51) tr.tempo = true;
                if (type == 0x58) {
                    tr.timeSig = true;
                    tr.timeSigData.assign(d.begin() + static_cast<long>(p), d.begin() + static_cast<long>(p + n));
                }
                if (type == 0x2F) tr.endTick = tick;
                p += n;
            } else {
                const int kind = status & 0xF0;
                tr.channel = status & 0x0F;
                const std::uint8_t vel = d[p + 1];
                p += 2;
                if (kind == 0x90 && vel > 0) ++tr.noteOns;
                else if (kind == 0x80 || kind == 0x90) ++tr.noteOffs;
            }
        }
        CHECK(p == end);
        tracks.push_back(tr);
        pos = end;
    }
    CHECK(pos == d.size());
    return tracks;
}

}  // namespace

TEST_CASE("SMF type 1: conductor track, one named track per part, drums on channel 10") {
    for (const char* name : {"example.json", "six_eight.json", "seven_eight.json"}) {
        CAPTURE(name);
        Realization r = realizeJson(readFixture(name));
        auto bytes = writeSmf(r);
        int format = 0, ppq = 0;
        auto tracks = parse(bytes, format, ppq);
        CHECK(format == 1);
        CHECK(ppq == 960);
        REQUIRE(tracks.size() == r.parts.size() + 1);
        CHECK(tracks[0].tempo);
        CHECK(tracks[0].timeSig);
        REQUIRE(tracks[0].timeSigData.size() == 4);
        CHECK(tracks[0].timeSigData[0] == r.meterNumerator);
        CHECK((1 << tracks[0].timeSigData[1]) == r.meterDenominator);
        for (std::size_t i = 0; i < r.parts.size(); ++i) {
            const auto& part = r.parts[i];
            const auto& tr = tracks[i + 1];
            CHECK(tr.name == part.name);
            CHECK(tr.noteOns == static_cast<int>(part.notes.size()));
            CHECK(tr.noteOffs == static_cast<int>(part.notes.size()));
            CHECK(tr.channel == part.channel);
            CHECK(tr.endTick == static_cast<std::uint32_t>(r.clipEnd()));
            if (part.role == Role::Drums) CHECK(tr.channel == 9);
        }
    }
}
