// The groove library. Steps: X accent, x hit, g ghost, . rest; one string per voice, '|' between bars.
// Keep the names, meters and one-line descriptions in sync with schema/src/score.ts (a test checks them).
//
// Patterns follow producer references checked on 2026-10-01: dembow (plugg-supply, bap.studio), afrobeats
// (beatstorapon, routenote; the weakest-sourced, check by ear), trap (Native Instruments, kickdrum.io), UK drill
// (Native Instruments, BandLab, midimighty), funk (Roland's "Funky Drummer"), boom bap and deep/tech house
// (Attack Magazine, bap.studio), liquid drum and bass (bap.studio), lofi (midimighty), jazz ride
// (jazznightschool), 6/8 (drumscore). Listening decides; change a pattern when it doesn't sound like the style.
#include "flowstate/grooves.h"

namespace flowstate {

namespace {

using V = DrumVoice;

std::vector<Groove> build() {
    return {
        // 4/4, 16ths
        {"four_on_floor", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Clap, "....X.......X..."},
            {V::OpenHat, "..x...x...x...x."},
            {V::Shaker, ".x.x.x.x.g.x.x.x"}}},
        {"tech_house", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Clap, "....X.......X..."},
            {V::PedalHat, "..x...x...x...x."},
            {V::Ride, "x...x...x...x..."},
            {V::Snare, ".......g........", 0.15}}},
        {"boom_bap", 4, 4, 4, {
            {V::Kick, "X......x..X.....|X.x.......X....."},
            {V::Snare, "....X.......X...", 0.12},
            {V::ClosedHat, "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x."},
            {V::OpenHat, "................|............x..."}}},
        {"lofi", 4, 4, 4, {
            {V::Kick, "X.....x.X.......|X.......x..x...."},
            {V::Snare, "....X.......X...", 0.2},
            {V::Clap, "....g.......g...", 0.25},
            {V::ClosedHat, "x.g.x.g.x.g.x.g.", 0.08}}},
        {"trap", 4, 4, 4, {
            {V::Kick, "X.............x.|..x............."},
            {V::Clap, "........X......."},
            {V::ClosedHat, "x...x...x...x...x...x...x...x...|x...x...x...x...x...x...xgxgxgxg"},
            {V::OpenHat, "................|..............x."}}},
        {"drill", 4, 4, 4, {
            {V::Kick, "X.............x.|X..x..x.......x."},
            {V::Snare, "........X.......|............X..g"},
            {V::ClosedHat, "X..x..X.X..x..Xg|X..x..X.X..x..X."}}},
        {"dembow", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Snare, "...x..X....x..X."},
            {V::ClosedHat, "g.g.g.g.g.g.g.g."}}},
        {"afrobeats", 4, 4, 4, {
            {V::Kick, "X.....x.....x..."},
            {V::Rim, "...X......X....."},
            {V::Shaker, "XgxgXgxgXgxgXgxg", 0.05}}},
        {"funk", 4, 4, 4, {
            {V::Kick, "X.x.......x..x.."},
            {V::Snare, "....X..g.g.gX..g"},
            {V::ClosedHat, "XxxxX.xxXxxxX.xx"},
            {V::OpenHat, ".....x.......x.."}}},
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
            {V::Kick, "X.........x....."},
            {V::Snare, "....X..g.g..X..g"},
            {V::ClosedHat, "..g...g...g...g."},
            {V::Ride, "x.x.x.x.x.x.x.x."}}},
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
            {V::Ride, "x..X.xx..X.x"},
            {V::PedalHat, "...x.....x.."},
            {V::Kick, "g..g..g..g.."},
            {V::Rim, ".........g.."}}},
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
            {V::Kick, "X..........."},
            {V::Snare, "......X....."},
            {V::ClosedHat, "X.x.x.X.x.x."}}},
        {"gospel_shuffle", 6, 8, 2, {
            {V::Kick, "X.........x."},
            {V::Snare, "......X....g"},
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
