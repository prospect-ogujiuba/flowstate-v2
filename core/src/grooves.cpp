// The groove library. Steps: X accent, x hit, g ghost, . rest; one string per voice, '|' between bars.
// Keep the names, meters and one-line descriptions in sync with schema/src/score.ts (a test checks them).
//
// Patterns follow producer references checked on 2026-10-01: dembow (plugg-supply, bap.studio), trap (Native Instruments, kickdrum.io), UK drill
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
        // The genre library (P1-3, 2026-10-27): several typical rhythms per style, from producer tutorials
        // and references (sources, confidence and notes: evals/results/p1-3-grooves/research.json).
        {"afrobeats", 4, 4, 4, {
            {V::Kick, "X.....x.X.......|X.....x.X.....x."},
            {V::Rim, "...X..x...x.X..."},
            {V::Clap, "............x..."},
            {V::ClosedHat, "X.xgX.xgX.xgX.xg"},
            {V::Shaker, "gxXxgxXxgxXxgxXx", 0.05},
            {V::CongaHigh, "..x.....x.x...x.", 0.05},
            {V::CongaLow, ".......x.......x", 0.05}}},
        {"afrobeats_bounce", 4, 4, 4, {
            {V::Kick, "X..x..x.X.......|X..x..x.X.....x."},
            {V::Rim, "...x..x....x..x."},
            {V::Clap, "....X.......X..."},
            {V::Shaker, "XgxgXgxgXgxgXgxg", 0.05},
            {V::AgogoHigh, "x.x..x.x.x.x..x."},
            {V::AgogoLow, "....x.......x..."},
            {V::CongaHigh, "..x...x...x.x.x."},
            {V::CongaLow, "......x.......xx"},
            {V::BongoHigh, ".g.x.g.x.g.x.g.x", 0.05}}},
        {"afrobeats_afrofusion", 4, 4, 4, {
            {V::Kick, "X......xx.......|X......x..x....."},
            {V::Rim, "...x..x...x.....", 0.15},
            {V::Clap, "............X...", 0.12},
            {V::ClosedHat, "x.gxx.g.x.gxx.g.", 0.1},
            {V::Shaker, "g.x.g.x.g.x.g.x.", 0.08},
            {V::CongaLow, "..............x.", 0.1},
            {V::CongaHigh, ".........x.x....", 0.1}}},
        {"afroswing", 4, 4, 4, {
            {V::Kick, "X..x..x...x.....|X..x..x...x..x.."},
            {V::Clap, "....X.......X..."},
            {V::Rim, ".......x......x."},
            {V::ClosedHat, "x.xxx.xgx.xxx.xg"},
            {V::Shaker, "gxgxgxgxgxgxgxgx", 0.05},
            {V::CongaHigh, "..x.......x...x."}}},
        {"amapiano", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Clap, "....x.......x..."},
            {V::OpenHat, "..x...x...x...x."},
            {V::Shaker, "xgXgxgXgxgXgxgXg", 0.05},
            {V::Rim, "...x..x....x..x.|...x..x...x..x.x"},
            {V::CongaHigh, "....x..x..x.x..."},
            {V::CongaLow, "..............x."}}},
        {"amapiano_private_school", 4, 4, 4, {
            {V::Kick, "X.......X.......|X.......X.....g."},
            {V::Rim, "...x......x.....|...x......x..x..", 0.15},
            {V::Snare, ".g.....g.g.....g", 0.2},
            {V::OpenHat, "..x...x...x...x."},
            {V::Shaker, "xgxgxgxgxgxgxgxg", 0.1},
            {V::CongaLow, "......x.......x.", 0.1}}},
        {"amapiano_bacardi", 4, 4, 4, {
            {V::Kick, "X..x..x.X...X...|X..x..x.X..x..x."},
            {V::Snare, "....X.......X..."},
            {V::Shaker, "XxxxXxxxXxxxXxxx"},
            {V::LowTom, "..x.....x.x.....|..x.....x.x...x."},
            {V::MidTom, ".x...x.......x.."},
            {V::OpenHat, "..x...x...x...x."},
            {V::Clap, ".......x.......x"}}},
        {"afro_house", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Clap, "....X.......X..."},
            {V::OpenHat, "..x...x...x...x."},
            {V::Shaker, "gxxxgxxxgxxxgxxx"},
            {V::Rim, "x..x..x...x.x..."},
            {V::CongaHigh, "..x..x....x..xx."},
            {V::CongaLow, "......x.......x."},
            {V::BongoHigh, ".x.......x.x...."}}},
        {"afro_house_tribal", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::LowTom, "x..x..x.........|x..x..x.....x..."},
            {V::MidTom, "........x..x..x."},
            {V::Shaker, "xgxgxgxgxgxgxgxg"},
            {V::Clave, "x..x..x...x.x...|..x.x...x..x..x."},
            {V::Woodblock, ".......x.......x"},
            {V::Cowbell, "..x...x...x...x."},
            {V::CongaHigh, ".x...x.x.x...x.."}}},
        {"gqom", 4, 4, 4, {
            {V::Kick, "X..x..x...x.....|X..x......x..x.."},
            {V::LowTom, "...........x..x.|......x....x...."},
            {V::MidTom, ".x..............|.x...........x.x"},
            {V::Snare, "........X......."},
            {V::Shaker, "..x...x...x...x."},
            {V::Rim, ".....x.........x"}}},
        {"dancehall", 4, 4, 4, {
            {V::Kick, "X..x....X..x....|X..x....X..x..x."},
            {V::Snare, "......X.......X."},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."},
            {V::Shaker, "gxgxgxgxgxgxgxgx"},
            {V::Rim, "....x.......x..."}}},
        {"dancehall_steppers", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Snare, "X..x..x.X..x..x."},
            {V::ClosedHat, "xx.xxx.xxx.xxx.x"},
            {V::OpenHat, "..x...x...x...x."}}},
        {"reggae_one_drop", 4, 4, 3, {
            {V::Kick, "......X....."},
            {V::Rim, "......X....."},
            {V::ClosedHat, "x.xx.xx.xx.x", 0.05},
            {V::PedalHat, "...x.....x.."}}},
        {"reggaeton_perreo", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Snare, "...x..X....x..X.|...x..X....x.xX."},
            {V::Clap, "......X.......X."},
            {V::ClosedHat, "x.Xxx.Xxx.Xxx.Xx"},
            {V::OpenHat, "................|..............x."},
            {V::TimbaleHigh, "................|............x.xx"}}},
        {"moombahton", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Clap, "...x..X....x..X."},
            {V::ClosedHat, "xgxgxgxgxgxgxgxg"},
            {V::OpenHat, "..x...x...x...x."},
            {V::MidTom, "................|.............xx."}}},
        {"baile_funk", 4, 4, 4, {
            {V::Kick, "X..x..x...x..x..|X..x..x...x..xx."},
            {V::Clap, "....X.......X..."},
            {V::CongaLow, ".x.....x.x....x."},
            {V::CongaHigh, "..x..x.......x.."},
            {V::Rim, "...x.......x...."}}},
        {"baile_funk_mandelao", 4, 4, 4, {
            {V::Kick, "X..x..x.X..x..x."},
            {V::Clap, "....X.......X..."},
            {V::Rim, "..x...x...x...x."},
            {V::Cowbell, ".x.....x.x.....x"}}},
        {"trap_triplet", 4, 4, 6, {
            {V::Kick, "X.................x.....|..x...........x........."},
            {V::Clap, "............X..........."},
            {V::ClosedHat, "x.x.x.x.x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x.x.xgxgxg"},
            {V::OpenHat, "........................|..................x....."}}},
        {"trap_rage", 4, 4, 4, {
            {V::Kick, "X.....x...x.....|X.x.......x..x.."},
            {V::Clap, "........X......."},
            {V::Snare, "................|...............x"},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."},
            {V::OpenHat, "..............x."},
            {V::Crash, "x...............|................"}}},
        {"trap_plugg", 4, 4, 4, {
            {V::Kick, "X......x..x.....|X.........x..x.."},
            {V::Clap, "........X......."},
            {V::ClosedHat, "x.x.x.x.x.x.xxx.|x.x.x.x.x.xxx.x."},
            {V::Rim, ".............x.."},
            {V::OpenHat, "......x........."}}},
        {"trap_soul", 4, 4, 4, {
            {V::Kick, "X.........x.....|X.......x.....x."},
            {V::Clap, "........X.......", 0.12},
            {V::ClosedHat, "x.x.x.x.x.xxx.x.|x.x.x.x.x.x.xgxg", 0.06},
            {V::Rim, "...g.......g....", 0.15},
            {V::Shaker, "..g...g...g...g."}}},
        {"drill_ny", 4, 4, 4, {
            {V::Kick, "X......x........|X......x..x....."},
            {V::Snare, "........X.......|......g.X.....x."},
            {V::ClosedHat, "x..x..x.x.x.x..x|x..x..x.x..xxx.x"},
            {V::Rim, "...........x....|...x............"},
            {V::OpenHat, "..............x."}}},
        {"phonk_drift", 4, 4, 4, {
            {V::Kick, "X......x..x.....|X......x..x..x.."},
            {V::Clap, "....X.......X..."},
            {V::Cowbell, "x..x..x.x..x..x."},
            {V::ClosedHat, "xxxxxxxxxxxxxx.x"},
            {V::OpenHat, "..............x."}}},
        {"phonk_memphis", 4, 4, 4, {
            {V::Kick, "X.....x...x.....|X.....x.x...x..."},
            {V::Clap, "........X......."},
            {V::Snare, "........x......."},
            {V::ClosedHat, "xgxgxgxgxgxgxgxg"},
            {V::Cowbell, "..x...x...x.x..x"}}},
        {"jersey_club", 4, 4, 4, {
            {V::Kick, "X...X...X..X..X."},
            {V::Clap, "....X.......X..."},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."},
            {V::Rim, "..x...x...x....x"}}},
        {"jersey_club_bounce", 4, 4, 4, {
            {V::Kick, "X...X...X..X..X.|X.x.X...X.x.x.x."},
            {V::Clap, "....X.......X...|....X.......X..x"},
            {V::ClosedHat, "xgxgxgxgxgxgxgxg"}}},
        {"uk_garage", 4, 4, 4, {
            {V::Kick, "X.........x.....|X.........x..x.."},
            {V::Snare, "....X.......X..."},
            {V::ClosedHat, "..x...x...x...x.|..x...x...x.g.x."},
            {V::OpenHat, "................|..............x."},
            {V::Rim, ".......x.x....x.|...x...x.x......"},
            {V::Shaker, "gxgxgxgxgxgxgxgx"}}},
        {"uk_garage_shuffle", 4, 4, 4, {
            {V::Kick, "X.....g...x..g..|X.....g....g..x."},
            {V::Snare, "....X.......X..g"},
            {V::Clap, "....g.......g..."},
            {V::ClosedHat, "gxgxgxXxgxgxXxgx"},
            {V::Shaker, "gxgxgxgxgxgxgxgx"}}},
        {"uk_funky", 4, 4, 4, {
            {V::Kick, "X......x..x.....|X......x..x...x."},
            {V::Clap, "....X.......X..."},
            {V::Rim, "...x..x....x..x."},
            {V::CongaHigh, ".x..x...x..x...."},
            {V::CongaLow, "......x.......x."},
            {V::OpenHat, "..x...x...x...x."},
            {V::Shaker, "xgxgxgxgxgxgxgxg"}}},
        {"jungle", 4, 4, 4, {
            {V::Kick, "X.x.......xx....|X.x.......x....."},
            {V::Snare, "....X..x.x..X..x|....X..x.x....x."},
            {V::Ride, "x.x.x.x.x.x.x.x."},
            {V::Crash, "................|..........x....."}}},
        {"bossa_nova", 4, 4, 4, {
            {V::Kick, "X.....x.X.....x."},
            {V::Rim, "x.....x.....x...|....x.....x....."},
            {V::ClosedHat, "x.x.x.x.x.x.x.x."},
            {V::PedalHat, "....x.......x..."}}},
        {"samba", 4, 4, 4, {
            {V::Kick, "x..xX..xx..xX..x"},
            {V::Shaker, "xggXxggXxggXxggX"},
            {V::Rim, "x.xx.x.x.xx.x.x.|x.x.xx.x.x.x.xx."},
            {V::AgogoHigh, "x.x...x.x.x...x."},
            {V::AgogoLow, "...x.....x.....x"}}},
        {"cumbia", 4, 4, 4, {
            {V::Kick, "X.......X......."},
            {V::Guiro, "x.xxx.xxx.xxx.xx"},
            {V::Cowbell, "..x...x...x...x."},
            {V::CongaMute, "x...x...x...x..."},
            {V::CongaHigh, "......x.......x."},
            {V::CongaLow, "...............x"},
            {V::Rim, "....x.......x..."}}},
        {"salsa", 4, 4, 4, {
            {V::Clave, "..x.x...x..x..x."},
            {V::Cowbell, "X.x.X.x.X.x.X.x."},
            {V::CongaMute, "xxXxxx..xxXxxx.."},
            {V::CongaHigh, "......x.......x."},
            {V::CongaLow, ".......x.......x"},
            {V::Kick, "...g..g....g..g."}}},
        {"rnb", 4, 4, 4, {
            {V::Kick, "X......x..x.....|X.........x..x.."},
            {V::Clap, "....X.......X...", 0.12},
            {V::ClosedHat, "x.xxx.x.x.xxx.x.|x.x.x.x.x.x.xxxx", 0.05},
            {V::Rim, "..........g.....|.......g........", 0.12},
            {V::Shaker, "..x...x...x...x."}}},
        {"rnb_new_jack_swing", 4, 4, 4, {
            {V::Kick, "X.....x...x.....|X.....x.x.x...x."},
            {V::Snare, "....X.......X..."},
            {V::Clap, "....X.......X..."},
            {V::ClosedHat, "xgxgxgxgxgxgxgxg"},
            {V::Tambourine, "....x.......x..."}}},
        {"gfunk", 4, 4, 4, {
            {V::Kick, "X......xX.......|X......xX....x.."},
            {V::Snare, "....X.......X...", 0.08},
            {V::Clap, "....X.......X...", 0.08},
            {V::ClosedHat, "XxxxXxxxXxxxXxx."},
            {V::OpenHat, "..............x."},
            {V::Shaker, "x.x.x.x.x.x.x.x."}}},
        {"disco", 4, 4, 4, {
            {V::Kick, "X...X...X...X..."},
            {V::Snare, "....X.......X..."},
            {V::OpenHat, "..X...X...X...X."},
            {V::ClosedHat, "xx.xxx.xxx.xxx.x"},
            {V::Tambourine, "....x.......x..."}}},
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
