#include <doctest/doctest.h>

#include "flowstate/groove.h"
#include "flowstate/steps.h"
#include "flowstate/timing.h"

#include <string>
#include <vector>

using namespace flowstate;

TEST_CASE("ticks per bar follow numerator * 960 * 4 / denominator") {
    CHECK(TimeGrid(4, 4, 1, 120).ticksPerBar() == 3840);
    CHECK(TimeGrid(3, 4, 1, 120).ticksPerBar() == 2880);
    CHECK(TimeGrid(6, 8, 1, 120).ticksPerBar() == 2880);
    CHECK(TimeGrid(7, 8, 1, 120).ticksPerBar() == 3360);
    CHECK(TimeGrid(5, 4, 1, 120).ticksPerBar() == 4800);
    CHECK(TimeGrid(2, 2, 1, 120).ticksPerBar() == 3840);
    CHECK(TimeGrid(6, 8, 1, 120).ticksPerBeat() == 480);
    CHECK(TimeGrid(7, 8, 4, 120).clipEnd() == 4 * 3360);
}

TEST_CASE("bar/beat positions are 1-based") {
    TimeGrid g(4, 4, 4, 120);
    CHECK(g.at(1, 1.0) == 0);
    CHECK(g.at(2, 1.0) == 3840);
    CHECK(g.at(1, 3.5) == 2400);
    TimeGrid six(6, 8, 2, 90);
    CHECK(six.at(1, 4.0) == 1440);  // beat 4 of 6/8 = second dotted-quarter pulse
    CHECK(six.at(2, 1.0) == 2880);
    TimeGrid seven(7, 8, 2, 90);
    CHECK(seven.at(2, 7.0) == 3360 + 6 * 480);
}

TEST_CASE("step offsets, including triplets, 6/8 and 7/8") {
    TimeGrid g(4, 4, 1, 120);
    CHECK(g.stepsPerBar(4) == 16);
    CHECK(g.stepOffset(1, 4) == 240);
    CHECK(g.stepOffset(1, 3) == 320);
    CHECK(g.stepOffset(11, 3) == 3520);
    TimeGrid six(6, 8, 1, 120);
    CHECK(six.stepsPerBar(2) == 12);
    CHECK(six.stepOffset(1, 2) == 240);
    TimeGrid seven(7, 8, 1, 120);
    CHECK(seven.stepsPerBar(2) == 14);
    CHECK(seven.stepOffset(13, 2) == 3120);
    CHECK(g.msToTicks(500.0) == doctest::Approx(960.0));
}

TEST_CASE("step strings: spaces ignored, one bar") {
    std::vector<std::string> w;
    auto p = parseSteps("x--- ..x- .x-- ....", 16, "xX-.", "t", w);
    CHECK(w.empty());
    REQUIRE(p.bars.size() == 1);
    CHECK(p.bars[0] == "x---..x-.x--....");
    CHECK(p.at(5, 0) == 'x');  // repeats across block bars
}

TEST_CASE("step strings: bar lines and implicit bar splitting") {
    std::vector<std::string> w;
    auto p = parseSteps("x.x.|X...|", 4, "xX-.", "t", w);
    CHECK(w.empty());
    REQUIRE(p.bars.size() == 2);
    CHECK(p.bars[1] == "X...");
    CHECK(p.at(2, 0) == 'x');
    CHECK(p.at(3, 0) == 'X');
    auto q = parseSteps("x...x...", 4, "xX-.", "t", w);
    CHECK(q.bars.size() == 2);
    CHECK(w.empty());
}

TEST_CASE("step strings: wrong lengths are repaired with warnings") {
    std::vector<std::string> w;
    // A bar at a quarter of the grid's resolution is spread over the bar, not repeated.
    auto coarse = parseSteps("x...", 16, "xX-.", "t", w);
    CHECK(coarse.scale == 1);
    CHECK(coarse.bars[0] == "x---............");
    CHECK(w.size() == 1);
    w.clear();
    auto padded = parseSteps("x.x", 16, "xX-.", "t", w);
    CHECK(padded.bars[0] == "x.x.............");
    CHECK(w.size() == 1);
    w.clear();
    auto truncated = parseSteps("x.x.x|x", 4, "xX-.", "t", w);
    CHECK(truncated.bars[0] == "x.x.");
    CHECK(truncated.bars[1] == "x---");  // one step spread over the bar
    CHECK(w.size() == 2);
    w.clear();
    auto bad = parseSteps("x?g.", 4, "xX-.", "t", w);
    CHECK(bad.bars[0] == "x...");
    CHECK(w.size() == 1);
    w.clear();
    CHECK(parseSteps("   ", 4, "x.", "t", w).empty());
}

