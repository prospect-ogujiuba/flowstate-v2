#include <doctest/doctest.h>

#include "flowstate/analyze.h"
#include "flowstate/midi_read.h"
#include "flowstate/realize.h"
#include "flowstate/smf.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace flowstate;
using nlohmann::json;

namespace {

// One realized part as its own SMF, the way a library pack ships a lane.
std::vector<std::uint8_t> partMidi(const std::string& scoreJson, const std::string& partId) {
    RealizeOptions o;
    o.humanize = false;
    Realization r = realizeJson(scoreJson, o);
    std::vector<PartRealization> keep;
    for (auto& p : r.parts)
        if (p.id == partId) keep.push_back(p);
    r.parts = keep;
    REQUIRE(r.parts.size() == 1);
    return writeSmf(r);
}

std::string scoreWith(const std::string& context, const std::string& harmony, const std::string& motifs,
                      const std::string& part) {
    return R"({"ir":"flowstate.score.v0","title":"t","context":)" + context +
           R"(,"form":[],"harmony":")" + harmony + R"(","motifs":)" + motifs + R"(,"parts":[)" + part + "]}";
}

const std::string kCMajor4 =
    R"({"tempo":96,"meterNumerator":4,"meterDenominator":4,"tonic":"C","mode":"major","bars":4,"swing":0,"style":[]})";

AnalyzeOptions declared(std::optional<Role> role, const std::string& name = "", std::vector<std::string> style = {}) {
    AnalyzeOptions o;
    o.declaredRole = role;
    o.title = name.empty() ? "x" : name;
    o.partName = name;
    o.style = std::move(style);
    return o;
}

std::vector<std::uint8_t> bytes(std::initializer_list<int> xs) {
    std::vector<std::uint8_t> out;
    for (int x : xs) out.push_back(static_cast<std::uint8_t>(x));
    return out;
}

// MThd (format 0, one track, `ppq`) + one MTrk with `events`.
std::vector<std::uint8_t> smf(int ppq, std::vector<std::uint8_t> events) {
    auto out = bytes({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, (ppq >> 8) & 0xFF, ppq & 0xFF, 'M', 'T', 'r', 'k'});
    const auto n = events.size();
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>((n >> s) & 0xFF));
    out.insert(out.end(), events.begin(), events.end());
    return out;
}

}  // namespace

