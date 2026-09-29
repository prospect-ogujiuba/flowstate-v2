// Behaviour tests for the audition scheduler. Plain C++ (no framework, no JUCE).
// Run: ctest --test-dir <build> --output-on-failure   (or the executable directly)

#include "AuditionScheduler.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace flowstate::spike;

namespace
{
int failures = 0;
int checks = 0;

#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; \
    std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, ...) do { ++checks; if (! (cond)) { ++failures; \
    std::printf ("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Recorded
{
    long long absSample;   // block start sample + offset
    int block;
    int offset;
    int channel, pitch, velocity;
    bool on;
};

struct VectorSink final : EventSink
{
    std::vector<Recorded>* out = nullptr;
    long long blockStart = 0;
    int block = 0;
    int numSamples = 0;
    bool offsetOutOfRange = false;
    int lastOffset = 0;
    bool outOfOrder = false;

    void emit (const ScheduledEvent& e) noexcept override
    {
        if (e.sampleOffset < 0 || e.sampleOffset >= numSamples)
            offsetOutOfRange = true;

        if (e.sampleOffset < lastOffset)
            outOfOrder = true;

        lastOffset = e.sampleOffset;
        out->push_back ({ blockStart + e.sampleOffset, block, e.sampleOffset, e.channel, e.pitch, e.velocity, e.isNoteOn });
    }
};

/** Simple host model: plays from startPpq, tempo given per block (host-style: ppq at each block
    start is the integral of tempo; tempo is constant within a block). */
struct Host
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    double ppq = 0.0;
    long long sample = 0;
    int block = 0;
    bool playing = true;
    bool looping = false;
    double loopStart = 0.0, loopEnd = 0.0;
    std::function<double (double ppq)> tempoAt = [] (double) { return 120.0; };

    VectorSink sink;

    explicit Host (std::vector<Recorded>& out) { sink.out = &out; }

    /** Runs one block; returns the block's (ppqStart, bpm). */
    std::pair<double, double> step (AuditionScheduler& s)
    {
        BlockPosition p;
        p.isPlaying = playing;
        p.hasPpq = true;
        p.ppqStart = ppq;
        p.bpm = tempoAt (ppq);
        p.sampleRate = sampleRate;
        p.numSamples = blockSize;
        p.isLooping = looping;
        p.loopStartPpq = loopStart;
        p.loopEndPpq = loopEnd;

        sink.blockStart = sample;
        sink.block = block;
        sink.numSamples = blockSize;
        sink.lastOffset = 0;
        s.process (p, sink);

        const auto result = std::make_pair (ppq, p.bpm);

        if (playing)
        {
            ppq += (double) blockSize / (sampleRate * 60.0 / p.bpm);

            if (looping && ppq >= loopEnd)
                ppq = loopStart + (ppq - loopEnd);
        }

        sample += blockSize;
        ++block;
        return result;
    }
};

/** Four-bar test clip: chords, bass and a drum hit on every beat, plus one note tied over the loop. */
RenderedClip makeClip()
{
    std::vector<ClipNote> notes;

    for (int bar = 0; bar < 4; ++bar)
    {
        const double b = bar * 4.0;

        for (int p : { 60, 64, 67 })
            notes.push_back ({ b, 3.5, 0, p + bar, 90 });

        notes.push_back ({ b, 1.0, 1, 36 + bar, 100 });
        notes.push_back ({ b + 2.0, 1.0, 1, 36 + bar, 100 });

        for (int beat = 0; beat < 4; ++beat)
            notes.push_back ({ b + beat, 0.25, 9, beat % 2 == 0 ? 36 : 38, 110 });
    }

    notes.push_back ({ 14.0, 4.0, 2, 72, 80 }); // runs past the loop end -> off at wrap
    return renderClip (notes, 16.0);
}

/** Every key's events must alternate on/off, starting with on. Returns keys still sounding. */
int checkBalanced (const std::vector<Recorded>& ev, bool& ok)
{
    std::map<int, bool> down;
    ok = true;

    for (const auto& e : ev)
    {
        auto& d = down[e.channel * 128 + e.pitch];

        if (e.on == d)
            ok = false; // double on or off without on

        d = e.on;
    }

    int stuck = 0;

    for (auto& [k, d] : down)
        stuck += d ? 1 : 0;

    return stuck;
}

std::vector<double> clipOnPpqs (const RenderedClip& c)
{
    std::vector<double> v;

    for (auto& e : c.events)
        if (e.isNoteOn)
            v.push_back (e.ppq);

    return v;
}

//==================================================================================================
void testRenderOrdering()
{
    std::printf ("render: sorted, offs first, loop-end off wraps to 0\n");
    const auto clip = makeClip();

    CHECK (clip.lengthPpq == 16.0);
    bool sorted = true;

    for (size_t i = 1; i < clip.events.size(); ++i)
    {
        const auto& a = clip.events[i - 1];
        const auto& b = clip.events[i];

        if (a.ppq > b.ppq || (a.ppq == b.ppq && a.isNoteOn && ! b.isNoteOn))
            sorted = false;
    }

    CHECK (sorted);

    bool foundWrapOff = false;

    for (auto& e : clip.events)
        if (! e.isNoteOn && e.channel == 2 && e.pitch == 72)
            foundWrapOff = e.ppq == 0.0;

    CHECK (foundWrapOff);

    // Invalid notes are dropped
    const auto bad = renderClip ({ { -1.0, 1.0, 0, 60, 100 }, { 0.0, 0.0, 0, 60, 100 },
                                   { 0.0, 1.0, 16, 60, 100 }, { 20.0, 1.0, 0, 60, 100 } }, 16.0);
    CHECK (bad.events.empty());
}

void testLoopWrapAndSync (double bpm, int blockSize, double sampleRate)
{
    std::printf ("loop: 100 bars at %.0f bpm, block %d, %.0f Hz\n", bpm, blockSize, sampleRate);
    const auto clip = makeClip();
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    h.blockSize = blockSize;
    h.sampleRate = sampleRate;
    h.tempoAt = [bpm] (double) { return bpm; };

    s.setClip (&clip, h.sink);

    const double totalPpq = 400.0; // 100 bars of 4/4
    const auto spp = sampleRate * 60.0 / bpm;

    while (h.ppq < totalPpq)
        h.step (s);

    CHECK (! h.sink.offsetOutOfRange);
    CHECK (! h.sink.outOfOrder);
    CHECK (s.getNumDiscontinuities() == 0);

    // Expected note-ons: every clip on, every iteration that started before the end.
    std::vector<long long> expected;
    const auto ons = clipOnPpqs (clip);
    const auto lastPpq = h.ppq;

    for (int k = 0; k * 16.0 < lastPpq; ++k)
        for (auto p : ons)
            if (k * 16.0 + p < lastPpq)
                expected.push_back ((long long) std::floor ((k * 16.0 + p) * spp + 1.0e-6));

    std::vector<long long> got;

    for (auto& e : ev)
        if (e.on)
            got.push_back (e.absSample);

    CHECK_MSG (got.size() == expected.size(), "got %zu ons, expected %zu", got.size(), expected.size());

    long long maxErr = 0;

    for (size_t i = 0; i < std::min (got.size(), expected.size()); ++i)
        maxErr = std::max (maxErr, std::llabs (got[i] - expected[i]));

    CHECK_MSG (maxErr <= 1, "max sample error %lld", maxErr); // bar-1 aligned, no drift over 100 bars

    // Tied note is closed exactly at each wrap (16 ppq boundaries), i.e. before the downbeat ons.
    int wrapOffs = 0;

    for (auto& e : ev)
        if (! e.on && e.channel == 2 && e.pitch == 72)
        {
            const auto ppq = (double) e.absSample / spp;
            const auto phase = std::fmod (ppq + 1.0e-3, 16.0);
            CHECK_MSG (phase < 2.0e-3 + 1.0 / spp, "tied note off at ppq %f", ppq);
            ++wrapOffs;
        }

    CHECK (wrapOffs >= 24);

    h.playing = false;
    h.step (s);
    bool ok = false;
    CHECK (checkBalanced (ev, ok) == 0);
    CHECK (ok);
    CHECK (s.getNumActiveNotes() == 0);
}

void testSeek()
{
    std::printf ("seek: jump back mid-note releases everything and resyncs\n");
    const auto clip = makeClip();
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    s.setClip (&clip, h.sink);

    while (h.ppq < 6.3) // inside bar 2, chord sounding
        h.step (s);

    CHECK (s.getNumActiveNotes() > 0);
    const auto activeBefore = s.getNumActiveNotes();

    h.ppq = 1.0; // user clicks back to beat 2 of bar 1
    const auto jumpBlock = h.block;
    const auto evBefore = ev.size();
    h.step (s);

    CHECK (s.getNumDiscontinuities() == 1);

    int offsAtZero = 0;

    for (size_t i = evBefore; i < ev.size(); ++i)
        if (! ev[i].on && ev[i].block == jumpBlock && ev[i].offset == 0)
            ++offsAtZero;

    CHECK_MSG (offsAtZero == activeBefore, "offs at 0: %d, active before: %d", offsAtZero, activeBefore);

    // Next scheduled on is the beat-2 drum hit at ppq 1.0 -> offset 0 in the jump block.
    bool found = false;

    for (size_t i = evBefore; i < ev.size(); ++i)
        if (ev[i].on)
        {
            found = ev[i].channel == 9 && ev[i].block == jumpBlock && ev[i].offset == 0;
            break;
        }

    CHECK (found);

    // Forward seek to bar 3 downbeat, then keep playing: timing locks to the new position.
    h.ppq = 8.0;
    const auto fwdBlock = h.block;
    const auto evFwd = ev.size();
    h.step (s);
    CHECK (s.getNumDiscontinuities() == 2);

    int downbeatOns = 0;

    for (size_t i = evFwd; i < ev.size(); ++i)
        if (ev[i].on && ev[i].block == fwdBlock && ev[i].offset == 0)
            ++downbeatOns;

    CHECK_MSG (downbeatOns == 5, "downbeat ons at bar 3: %d", downbeatOns); // 3 chord + bass + kick

    for (int i = 0; i < 400; ++i)
        h.step (s);

    CHECK (s.getNumDiscontinuities() == 2);
    h.playing = false;
    h.step (s);
    bool ok = false;
    CHECK (checkBalanced (ev, ok) == 0);
    CHECK (ok);
}

void testHostLoop()
{
    std::printf ("host loop: cycle 4..8 ppq, wrap inside block and at block edge\n");
    const auto clip = makeClip();

    for (int blockSize : { 512, 700, 1000 })
    {
        AuditionScheduler s;
        std::vector<Recorded> ev;
        Host h (ev);
        h.blockSize = blockSize;
        h.looping = true;
        h.loopStart = 4.0;
        h.loopEnd = 8.0;
        h.ppq = 4.0;
        s.setClip (&clip, h.sink);

        const auto spp = h.sampleRate * 60.0 / 120.0;
        const auto blocks = (int) std::ceil (4.0 * 10.0 * spp / blockSize); // ~10 loop passes

        for (int i = 0; i < blocks; ++i)
            h.step (s);

        CHECK (! h.sink.offsetOutOfRange);
        CHECK (s.getNumDiscontinuities() == 0); // in-block wraps are predicted, not treated as seeks

        // Each pass plays bar 2 once: 3 chord + 2 bass + 4 drums = 9 ons.
        int ons = 0;

        for (auto& e : ev)
            ons += e.on ? 1 : 0;

        const auto passes = (h.sample / (4.0 * spp));
        CHECK_MSG (std::abs (ons - 9.0 * std::ceil (passes)) <= 9.0, "ons %d over %.2f passes", ons, passes);

        // Downbeat chord of bar 2 (ch 0, pitch 61) fires once per pass, one pass apart.
        std::vector<long long> downbeats;

        for (auto& e : ev)
            if (e.on && e.channel == 0 && e.pitch == 61)
                downbeats.push_back (e.absSample);

        bool evenlySpaced = downbeats.size() >= 9;

        for (size_t i = 1; i < downbeats.size(); ++i)
            if (std::llabs (downbeats[i] - downbeats[i - 1] - (long long) (4.0 * spp)) > 1)
                evenlySpaced = false;

        CHECK_MSG (evenlySpaced, "block %d: %zu downbeats", blockSize, downbeats.size());

        h.playing = false;
        h.step (s);
        bool ok = false;
        CHECK (checkBalanced (ev, ok) == 0);
        CHECK (ok);
    }
}

void testTempoRamp()
{
    std::printf ("tempo: ramp 60 -> 180 bpm over 100 bars stays on the grid\n");
    const auto clip = makeClip();
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    h.blockSize = 256;
    h.tempoAt = [] (double ppq) { return 60.0 + 120.0 * std::min (ppq / 400.0, 1.0); };
    s.setClip (&clip, h.sink);

    struct BlockInfo { double ppq, bpm; };
    std::vector<BlockInfo> blocks;

    while (h.ppq < 400.0)
    {
        auto [ppq, bpm] = h.step (s);
        blocks.push_back ({ ppq, bpm });
    }

    CHECK (s.getNumDiscontinuities() == 0);

    // Reconstruct each on's ppq from its block's mapping and compare with the grid.
    std::vector<double> expectedPpq;
    const auto ons = clipOnPpqs (clip);

    for (int k = 0; k * 16.0 < h.ppq; ++k)
        for (auto p : ons)
            if (k * 16.0 + p < h.ppq)
                expectedPpq.push_back (k * 16.0 + p);

    std::vector<double> gotPpq;

    for (auto& e : ev)
        if (e.on)
        {
            const auto& b = blocks[(size_t) e.block];
            gotPpq.push_back (b.ppq + e.offset / (h.sampleRate * 60.0 / b.bpm));
        }

    CHECK_MSG (gotPpq.size() == expectedPpq.size(), "got %zu, expected %zu", gotPpq.size(), expectedPpq.size());

    double maxErr = 0.0;

    for (size_t i = 0; i < std::min (gotPpq.size(), expectedPpq.size()); ++i)
        maxErr = std::max (maxErr, std::abs (gotPpq[i] - expectedPpq[i]));

    // One sample at 180 bpm / 48 kHz is ~6.25e-5 ppq.
    CHECK_MSG (maxErr < 1.0e-4, "max grid error %g ppq", maxErr);

    h.playing = false;
    h.step (s);
    bool ok = false;
    CHECK (checkBalanced (ev, ok) == 0);
    CHECK (ok);
}

void testJitterIsNotASeek()
{
    std::printf ("jitter: small host ppq wobble neither doubles nor drops notes\n");
    const auto clip = makeClip();
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    s.setClip (&clip, h.sink);

    int i = 0;

    while (h.ppq < 64.0)
    {
        h.step (s);
        h.ppq += ((i++ % 3) - 1) * 1.0e-4; // +-0.1 ms wobble at 120 bpm
    }

    CHECK (s.getNumDiscontinuities() == 0);

    int ons = 0;

    for (auto& e : ev)
        ons += e.on ? 1 : 0;

    const auto perLoop = (int) clipOnPpqs (clip).size();
    CHECK_MSG (ons >= 4 * perLoop && ons <= 4 * perLoop + 5, "ons %d, per loop %d", ons, perLoop); // + next downbeat

    bool ok = false;
    checkBalanced (ev, ok);
    CHECK (ok);
}

void testStopReleases()
{
    std::printf ("stop: releases sounding notes at offset 0 and stays silent\n");
    const auto clip = makeClip();
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    s.setClip (&clip, h.sink);

    while (h.ppq < 1.5)
        h.step (s);

    const auto active = s.getNumActiveNotes();
    CHECK (active > 0);

    h.playing = false;
    const auto before = ev.size();
    const auto stopBlock = h.block;
    h.step (s);

    CHECK ((int) (ev.size() - before) == active);

    for (size_t i = before; i < ev.size(); ++i)
        CHECK (! ev[i].on && ev[i].offset == 0 && ev[i].block == stopBlock);

    const auto afterStop = ev.size();

    for (int i = 0; i < 20; ++i)
        h.step (s);

    CHECK (ev.size() == afterStop);
    CHECK (s.getNumActiveNotes() == 0);

    // Restart from bar 1: downbeat plays at offset 0.
    h.playing = true;
    h.ppq = 0.0;
    const auto restartBlock = h.block;
    h.step (s);

    int downbeat = 0;

    for (auto& e : ev)
        if (e.on && e.block == restartBlock && e.offset == 0)
            ++downbeat;

    CHECK (downbeat == 5); // 3 chord + bass + kick
}

void testPreRollAndClipSwap()
{
    std::printf ("pre-roll: nothing before ppq 0; clip swap releases old notes\n");
    const auto clip = makeClip();
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    h.ppq = -2.0; // count-in
    s.setClip (&clip, h.sink);

    const auto spp = h.sampleRate * 60.0 / 120.0;

    while (h.ppq < 0.5)
        h.step (s);

    CHECK (! ev.empty());
    CHECK (ev.front().on);
    CHECK_MSG (std::llabs (ev.front().absSample - (long long) (2.0 * spp)) <= 1,
               "first event at sample %lld", ev.front().absSample);

    const auto active = s.getNumActiveNotes();
    CHECK (active > 0);

    const auto other = renderClip ({ { 0.0, 1.0, 0, 48, 100 } }, 4.0);
    const auto before = ev.size();
    s.setClip (&other, h.sink, 0);
    CHECK ((int) (ev.size() - before) == active);
    CHECK (s.getNumActiveNotes() == 0);

    s.setClip (nullptr, h.sink, 0);

    for (int i = 0; i < 50; ++i)
        h.step (s);

    bool ok = false;
    CHECK (checkBalanced (ev, ok) == 0);
    CHECK (ok);
}

void testRetriggerNeverStacks()
{
    std::printf ("retrigger: overlapping same-key notes never stack note-ons\n");
    const auto clip = renderClip ({ { 0.0, 2.0, 0, 60, 100 }, { 1.0, 2.0, 0, 60, 100 }, { 1.0, 0.5, 0, 60, 90 } }, 4.0);
    AuditionScheduler s;
    std::vector<Recorded> ev;
    Host h (ev);
    s.setClip (&clip, h.sink);

    while (h.ppq < 16.0)
        h.step (s);

    h.playing = false;
    h.step (s);

    bool ok = false;
    CHECK (checkBalanced (ev, ok) == 0);
    CHECK (ok);
}
} // namespace

int main()
{
    testRenderOrdering();

    for (double bpm : { 60.0, 120.0, 180.0 })
        for (int block : { 32, 441, 512, 4096 })
            testLoopWrapAndSync (bpm, block, block == 441 ? 44100.0 : 48000.0);

    testSeek();
    testHostLoop();
    testTempoRamp();
    testJitterIsNotASeek();
    testStopReleases();
    testPreRollAndClipSwap();
    testRetriggerNeverStacks();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
