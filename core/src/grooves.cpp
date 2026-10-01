// The groove library. Steps: X accent, x hit, g ghost, . rest; one string per voice, '|' between bars.
// Keep the names, meters and one-line descriptions in sync with schema/src/score.ts (a test checks them).
#include "flowstate/grooves.h"

namespace flowstate {

namespace {

using V = DrumVoice;

std::vector<Groove> build() {
    return {
        // 4/4, 16ths
        {"four_on_floor", 4, 4, 4, {
            {V::Kick, "X...x...X...x..."},
            {V::Clap, "....X.......X..."},
            {V::OpenHat, "..x...x...x...x."},
            {V::Shaker, "xgxgxgxgxgxgxgxg"}}},
        {"tech_house", 4, 4, 4, {
            {V::Kick, "x...x...x...x..."},
            {V::Clap, "....x.......x..."},
            {V::ClosedHat, "ggXgggXgggXgggXg"},
            {V::Rim, "......x....x..x."}}},
        {"boom_bap", 4, 4, 4, {
            {V::Kick, "x.....x...x.....|x.....x..x..x..."},
            {V::Snare, "....X.......X...", 0.1},
            {V::ClosedHat, "X.x.X.x.X.x.X.x."}}},
        {"lofi", 4, 4, 4, {
            {V::Kick, "x......x..x.....|x.........x....."},
            {V::Snare, "....x.......x...|....x.......x..g", 0.2},
            {V::ClosedHat, "x.g.x.g.x.g.x.g.", 0.08}}},
        {"trap", 4, 4, 4, {
            {V::Kick, "x.......x..x....|x.....x...x....."},
            {V::Snare, "........X......."},
            {V::ClosedHat, "x.x.x.x.x.x.xxxx|x.x.x.x.xxxxxxxx"},
            {V::OpenHat, "................|..............x."}}},
        {"drill", 4, 4, 4, {
            {V::Kick, "x.........x.....|..x.......x....."},
            {V::Snare, "........X.......|........X..x...."},
            {V::ClosedHat, "x..x..x.x..x..x.|x..x..x.x.x.x.xx"}}},
        {"dembow", 4, 4, 4, {
            {V::Kick, "x...x...x...x..."},
            {V::Snare, "...x..x....x..x."},
            {V::ClosedHat, "x.g.x.g.x.g.x.g."}}},
        {"afrobeats", 4, 4, 4, {
            {V::Kick, "x.....x...x.....|x.....x...x..x.."},
            {V::Rim, "...x..x....x..x."},
            {V::Clap, "....x.......x..."},
            {V::Shaker, "XgxgXgxgXgxgXgxg", 0.05},
            {V::HighTom, "..x.......x.x..."},
            {V::MidTom, ".....x.......x.."}}},
        {"funk", 4, 4, 4, {
            {V::Kick, "x.x....x..x....."},
            {V::Snare, "....X..g.g..X..g"},
            {V::ClosedHat, "XgxgXgxgXgxgXg.."},
            {V::OpenHat, "..............x."}}},
        {"rock", 4, 4, 4, {
            {V::Kick, "x.....x.x.......|x.....x.x...x..."},
            {V::Snare, "....X.......X..."},
            {V::ClosedHat, "X.x.X.x.X.x.X.x."},
            {V::Crash, "x...............|................"}}},
        {"pop", 4, 4, 4, {
            {V::Kick, "x.....x.x......."},
            {V::Clap, "....X.......X..."},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."},
            {V::Tambourine, "....x.......x..."}}},
        {"ballad", 4, 4, 4, {
            {V::Kick, "x.......x......."},
            {V::Rim, "....x.......x..."},
            {V::ClosedHat, "x.g.x.g.x.g.x.g."}}},
        {"neo_soul", 4, 4, 4, {
            {V::Kick, "x......x..x.....|x.........x..x.."},
            {V::Snare, "....x.......x...|....x.......x..g", 0.25},
            {V::ClosedHat, "x.xgx.x.x.xgx.x.", 0.12}}},
        {"dnb", 4, 4, 4, {
            {V::Kick, "x.........x....."},
            {V::Snare, "....X.......X..g"},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."},
            {V::Shaker, "gxgxgxgxgxgxgxgx"}}},
        {"half_time", 4, 4, 4, {
            {V::Kick, "x.........x....."},
            {V::Snare, "........X......."},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."}}},
        {"cinematic_toms", 4, 4, 4, {
            {V::Kick, "x.......x......."},
            {V::LowTom, "x.....x.x.......|x.....x.x...x.x."},
            {V::MidTom, "....x.......x..."},
            {V::Crash, "x...............|................"}}},
        {"sparse_pulse", 4, 4, 4, {
            {V::Kick, "x..............."},
            {V::Ride, "x...x...x...x..."},
            {V::Shaker, "..g...g...g...g."}}},
        // 4/4, triplets
        {"jazz_swing", 4, 4, 3, {
            {V::Ride, "x..x.xx..x.x"},
            {V::PedalHat, "...x.....x.."},
            {V::Kick, "g..g..g..g.."},
            {V::Snare, "........g...|.....g......"}}},
        {"brush_swing", 4, 4, 3, {
            {V::Snare, "x.gx.gx.gx.g"},
            {V::PedalHat, "...x.....x.."},
            {V::Kick, "g.....g....."}}},
        // 3/4
        {"jazz_waltz", 3, 4, 3, {
            {V::Ride, "x..x.xx.."},
            {V::PedalHat, "...x..x.."},
            {V::Kick, "g........"}}},
        {"waltz", 3, 4, 2, {
            {V::Kick, "x....."},
            {V::Rim, "..x.x."},
            {V::ClosedHat, "x.x.x."}}},
        // 6/8 (the beat is the eighth note; 2 steps per eighth)
        {"six_eight", 6, 8, 2, {
            {V::Kick, "x..........."},
            {V::Snare, "......X....."},
            {V::ClosedHat, "x.x.x.x.x.x."}}},
        {"gospel_shuffle", 6, 8, 2, {
            {V::Kick, "x.......x..."},
            {V::Snare, "......X...g."},
            {V::ClosedHat, "X.x.x.X.x.x."}}},
    };
}

}  // namespace

const std::vector<Groove>& grooves() {
    static const std::vector<Groove> all = build();
    return all;
}

const Groove* findGroove(const std::string& name) {
    for (const auto& g : grooves())
        if (g.name == name) return &g;
    return nullptr;
}

}  // namespace flowstate