TEST_CASE("readSmf: rejects what it can't read, with a reason") {
    CHECK((readSmf({}).error->code == MidiReadErrorCode::Empty));
    CHECK((readSmf(bytes({'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})).error->code == MidiReadErrorCode::Malformed));
    auto smpte = smf(480, bytes({0, 0x90, 60, 100, 10, 0x80, 60, 0, 0, 0xFF, 0x2F, 0}));
    smpte[12] = 0xE7;  // -25 fps
    CHECK((readSmf(smpte).error->code == MidiReadErrorCode::UnsupportedTiming));
    CHECK((readSmf(smf(480, bytes({0, 0xFF, 0x2F, 0}))).error->code == MidiReadErrorCode::NoNotes));
    auto truncated = smf(480, bytes({0, 0x90, 60, 100, 10, 0x80, 60, 0, 0, 0xFF, 0x2F, 0}));
    truncated.resize(truncated.size() - 6);
    CHECK((readSmf(truncated).error->code == MidiReadErrorCode::Malformed));
}

TEST_CASE("readSmf: running status, velocity-0 note-offs, PPQ rescaling, tempo and meter") {
    // 480 PPQ: tempo 100 BPM (600000 us), 3/4, then C4 and E4 under running status, ended by velocity 0.
    const auto data = smf(480, bytes({0, 0xFF, 0x51, 3, 0x09, 0x27, 0xC0,
                                      0, 0xFF, 0x58, 4, 3, 2, 24, 8,
                                      0, 0x90, 60, 100,
                                      0, 64, 90,
                                      0x83, 0x60, 60, 0,  // 480 ticks later, running status
                                      0, 64, 0,
                                      0, 0xFF, 0x2F, 0}));
    const auto r = readSmf(data);
    REQUIRE(r.data);
    CHECK(r.data->sourcePpq == 480);
    CHECK(*r.data->tempo == doctest::Approx(100.0));
    CHECK(*r.data->meterNumerator == 3);
    CHECK(*r.data->meterDenominator == 4);
    REQUIRE(r.data->notes.size() == 2);
    CHECK(r.data->notes[0].pitch == 60);
    CHECK(r.data->notes[0].dur == kPpq);  // 480 at 480 PPQ = one quarter = 960 at kPpq
    CHECK(r.data->notes[1].vel == 90);
}

TEST_CASE("readSmf: a note-on without a note-off ends at the track end, with a warning") {
    const auto r = readSmf(smf(960, bytes({0, 0x90, 60, 100, 0x87, 0x40, 0xFF, 0x2F, 0})));
    REQUIRE(r.data);
    CHECK(r.data->notes[0].dur == 960);
    CHECK_FALSE(r.data->warnings.empty());
}

TEST_CASE("nameChord: common shapes and slash basses") {
    CHECK(nameChord({60, 64, 67}, false) == "C");
    CHECK(nameChord({57, 60, 64, 67}, false) == "Am7");
    CHECK(nameChord({40, 55, 60}, false) == "C/E");
    CHECK(nameChord({50, 53, 57, 60, 64}, false) == "Dm9");
    CHECK(nameChord({43, 59, 62, 65}, false) == "G7");
    CHECK(nameChord({44, 64, 68, 71}, false) == "E/G#");
    CHECK(nameChord({59, 60, 64, 67}, false) == "Cmaj7");  // B is the bottom voice, not a bass
    CHECK(nameChord({58, 62, 65}, true) == "Bb");
}

TEST_CASE("estimateKey: a clear scale is reliable; a short fragment says why it is not") {
    std::vector<MidiNote> scale;
    Tick t = 0;
    for (int p : {60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60, 55, 60}) scale.push_back({(t += 960) - 960, 960, p, 90, 0, 0});
    const auto k = estimateKey(scale);
    CHECK(k.reliable);
    CHECK(k.tonic == 0);
    CHECK((k.mode == Mode::Major));

    const auto few = estimateKey({{0, 960, 60, 90, 0, 0}, {960, 960, 64, 90, 0, 0}});
    CHECK_FALSE(few.reliable);
    CHECK(few.reason == "too few notes to tell the key");
}

TEST_CASE("analyze: a chord part round-trips to its chord symbols and rhythm") {
    const std::string part =
        R"({"id":"keys","role":"chords","name":"Keys","low":"C3","high":"C5","grid":4,"velocity":80,"blocks":[{"startBar":1,"endBar":4,"rhythm":"x--- ..x- x--- ....","voicing":"close"}]})";
    const auto score = scoreWith(kCMajor4, "Cmaj7:4 | Am7:4 | Dm7:4 | G7:4", "[]", part);
    const auto a = analyzeMidi(partMidi(score, "keys"));
    REQUIRE(a.ok);
    CHECK((a.role == Role::Chords));
    CHECK(a.bars == 4);
    CHECK(a.tempo == doctest::Approx(96.0));
    CHECK(a.harmony == std::vector<std::string>{"Cmaj7", "Am7", "Dm7", "G7"});
    CHECK(a.grid.grid == 2);  // every onset and end is on an eighth: the coarsest grid that fits
    CHECK(a.fidelity.rhythm == doctest::Approx(1.0));
    CHECK(a.fidelity.pitch >= 0.9);

    // The IR it wrote is valid and re-realizes.
    std::vector<std::string> warnings;
    const Score s = parseScore(a.scoreJson, warnings);
    CHECK(s.parts.size() == 1);
    CHECK(s.context.bars == 4);
}

TEST_CASE("analyze: a melody round-trips exactly through scale degrees") {
    const std::string motifs = R"([{"id":"m1","notes":"5:.75! 4:.25 b3:1 r:.5 1:1.5 | 2:.5 3:.5 5:1 6+:1 r:1"}])";
    const std::string part =
        R"({"id":"lead","role":"melody","name":"Lead","low":"C4","high":"C6","grid":4,"velocity":90,"blocks":[{"startBar":1,"endBar":2,"motif":"m1","repeatEvery":0}]})";
    const std::string ctx =
        R"({"tempo":110,"meterNumerator":4,"meterDenominator":4,"tonic":"D","mode":"dorian","bars":2,"swing":0,"style":[]})";
    const auto a = analyzeMidi(partMidi(scoreWith(ctx, "", motifs, part), "lead"), declared(Role::Melody, "Lead"));
    REQUIRE(a.ok);
    CHECK((a.role == Role::Melody));
    CHECK(a.fidelity.rhythm == doctest::Approx(1.0));
    CHECK(a.fidelity.notes == doctest::Approx(1.0));
    CHECK(a.fidelity.literalNotes == 0);
}

TEST_CASE("analyze: a bass part round-trips its rhythm; tones the tokens can't name stay literal") {
    const std::string part =
        R"({"id":"bass","role":"bass","name":"Bass","low":"E1","high":"G2","grid":4,"velocity":95,"blocks":[{"startBar":1,"endBar":4,"rhythm":"R--- ..5. R-.. ..a."}]})";
    const auto score = scoreWith(kCMajor4, "C:4 | Am:4 | F:4 | G:4", "[]", part);
    const auto a = analyzeMidi(partMidi(score, "bass"));
    REQUIRE(a.ok);
    CHECK((a.role == Role::Bass));
    CHECK(a.fidelity.rhythm == doctest::Approx(1.0));
    CHECK(a.fidelity.notes >= 0.75);
}

TEST_CASE("analyze: GM drums round-trip to voice lanes") {
    const std::string part =
        R"({"id":"drums","role":"drums","name":"Kit","low":"C1","high":"C6","grid":4,"velocity":96,"blocks":[{"startBar":1,"endBar":4,"fill":"none","drums":[)"
        R"({"voice":"kick","steps":"x.....x. ..x....."},{"voice":"snare","steps":"....x... ....x..."},{"voice":"closed_hat","steps":"x.x.x.x. x.x.x.x."}]}]})";
    const auto a = analyzeMidi(partMidi(scoreWith(kCMajor4, "", "[]", part), "drums"));
    REQUIRE(a.ok);
    CHECK((a.role == Role::Drums));
    CHECK(a.key.reason == "drums have no key");
    CHECK(a.fidelity.rhythm == doctest::Approx(1.0));
    CHECK(a.fidelity.notes == doctest::Approx(1.0));
    const auto ir = json::parse(a.scoreJson);
    CHECK(ir["parts"][0]["blocks"][0]["drums"].size() == 3);
}

TEST_CASE("analyze: lane rules fail with a precise code") {
    const std::string chords =
        R"({"id":"keys","role":"chords","name":"Keys","low":"C3","high":"C5","grid":4,"velocity":80,"blocks":[{"startBar":1,"endBar":4,"rhythm":"x--- x--- x--- x---","voicing":"close"}]})";
    const auto chordMidi = partMidi(scoreWith(kCMajor4, "Cmaj7:4 | Am7:4 | Dm7:4 | G7:4", "[]", chords), "keys");

    auto conflict = analyzeMidi(chordMidi, declared(Role::Melody));
    CHECK_FALSE(conflict.ok);
    CHECK(conflict.errorCode == "lane_conflict");
    CHECK(conflict.error.find("clearly chords") != std::string::npos);

    auto bass = analyzeMidi(chordMidi, declared(Role::Bass));
    CHECK_FALSE(bass.ok);
    CHECK((bass.errorCode == "lane_conflict" || bass.errorCode == "bass_range"));

    auto drums = analyzeMidi(chordMidi, declared(Role::Drums));
    CHECK_FALSE(drums.ok);
    CHECK((drums.errorCode == "lane_conflict" || drums.errorCode == "not_drums"));

    const std::string high =
        R"({"id":"bass","role":"bass","name":"Bass","low":"E4","high":"C6","grid":4,"velocity":95,"blocks":[{"startBar":1,"endBar":4,"rhythm":"R--- R--- R--- R---"}]})";
    auto range = analyzeMidi(partMidi(scoreWith(kCMajor4, "C:16", "[]", high), "bass"), declared(Role::Bass));
    CHECK_FALSE(range.ok);
    CHECK(range.errorCode == "bass_range");

    auto broken = analyzeMidi(bytes({1, 2, 3}));
    CHECK(broken.errorCode == "malformed");
}

TEST_CASE("analyze: a rolled chord is one onset") {
    std::vector<MidiNote> notes;
    for (int bar = 0; bar < 4; ++bar)
        for (int i = 0; i < 4; ++i) notes.push_back({bar * 3840 + i * 18, 3700 - i * 18, 57 + i * 4 - (i == 3 ? 1 : 0), 70, 0, 0});
    Realization r;
    r.bars = 4;
    r.ticksPerBar = 3840;
    PartRealization p;
    p.id = "keys";
    p.name = "Keys";
    for (const auto& n : notes) p.notes.push_back({n.tick, n.dur, n.pitch, n.vel, ""});
    r.parts.push_back(p);
    const auto a = analyzeMidi(writeSmf(r));
    REQUIRE(a.ok);
    CHECK((a.role == Role::Chords));
    CHECK(a.profile.onsetCount == 4);
    CHECK(a.fidelity.rhythm == doctest::Approx(1.0));
}

TEST_CASE("analyze: deterministic, and the normalized MIDI analyzes to the same IR") {
    const std::string part =
        R"({"id":"keys","role":"chords","name":"Keys","low":"C3","high":"C5","grid":4,"velocity":80,"blocks":[{"startBar":1,"endBar":4,"rhythm":"x--- ..x- x--- ....","voicing":"drop2"}]})";
    const auto midi = partMidi(scoreWith(kCMajor4, "Fmaj7:4 | Em7:4 | Dm9:4 | G13:4", "[]", part), "keys");
    const auto a = analyzeMidi(midi, declared(std::nullopt, "Keys", {"neo-soul"}));
    const auto b = analyzeMidi(midi, declared(std::nullopt, "Keys", {"neo-soul"}));
    REQUIRE(a.ok);
    CHECK(analysisJson(a) == analysisJson(b));
    CHECK(a.normalizedMidi == b.normalizedMidi);
    const auto again = analyzeMidi(a.normalizedMidi, declared(std::nullopt, "Keys", {"neo-soul"}));
    CHECK(again.scoreJson == a.scoreJson);
    CHECK(again.normalizedMidi == a.normalizedMidi);
}

TEST_CASE("analyze: leading empty bars are trimmed and a long end-of-track is ignored") {
    Realization r;
    r.bars = 141;  // like the GodFlow chord files: notes in 4 bars, end-of-track far later
    r.ticksPerBar = 3840;
    PartRealization p;
    p.id = "keys";
    p.name = "Keys";
    for (int bar = 2; bar < 6; ++bar)
        for (int pitch : {60, 64, 67}) p.notes.push_back({bar * 3840, 3840, pitch, 80, ""});
    r.parts.push_back(p);
    const auto a = analyzeMidi(writeSmf(r));
    REQUIRE(a.ok);
    CHECK(a.bars == 4);
    CHECK(a.harmony == std::vector<std::string>{"C"});
}

TEST_CASE("analyzeMidiData: a played part becomes literal notes that play back exactly, humanize or not") {
    // A riff as played: off the grid by a few ticks, uneven velocities, in A minor at 92 BPM.
    MidiFileData played;
    played.tempo = 92.0;
    played.meterNumerator = 4;
    played.meterDenominator = 4;
    const int pitches[] = {69, 72, 76, 74, 72, 71, 69, 64, 69, 72, 76, 77, 76, 74, 72, 69};
    for (int i = 0; i < 16; ++i) {
        const Tick tick = static_cast<Tick>(i) * 480 + (i == 0 ? 0 : i % 3 == 0 ? 17 : i % 3 == 1 ? -9 : 4);
        played.notes.push_back({tick, 400, pitches[i], 70 + (i * 7) % 40, 0, 0});
    }
    played.endTick = 16 * 480;

    AnalyzeOptions o;
    o.title = "What I played";
    o.literalPart = true;
    const Analysis a = analyzeMidiData(played, o);
    REQUIRE(a.ok);
    CHECK((a.role == Role::Melody));
    CHECK(a.bars == 2);
    const auto score = json::parse(a.scoreJson);
    CHECK(score["context"]["tempo"] == 92.0);
    REQUIRE(score["parts"].size() == 1);
    const auto& blocks = score["parts"][0]["blocks"];
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0]["notes"].size() == 16);
    CHECK_FALSE(blocks[0].contains("motif"));
    CHECK(score["motifs"].empty());

    for (const bool humanize : {false, true}) {
        RealizeOptions ro;
        ro.humanize = humanize;
        ro.seed = 7;
        const auto r = realizeJson(a.scoreJson, ro);
        REQUIRE(r.parts.size() == 1);
        const auto& notes = r.parts[0].notes;
        REQUIRE(notes.size() == played.notes.size());
        for (std::size_t i = 0; i < notes.size(); ++i) {
            INFO(i);
            CHECK(notes[i].tick == played.notes[i].tick);
            CHECK(notes[i].pitch == played.notes[i].pitch);
            CHECK(notes[i].vel == played.notes[i].vel);
        }
    }
}

TEST_CASE("analyzeMidiData: played chords keep their harmony reading; nothing played says so") {
    MidiFileData played;
    played.tempo = 120.0;
    played.meterNumerator = 4;
    played.meterDenominator = 4;
    // Am | F | C | G, one bar each, struck on beat 1 and the "and" of 2.
    const std::vector<std::vector<int>> chords{{57, 60, 64}, {53, 57, 60}, {48, 52, 55}, {55, 59, 62}};
    for (int bar = 0; bar < 4; ++bar)
        for (const Tick at : {Tick{0}, Tick{1440}})
            for (int p : chords[static_cast<std::size_t>(bar)]) played.notes.push_back({bar * 3840 + at, 900, p, 90, 0, 0});
    played.endTick = 4 * 3840;
    AnalyzeOptions o;
    o.literalPart = true;
    const Analysis a = analyzeMidiData(played, o);
    REQUIRE(a.ok);
    CHECK((a.role == Role::Chords));
    CHECK(a.harmony == std::vector<std::string>{"Am", "F", "C", "G"});
    const auto score = json::parse(a.scoreJson);
    CHECK(score["harmony"].get<std::string>().rfind("Am", 0) == 0);
    CHECK(score["parts"][0]["blocks"][0]["notes"].size() == played.notes.size());

    const Analysis none = analyzeMidiData(MidiFileData{}, o);
    CHECK_FALSE(none.ok);
    CHECK(none.errorCode == "no_notes");
}