TEST_CASE("step events: holds extend, also across bars; hold after rest is a rest") {
    std::vector<std::string> w;
    auto p = parseSteps("x--.|--x.", 4, "xX-.", "t", w);
    auto ev = stepEvents(p, 2);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].step == 0);
    CHECK(ev[0].length == 3);
    CHECK(ev[1].step == 6);
    CHECK(ev[1].length == 1);
    auto q = parseSteps("x--x", 4, "xX-.", "t", w);
    auto qe = stepEvents(q, 2);
    REQUIRE(qe.size() == 4);
    CHECK(qe[2].step == 4);
    CHECK(qe[2].length == 3);
}

TEST_CASE("swing delays off-beat steps and stays monotonic") {
    TimeGrid g(4, 4, 1, 120);
    Swing s(g, 4, 0.5);
    CHECK(s.apply(0) == 0);
    CHECK(s.apply(240) == 360);  // odd 16th delayed by 0.5 * step
    CHECK(s.apply(480) == 480);  // on-beat 8th unchanged
    CHECK(s.apply(720) == 840);
    Tick prev = -1;
    for (Tick t = 0; t < 3840; t += 7) {
        Tick w = s.apply(t);
        CHECK(w >= prev);
        prev = w;
    }
    Swing eighths(g, 2, 0.33);
    CHECK(eighths.apply(480) == 480 + roundHalfUp(0.33 * 480));
    Swing triplets(g, 3, 0.5);
    CHECK_FALSE(triplets.active());
    CHECK(triplets.apply(320) == 320);
}

TEST_CASE("metric weights, including 6/8 and 7/8 groupings") {
    TimeGrid g(4, 4, 1, 120);
    CHECK(metricWeight(g, 0) == 1.0);
    CHECK(metricWeight(g, 960) == 0.5);
    CHECK(metricWeight(g, 480) == 0.0);
    CHECK(metricWeight(g, 240) == -0.5);
    TimeGrid six(6, 8, 1, 120);
    CHECK(metricWeight(six, 1440) == 0.5);  // second pulse
    CHECK(metricWeight(six, 480) == 0.0);   // weak eighth
    TimeGrid seven(7, 8, 1, 120);
    CHECK(metricWeight(seven, 960) == 0.5);   // 2+2+3: pulses on 1, 3, 5
    CHECK(metricWeight(seven, 1920) == 0.5);
    CHECK(metricWeight(seven, 2400) == 0.0);  // inside the final 3-group
}

TEST_CASE("seeded RNG is platform independent") {
    Rng a(42), b(42);
    for (int i = 0; i < 16; ++i) CHECK(a.next() == b.next());
    Rng c(0);
    CHECK(c.next() == 0xE220A8397B1DCDAFULL);  // SplitMix64 reference value for seed 0
    Rng t(7);
    for (int i = 0; i < 100; ++i) {
        double x = t.triangular();
        CHECK(x >= -1.0);
        CHECK(x <= 1.0);
    }
}

TEST_CASE("step strings: bars at another resolution are read at the finest one") {
    std::vector<std::string> w;
    // Half resolution (8 steps where 16 are expected): each step covers two.
    auto half = parseSteps("x...x... | x.x.x..x", 16, "xX-.", "t", w);
    CHECK(half.scale == 1);
    CHECK(half.bars[0] == "x-......x-......");
    CHECK(half.bars[1] == "x-..x-..x-....x-");
    CHECK(w.size() == 2);
    w.clear();
    // Double resolution (16 where 8 are expected): the pattern is read at 16, and a normal bar is expanded.
    auto dbl = parseSteps("x.xxx.x.x.x.x.xx | x-..x...", 8, "xX-.", "t", w);
    CHECK(dbl.scale == 2);
    CHECK(dbl.stepsPerBar == 16);
    CHECK(dbl.bars[0] == "x.xxx.x.x.x.x.xx");
    CHECK(dbl.bars[1] == "x---....x-......");
    CHECK(w.size() == 1);
    w.clear();
    // Far beyond the cap, or unrelated lengths, are still truncated or padded.
    auto huge = parseSteps(std::string(64, 'x'), 4, "xX-.", "t", w);
    CHECK(huge.scale == 1);
    CHECK(huge.bars[0] == "xxxx");
}
