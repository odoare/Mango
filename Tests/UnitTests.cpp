/*
  ------------------------------------------------------------------------------
    UnitTests.cpp

    Console tests for the JUCE-free pieces Mango relies on: the FxmeTools
    deterministic RNG, weighted duration table, AR envelope, bit crusher,
    delay line, the StringSequencer/SequencerEngine patches, and (from phase
    5 on) the Mango override parser. Plain asserts, no framework.

    Build with -DMANGO_BUILD_TESTS=ON; run via ctest or directly.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <FxmeTools/dsp/DeterministicRandom.h>
#include <FxmeTools/midi/NoteDuration.h>
#include <FxmeTools/dsp/ArEnvelope.h>
#include <FxmeTools/dsp/BitCrusher.h>
#include <FxmeTools/dsp/Downsampler.h>
#include <FxmeTools/dsp/DelayLine.h>
#include <FxmeTools/dsp/SpectralFreeze.h>
#include <FxmeTools/dsp/Saturator.h>
#include <FxmeTools/dsp/GrainLooper.h>
#include <FxmeTools/midi/StringSequencer.h>
#include <FxmeTools/midi/SequencerEngine.h>
#include <Dsp/OverrideParser.h>
#include <Dsp/HeldNotes.h>

static int numChecks = 0;

#define CHECK(cond) do { ++numChecks; if (! (cond)) { \
    std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

static bool near (double a, double b, double tol = 1e-9)
{
    return std::fabs (a - b) <= tol;
}

//==============================================================================
static int testDeterministicRandom()
{
    using namespace fxme::detrand;

    // Same inputs -> same output; different inputs -> (almost surely) different.
    CHECK (u01 (42, 1, 2, 3, 0) == u01 (42, 1, 2, 3, 0));
    CHECK (u01 (42, 1, 2, 3, 0) != u01 (43, 1, 2, 3, 0));
    CHECK (u01 (42, 1, 2, 3, 0) != u01 (42, 1, 2, 4, 0));

    // Range and rough uniformity.
    double sum = 0.0;
    for (int i = 0; i < 10000; ++i)
    {
        const float u = u01 (7, (uint64_t) i);
        CHECK (u >= 0.0f && u < 1.0f);
        sum += u;
    }
    CHECK (std::fabs (sum / 10000.0 - 0.5) < 0.02);

    // Weighted choice: respects zero weights and proportions.
    const float w[4] = { 1.0f, 0.5f, 0.1f, 0.0f };
    int counts[4] = {};
    for (int i = 0; i < 16000; ++i)
    {
        const int k = weightedChoice (w, 4, u01 (11, (uint64_t) i));
        CHECK (k >= 0 && k < 3);   // index 3 has zero weight
        ++counts[k];
    }
    // P(0) = 1/1.6 = 0.625 within a few percent.
    CHECK (std::fabs (counts[0] / 16000.0 - 0.625) < 0.03);

    const float zeros[3] = { 0.0f, 0.0f, 0.0f };
    CHECK (weightedChoice (zeros, 3, 0.5f) == -1);
    return 0;
}

//==============================================================================
static int testNoteDuration()
{
    using namespace fxme;

    CHECK (near (noteDurationBeats (NoteBase::Quarter,      NoteMod::Straight), 1.0));
    CHECK (near (noteDurationBeats (NoteBase::Eighth,       NoteMod::Straight), 0.5));
    CHECK (near (noteDurationBeats (NoteBase::Sixteenth,    NoteMod::Dotted),   0.375));
    CHECK (near (noteDurationBeats (NoteBase::ThirtySecond, NoteMod::Triplet),  0.125 * 2.0 / 3.0));

    // All-zero weights fall back to a straight quarter.
    WeightedDurationTable t;
    t.baseWeights[0] = t.baseWeights[1] = t.baseWeights[2] = t.baseWeights[3] = 0.0f;
    CHECK (near (t.drawBeats (0.3f), 1.0));

    // Only 1/8 straight enabled -> always 0.5 beats.
    WeightedDurationTable t2;
    t2.baseWeights[0] = 0.0f; t2.baseWeights[1] = 1.0f;
    t2.modWeights[0] = 1.0f; t2.modWeights[1] = 0.0f; t2.modWeights[2] = 0.0f;
    for (int i = 0; i < 100; ++i)
        CHECK (near (t2.drawBeats (i / 100.0f), 0.5));

    // The example from the spec: P(1/4)=1, P(1/8)=0.5, P(1/16)=0.1, P(1/32)=0
    // straight only -> P(quarter) = 1/1.6.
    WeightedDurationTable t3;
    t3.baseWeights[0] = 1.0f; t3.baseWeights[1] = 0.5f;
    t3.baseWeights[2] = 0.1f; t3.baseWeights[3] = 0.0f;
    int quarters = 0;
    for (int i = 0; i < 16000; ++i)
        if (near (t3.drawBeats (fxme::detrand::u01 (5, (uint64_t) i)), 1.0))
            ++quarters;
    CHECK (std::fabs (quarters / 16000.0 - 1.0 / 1.6) < 0.03);
    return 0;
}

//==============================================================================
static int testArEnvelope()
{
    fxme::ArEnvelope env;
    CHECK (! env.isActive());

    env.trigger (2, 3, 2);
    CHECK (env.isActive());
    CHECK (near (env.nextSample(), 0.5, 1e-6));
    CHECK (near (env.nextSample(), 1.0, 1e-6));
    CHECK (near (env.nextSample(), 1.0, 1e-6));
    CHECK (near (env.nextSample(), 1.0, 1e-6));
    CHECK (near (env.nextSample(), 1.0, 1e-6));
    CHECK (near (env.nextSample(), 0.5, 1e-6));
    CHECK (near (env.nextSample(), 0.0, 1e-6));
    CHECK (! env.isActive());
    CHECK (near (env.nextSample(), 0.0, 1e-6));

    // Zero attack starts at full level.
    env.trigger (0, 2, 0);
    CHECK (near (env.nextSample(), 1.0, 1e-6));
    return 0;
}

//==============================================================================
static int testBitCrusher()
{
    fxme::BitCrusher bc;
    bc.setBits (1.0f);   // levels = 1: quantise to -1, 0, +1
    CHECK (near (bc.processSample (0.8f),  1.0, 1e-6));
    CHECK (near (bc.processSample (0.3f),  0.0, 1e-6));
    CHECK (near (bc.processSample (-0.8f), -1.0, 1e-6));

    bc.setBits (24.0f);  // effectively transparent
    CHECK (std::fabs (bc.processSample (0.123456f) - 0.123456f) < 1e-5);
    return 0;
}

//==============================================================================
static int testDownsampler()
{
    // Factor 1 is bit-transparent.
    fxme::Downsampler ds;
    ds.setFactor (1.0f);
    ds.reset();
    for (int i = 0; i < 50; ++i)
    {
        const float x = std::sin (0.37f * (float) i);
        CHECK (ds.processSample (x) == x);
    }

    // Factor 4 holds each value for 4 samples, starting at the first input.
    ds.setFactor (4.0f);
    ds.reset();
    for (int i = 0; i < 32; ++i)
    {
        const float y = ds.processSample ((float) i);
        CHECK (near (y, (float) (i - i % 4), 1e-6));
    }
    return 0;
}

//==============================================================================
static int testSpectralFreeze()
{
    using SF = fxme::SpectralFreeze;
    const int N = SF::kSize;

    SF a, b;
    a.prepare();
    b.prepare();
    a.setIdentity (42, 7);
    b.setIdentity (42, 7);
    a.startCapture();
    b.startCapture();

    // One window of a full-scale sine is captured and passed through...
    const double w = 2.0 * 3.14159265358979 * 440.0 / 48000.0;
    double passDiff = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const float x  = (float) std::sin (w * i);
        const float ya = a.processSample (x);
        b.processSample (x);
        passDiff += std::fabs (ya - x);
    }
    CHECK (passDiff < 1e-6);
    CHECK (a.isFrozen());

    // ...then the input goes SILENT and the wash must keep sounding, at
    // roughly the captured level (also pins the self-calibrated FFT gain),
    // bit-identically for two instances with the same identity.
    double acc = 0.0;
    int    cnt = 0;
    float  maxDiff = 0.0f;
    for (int i = 0; i < 6 * N; ++i)
    {
        const float ya = a.processSample (0.0f);
        const float yb = b.processSample (0.0f);
        maxDiff = std::max (maxDiff, std::fabs (ya - yb));
        if (i > N)   // past the capture->wash crossfade
        {
            acc += (double) ya * ya;
            ++cnt;
        }
    }
    const double rms = std::sqrt (acc / (double) cnt);
    CHECK (rms > 0.3 && rms < 0.85);   // random-phase OLA of a 0.707-RMS sine
    CHECK (maxDiff == 0.0f);
    return 0;
}

//==============================================================================
static int testSpectralFreezeRetrigger()
{
    using SF = fxme::SpectralFreeze;
    const int N = SF::kSize;
    const double w = 2.0 * 3.14159265358979 * 440.0 / 48000.0;

    SF a, b;
    a.prepare(); b.prepare();
    a.setIdentity (5, 9); b.setIdentity (5, 9);
    a.startCapture(); b.startCapture();

    auto feed = [&] (int count, float amp, int phase0)
    {
        for (int i = 0; i < count; ++i)
        {
            const float x = amp * (float) std::sin (w * (phase0 + i));
            a.processSample (x);
            b.processSample (x);
        }
    };

    // Capture a QUIET window, then keep feeding a LOUD sine: the wash keeps
    // playing the quiet capture (the freeze ignores live input) while the
    // rolling history fills with the loud signal.
    feed (N, 0.25f, 0);
    feed (2 * N, 1.0f, 0);

    double accBefore = 0.0; int cntB = 0;
    for (int i = 0; i < N; ++i)
    {
        const float ya = a.processSample ((float) std::sin (w * i));
        b.processSample ((float) std::sin (w * i));
        accBefore += (double) ya * ya; ++cntB;
    }
    const double rmsBefore = std::sqrt (accBefore / (double) cntB);

    // Retrigger: re-capture from the loud history, without a dry gap.
    a.retrigger();
    b.retrigger();

    feed (N, 1.0f, 0);   // skip the spectrum crossfade
    double accAfter = 0.0; int cntA = 0; float md = 0.0f;
    for (int i = 0; i < 2 * N; ++i)
    {
        const float ya = a.processSample ((float) std::sin (w * i));
        const float yb = b.processSample ((float) std::sin (w * i));
        md = std::max (md, std::fabs (ya - yb));
        accAfter += (double) ya * ya; ++cntA;
    }
    const double rmsAfter = std::sqrt (accAfter / (double) cntA);

    CHECK (rmsBefore > 0.05);            // the wash was sounding (no dry gap)
    CHECK (rmsAfter > 1.8 * rmsBefore);  // re-captured the ~4x louder input
    CHECK (md == 0.0f);                  // still bit-deterministic across retrigger
    return 0;
}

//==============================================================================
static int testSpectralFreezeMulti()
{
    const int N = fxme::SpectralFreeze::kSize;

    fxme::SpectralFreezeMulti m;
    m.prepare (2);
    m.setIdentity (42, 0x100);   // low byte clear: reserved for the channel
    m.startCapture();

    // Capture one window of the same sine on both channels...
    std::vector<float> l ((size_t) N), r ((size_t) N);
    const double w = 2.0 * 3.14159265358979 * 330.0 / 48000.0;
    for (int i = 0; i < N; ++i)
        l[(size_t) i] = r[(size_t) i] = (float) std::sin (w * i);
    float* ptrs[2] = { l.data(), r.data() };
    m.process (ptrs, 2, N);

    // ...then feed silence. Width 0: both channels must be the identical
    // mono average, and still sounding.
    m.setWidth (0.0f);
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    m.process (ptrs, 2, N);
    float  diff0 = 0.0f, level = 0.0f;
    double acc0 = 0.0;
    for (int i = N / 2; i < N; ++i)   // past the capture->wash crossfade
    {
        diff0 = std::max (diff0, std::fabs (l[(size_t) i] - r[(size_t) i]));
        level = std::max (level, std::fabs (l[(size_t) i]));
        acc0 += (double) l[(size_t) i] * l[(size_t) i];
    }
    CHECK (diff0 == 0.0f);
    CHECK (level > 0.1f);

    // Width 1: the two phase streams are decorrelated, channels differ —
    // and thanks to the equal-power blend the energy matches width 0.
    m.setWidth (1.0f);
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    m.process (ptrs, 2, N);
    float  diff1 = 0.0f;
    double acc1 = 0.0;
    for (int i = 0; i < N; ++i)
        diff1 = std::max (diff1, std::fabs (l[(size_t) i] - r[(size_t) i]));
    for (int i = N / 2; i < N; ++i)
        acc1 += (double) l[(size_t) i] * l[(size_t) i];
    CHECK (diff1 > 0.01f);

    const double rmsRatio = std::sqrt (acc0 / acc1);
    CHECK (rmsRatio > 0.7 && rmsRatio < 1.4);
    return 0;
}

//==============================================================================
static int testDelayLine()
{
    fxme::DelayLine dl;
    dl.prepare (1000.0, 1.0f);
    dl.setDelaySeconds (0.01f);   // 10 samples
    dl.setFeedback (0.0f);
    dl.reset();                   // snap smoothing to the target

    std::vector<float> out;
    for (int i = 0; i < 30; ++i)
        out.push_back (dl.processSample (i == 0 ? 1.0f : 0.0f));

    // The impulse must come back at ~10 samples, once (no feedback).
    int peak = -1;
    for (int i = 0; i < 30; ++i)
        if (out[(size_t) i] > 0.5f) { peak = i; break; }
    CHECK (peak >= 9 && peak <= 11);

    float tail = 0.0f;
    for (int i = peak + 2; i < 30; ++i)
        tail += std::fabs (out[(size_t) i]);
    CHECK (tail < 1e-3f);

    // Feedback path damping: with damp=0 an impulse echoes back nearly
    // intact for many round trips; with damp=1 the lowpass smears it, so
    // the sample right at the echo position decays much faster.
    auto echoPeak = [] (float damp, int echoIndex)
    {
        fxme::DelayLine d;
        d.prepare (48000.0, 0.1f);
        d.setDelaySeconds (0.005f);   // 240 samples
        d.setFeedback (0.9f);
        d.setDamping (damp);
        d.reset();
        float peakAbs = 0.0f;
        const int from = echoIndex * 240 - 5, to = echoIndex * 240 + 5;
        for (int i = 0; i <= to; ++i)
        {
            const float y = d.processSample (i == 0 ? 1.0f : 0.0f);
            if (i >= from)
                peakAbs = std::max (peakAbs, std::fabs (y));
        }
        return peakAbs;
    };
    const float clean = echoPeak (0.0f, 4);
    const float dark  = echoPeak (1.0f, 4);
    CHECK (clean > 0.6f);             // 0.9^3 = 0.729, undamped path is transparent
    CHECK (dark < 0.5f * clean);      // damping audibly eats the repeats

    // Feedback now reaches resonator territory (was capped at 0.98).
    {
        fxme::DelayLine d;
        d.prepare (48000.0, 0.1f);
        d.setDelaySeconds (0.005f);
        d.setFeedback (0.999f);
        d.reset();
        float first = 0.0f, tenth = 0.0f;
        for (int i = 0; i <= 10 * 240 + 5; ++i)
        {
            const float y = std::fabs (d.processSample (i == 0 ? 1.0f : 0.0f));
            if (i >= 240 - 5 && i <= 240 + 5)           first = std::max (first, y);
            if (i >= 10 * 240 - 5 && i <= 10 * 240 + 5) tenth = std::max (tenth, y);
        }
        CHECK (first > 0.9f);
        CHECK (tenth > 0.98f * first);   // ~0.999^9 of the first echo
    }

    // Portamento: a long smoothing time must still converge, a short one
    // must land the echo at the new position almost immediately.
    {
        fxme::DelayLine d;
        d.prepare (48000.0, 0.1f);
        d.setDelaySeconds (0.01f);
        d.setFeedback (0.0f);
        d.setSmoothingSeconds (0.001f);   // 1 ms porta
        d.reset();
        d.setDelaySeconds (0.005f);       // retarget AFTER reset: must glide there fast
        int peak2 = -1;
        for (int i = 0; i < 480; ++i)
            if (std::fabs (d.processSample (i == 0 ? 1.0f : 0.0f)) > 0.5f) { peak2 = i; break; }
        CHECK (peak2 >= 235 && peak2 <= 250);   // ~240 samples, not the old 480
    }
    return 0;
}

//==============================================================================
static int testSaturator()
{
    fxme::Saturator sat;
    sat.prepare (48000.0);
    sat.setModel (fxme::Saturator::Model::Standard);
    sat.setDriveDb (0.0f);
    sat.setBias (0.0f);

    // tanh at low levels ~ identity (after the DC blocker settles).
    float y = 0.0f;
    for (int i = 0; i < 100; ++i)
        y = sat.processSample (0.01f * std::sin (0.1f * (float) i));
    CHECK (std::fabs (y) < 0.05f);

    // Hard drive stays bounded.
    sat.setDriveDb (40.0f);
    for (int i = 0; i < 100; ++i)
    {
        const float o = sat.processSample (std::sin (0.3f * (float) i));
        CHECK (std::fabs (o) < 2.5f);
    }
    return 0;
}

//==============================================================================
static int testGrainLooperAttack()
{
    // Record a 100-sample DC grain at 1 kHz, then feed zeros: the output is
    // (mix-faded) grain envelope only, so the mean over a settled window
    // reflects the attack shape. mean(t^g) = 1/(g+1): g=6 -> ~0.14,
    // linear g=1 -> ~0.5, g=1/6 -> ~0.86; no attack -> ~1. The mean over
    // the first 100 samples (the recording pass-through) must match too:
    // the first grain is shaped like the repetitions, not passed raw.
    struct Levels { double first, loop; };
    auto levels = [] (float attackFraction, float gamma)
    {
        fxme::GrainLooper l;
        l.prepare (1000.0, 1.0f);
        l.setCrossfade (0.001f);            // 1-sample seams: negligible
        l.setAttack (attackFraction, gamma);
        l.trigger (0.1f);                   // 100 samples

        Levels lv { 0.0, 0.0 };
        for (int i = 0; i < 500; ++i)
        {
            const float out = l.processSample (i < 100 ? 1.0f : 0.0f);
            if (i < 100)  lv.first += out / 100.0;
            if (i >= 200) lv.loop  += out / 300.0;   // mix fully settled
        }
        return lv;
    };

    CHECK (levels (0.0f, 1.0f).loop        > 0.9);    // attack off
    CHECK (levels (1.0f, 6.0f).loop        < 0.25);   // slow swell
    const double lin = levels (1.0f, 1.0f).loop;
    CHECK (std::fabs (lin - 0.5) < 0.1);              // linear
    CHECK (levels (1.0f, 1.0f / 6.0f).loop > 0.75);   // fast

    // The first (recording) grain carries the same envelope.
    CHECK (levels (0.0f, 1.0f).first > 0.9);
    CHECK (std::fabs (levels (1.0f, 1.0f).first - 0.5) < 0.1);
    CHECK (levels (1.0f, 6.0f).first < 0.25);

    // Release, same measurement (mean(remaining^g) = 1/(g+1) too).
    auto meanWithRelease = [] (float fraction, float gamma)
    {
        fxme::GrainLooper l;
        l.prepare (1000.0, 1.0f);
        l.setCrossfade (0.001f);
        l.setRelease (fraction, gamma);
        l.trigger (0.1f);

        double acc = 0.0;
        int count = 0;
        for (int i = 0; i < 500; ++i)
        {
            const float out = l.processSample (i < 100 ? 1.0f : 0.0f);
            if (i >= 200) { acc += out; ++count; }
        }
        return acc / count;
    };

    CHECK (meanWithRelease (0.0f, 1.0f) > 0.9);    // release off
    CHECK (meanWithRelease (1.0f, 6.0f) < 0.25);   // fast: exp-decay-like tail
    CHECK (meanWithRelease (1.0f, 1.0f / 6.0f) > 0.75);   // slow: holds then drops
    return 0;
}

//==============================================================================
static int testStringSequencerAddWithId()
{
    fxme::StringSequencer seq;
    seq.setNumSteps (16);

    CHECK (seq.addBlockWithId (7, 0, 4));
    CHECK (! seq.addBlockWithId (7, 8, 2));    // id taken
    CHECK (! seq.addBlockWithId (9, 2, 4));    // overlap
    CHECK (seq.addBlockWithId (3, 8, 2));

    // The auto-id counter continues past the forced ids.
    const int autoId = seq.addBlock (12, 2);
    CHECK (autoId == 8);

    CHECK (seq.blockById (7) != nullptr && seq.blockById (7)->startStep == 0);
    CHECK (seq.blockById (3) != nullptr && seq.blockById (3)->endStep == 10);
    return 0;
}

//==============================================================================
// Shrinking the grid must not destroy blocks: numSteps is a window, and the
// blocks outside it come back unchanged when it grows again.
static int testStringSequencerGridShrink()
{
    fxme::StringSequencer seq;
    seq.setNumSteps (32);
    const int a = seq.addBlock (0, 4);      // [0,4)   stays in range
    const int b = seq.addBlock (14, 6);     // [14,20) straddles a 16-step window
    const int c = seq.addBlock (24, 4);     // [24,28) goes dormant
    seq.setContent (c, "dur=mididur");

    seq.setNumSteps (16);

    CHECK (seq.blocks().size() == 3);       // nothing erased
    CHECK (seq.dormantCount() == 1);
    CHECK (seq.isInRange (*seq.blockById (a)));
    CHECK (seq.isInRange (*seq.blockById (b)));
    CHECK (! seq.isInRange (*seq.blockById (c)));

    // The straddler keeps its real range but plays/draws only to the edge.
    CHECK (seq.blockById (b)->endStep == 20);
    CHECK (seq.playableEnd (*seq.blockById (b)) == 16);
    CHECK (seq.blockAt (15) != nullptr && seq.blockAt (15)->id == b);
    CHECK (seq.blockAt (25) == nullptr);    // dormant blocks never sound

    // An in-range block cannot be dragged out of the window...
    CHECK (seq.moveBlock (a, 30));
    CHECK (seq.blockById (a)->startStep == 10 && seq.blockById (a)->endStep == 14);
    // ...and the straddler's start is walled at the edge too.
    CHECK (seq.moveBlockStart (b, 19));
    CHECK (seq.blockById (b)->startStep == 15);

    // Growing back restores the dormant block, content included.
    seq.setNumSteps (32);
    CHECK (seq.dormantCount() == 0);
    CHECK (seq.blockById (c)->startStep == 24 && seq.blockById (c)->endStep == 28);
    CHECK (seq.blockById (c)->content == "dur=mididur");

    // State saved at 32 steps reloads intact into a 16-step window.
    fxme::StringSequencer re;
    re.setNumSteps (16);
    CHECK (re.addBlockWithId (5, 24, 4));
    CHECK (re.dormantCount() == 1);
    re.setNumSteps (32);
    CHECK (re.blockById (5)->startStep == 24 && re.blockById (5)->endStep == 28);
    return 0;
}

//==============================================================================
// canPlaceBlock backs the copy gestures (cmd-drag duplicate, cmd-D): a copy
// must land whole on free steps or not at all.
static int testStringSequencerCanPlace()
{
    fxme::StringSequencer seq;
    seq.setNumSteps (16);
    const int a = seq.addBlock (0, 4);     // [0,4)
    seq.addBlock (8, 4);                   // [8,12)

    CHECK (seq.canPlaceBlock (4, 4));      // the gap, exactly
    CHECK (! seq.canPlaceBlock (4, 5));    // one step into the next block
    CHECK (! seq.canPlaceBlock (2, 2));    // inside the first
    CHECK (seq.canPlaceBlock (12, 4));     // flush against the window's end
    CHECK (! seq.canPlaceBlock (14, 4));   // would run off it: no partial copy
    CHECK (seq.canPlaceBlock (0, 4, a));   // ...unless that block is excluded
    CHECK (! seq.canPlaceBlock (-1, 2));
    CHECK (! seq.canPlaceBlock (4, 0));
    return 0;
}

//==============================================================================
static int testStringSequencerMoveBlock()
{
    fxme::StringSequencer seq;
    seq.setNumSteps (16);
    const int a = seq.addBlock (0, 3);    // [0,3)
    const int b = seq.addBlock (6, 4);    // [6,10)
    const int c = seq.addBlock (12, 2);   // [12,14)

    // Free move, duration preserved.
    CHECK (seq.moveBlock (b, 4));
    CHECK (seq.blockById (b)->startStep == 4 && seq.blockById (b)->endStep == 8);

    // Clamped against the left neighbour...
    CHECK (seq.moveBlock (b, 0));
    CHECK (seq.blockById (b)->startStep == 3 && seq.blockById (b)->endStep == 7);

    // ...against the right neighbour...
    CHECK (seq.moveBlock (b, 11));
    CHECK (seq.blockById (b)->startStep == 8 && seq.blockById (b)->endStep == 12);

    // ...and against the pattern bounds.
    CHECK (seq.moveBlock (c, 40));
    CHECK (seq.blockById (c)->startStep == 14 && seq.blockById (c)->endStep == 16);
    CHECK (seq.moveBlock (a, -5));
    CHECK (seq.blockById (a)->startStep == 0);

    CHECK (! seq.moveBlock (99, 0));      // unknown id
    return 0;
}

//==============================================================================
static int testSequencerEngineMovedBlockExit()
{
    fxme::StringSequencer seq;
    seq.setStepSize (fxme::SeqStepSize::Quarter);
    seq.setNumSteps (8);
    const int id = seq.addBlock (2, 4);   // [2,6)

    int enters = 0, exits = 0;
    fxme::EngineCallbacks cbs;
    cbs.onBlockEnter = [&] (int, const std::string&) { ++enters; };
    cbs.onBlockExit  = [&] (int)                     { ++exits; };

    fxme::SequencerEngine engine (cbs);
    engine.setEnterEmptyBlocks (true);
    engine.start (seq);
    for (int i = 0; i < 45; ++i)          // playhead to ~4.5 beats: inside the block
        engine.advance (0.1, seq);
    CHECK (enters == 1 && exits == 0);

    // Move the block from under the playhead (as a GUI drag would): the
    // engine must exit it at the next step transition, not hang on to it.
    CHECK (seq.moveBlock (id, 0));        // now [0,4), playhead at 4.5
    for (int i = 0; i < 10; ++i)          // cross into step 5
        engine.advance (0.1, seq);
    CHECK (exits == 1);
    return 0;
}

//==============================================================================
static int testSequencerEngineEmptyBlocks()
{
    fxme::StringSequencer seq;
    seq.setStepSize (fxme::SeqStepSize::Quarter);   // 1 beat per step
    seq.setNumSteps (8);
    const int blockId = seq.addBlock (2, 2);        // steps [2,4), empty content
    CHECK (blockId >= 0);

    int enters = 0, exits = 0;
    fxme::EngineCallbacks cbs;
    cbs.onBlockEnter = [&] (int, const std::string&) { ++enters; };
    cbs.onBlockExit  = [&] (int)                     { ++exits; };

    // Default behaviour: empty blocks are skipped.
    {
        fxme::SequencerEngine engine (cbs);
        engine.start (seq);
        for (int i = 0; i < 80; ++i)
            engine.advance (0.1, seq);
        CHECK (enters == 0 && exits == 0);
    }

    // Opt-in: empty blocks fire.
    {
        enters = exits = 0;
        fxme::SequencerEngine engine (cbs);
        engine.setEnterEmptyBlocks (true);
        engine.start (seq);
        for (int i = 0; i < 80; ++i)
            engine.advance (0.1, seq);   // one full 8-beat loop
        CHECK (enters == 1 && exits == 1);
    }
    return 0;
}

//==============================================================================
static int testOverrideParser()
{
    using namespace mng;

    // Empty / whitespace = valid empty set.
    auto empty = parseOverrides ("");
    CHECK (empty.has_value() && empty->count == 0);
    CHECK (parseOverrides ("   \t ").has_value());

    // The spec example.
    auto delay = parseOverrides ("dur=0.125 fb=0.6");
    CHECK (delay.has_value() && delay->count == 2);
    CHECK (delay->find (OvKey::Dur) != nullptr);
    CHECK (delay->find (OvKey::Dur)->kind == Expr::Const);
    CHECK (near (delay->find (OvKey::Dur)->value, 0.125f, 1e-6));
    CHECK (near (delay->find (OvKey::Fb)->value, 0.6f, 1e-6));
    CHECK (delay->find (OvKey::Q) == nullptr);

    auto ks = parseOverrides ("dur=mididur fb=0.99 damp=0.3 porta=5");
    CHECK (ks.has_value());
    CHECK (near (ks->find (OvKey::Damp)->value, 0.3f, 1e-6));
    CHECK (near (ks->find (OvKey::Porta)->value, 5.0f, 1e-6));

    auto lofi = parseOverrides ("bits=4 down=8 mix=0.5");
    CHECK (lofi.has_value());
    CHECK (near (lofi->find (OvKey::Down)->value, 8.0f, 1e-6));
    CHECK (near (lofi->find (OvKey::Mix)->value, 0.5f, 1e-6));

    auto ring = parseOverrides ("f0=50 f1=2000 amp=0.7");
    CHECK (ring.has_value());
    CHECK (near (ring->find (OvKey::Amp)->value, 0.7f, 1e-6));

    auto frz = parseOverrides ("mix=0.8 width=0.25");
    CHECK (frz.has_value());
    CHECK (near (frz->find (OvKey::Width)->value, 0.25f, 1e-6));

    auto dist = parseOverrides ("drive=30 gain=-6.5");
    CHECK (dist.has_value());
    CHECK (near (dist->find (OvKey::Gain)->value, -6.5f, 1e-6));

    // mididur forms.
    auto md = parseOverrides ("dur=mididur");
    CHECK (md.has_value() && md->find (OvKey::Dur)->kind == Expr::MididurScaled);
    CHECK (near (md->find (OvKey::Dur)->eval (0.01f), 0.01f, 1e-7));

    auto md2 = parseOverrides ("dur=mididur*2");
    CHECK (md2.has_value() && near (md2->find (OvKey::Dur)->eval (0.01f), 0.02f, 1e-7));

    auto md3 = parseOverrides ("dur=mididur/4");
    CHECK (md3.has_value() && near (md3->find (OvKey::Dur)->eval (0.01f), 0.0025f, 1e-7));

    auto md4 = parseOverrides ("dur=3*mididur");
    CHECK (md4.has_value() && near (md4->find (OvKey::Dur)->eval (0.01f), 0.03f, 1e-7));

    // midifreq = 1/mididur (Hz of the last MIDI note), same expression forms.
    auto mf = parseOverrides ("f0=midifreq f1=midifreq*2");
    CHECK (mf.has_value());
    CHECK (mf->find (OvKey::F0)->kind == Expr::MidifreqScaled);
    CHECK (near (mf->find (OvKey::F0)->eval (0.01f), 100.0f, 1e-4));
    CHECK (near (mf->find (OvKey::F1)->eval (0.01f), 200.0f, 1e-4));

    auto mf2 = parseOverrides ("f0=midifreq/2 f1=3*midifreq");
    CHECK (mf2.has_value());
    CHECK (near (mf2->find (OvKey::F0)->eval (0.01f), 50.0f, 1e-4));
    CHECK (near (mf2->find (OvKey::F1)->eval (0.01f), 300.0f, 1e-4));
    CHECK (near (mf2->find (OvKey::F0)->eval (0.0f), 0.0f, 1e-7));   // no note yet: safe 0

    CHECK (! parseOverrides ("f0=midifreq/0").has_value());
    CHECK (! parseOverrides ("f0=midifrequency").has_value());

    // Vowels.
    auto vow = parseOverrides ("v0=a v1=u");
    CHECK (vow.has_value());
    CHECK (near (vow->find (OvKey::V0)->value, 0.0f, 1e-7));
    CHECK (near (vow->find (OvKey::V1)->value, 4.0f, 1e-7));

    // Probability-weight keys.
    auto w = parseOverrides ("w4=0 w32=1 wtrip=0.5");
    CHECK (w.has_value() && w->count == 3);
    CHECK (near (w->find (OvKey::W4)->value, 0.0f, 1e-7));
    CHECK (near (w->find (OvKey::W32)->value, 1.0f, 1e-7));
    CHECK (near (w->find (OvKey::Wtrip)->value, 0.5f, 1e-7));

    // More than 8 assignments (the old cap) parse fine.
    auto many = parseOverrides ("dur=1 fb=1 att=1 rel=1 q=1 f0=1 f1=1 bits=1 w4=1 w8=1");
    CHECK (many.has_value() && many->count == 10);

    // Errors: unknown key, malformed value, missing '=', division by zero.
    CHECK (! parseOverrides ("foo=1").has_value());
    CHECK (! parseOverrides ("dur=abc").has_value());
    CHECK (! parseOverrides ("dur").has_value());
    CHECK (! parseOverrides ("dur=").has_value());
    CHECK (! parseOverrides ("dur=mididur/0").has_value());
    CHECK (! parseOverrides ("dur=0.25 fb=oops").has_value());   // all-or-nothing
    return 0;
}

//==============================================================================
// Chord support: the voice digits on mididur / midifreq, and the note
// tracking they read.
static int testMidiPolyphony()
{
    using namespace mng;

    // Parsing: voice digits on both magic words, in every expression form.
    auto p = parseOverrides ("dur=mididur2 f0=midifreq3 fb=mididur1*2 q=4*midifreq4 att=mididur/2");
    CHECK (p.has_value());
    CHECK (p->find (OvKey::Dur)->kind == Expr::MididurScaled  && p->find (OvKey::Dur)->voice == 2);
    CHECK (p->find (OvKey::F0)->kind  == Expr::MidifreqScaled && p->find (OvKey::F0)->voice  == 3);
    CHECK (p->find (OvKey::Fb)->voice == 1 && near (p->find (OvKey::Fb)->value, 2.0f, 1e-6));
    CHECK (p->find (OvKey::Q)->voice  == 4 && near (p->find (OvKey::Q)->value,  4.0f, 1e-6));
    CHECK (p->find (OvKey::Att)->voice == 0);   // no digit: still the last note

    // Out-of-range or malformed digits are parse errors, not silent clamps.
    CHECK (! parseOverrides ("dur=mididur5").has_value());
    CHECK (! parseOverrides ("dur=mididur0").has_value());
    CHECK (! parseOverrides ("f0=midifreq9*2").has_value());

    // Held notes are addressed low to high, whatever order they were played.
    HeldNotes held;
    held.noteOn (64); held.noteOn (60); held.noteOn (67);
    MidiNoteState m;
    m.last = midiNotePeriod (67);
    held.fill (m);
    CHECK (m.heldCount == 3);
    CHECK (near (m.periodFor (1), midiNotePeriod (60), 1e-9));
    CHECK (near (m.periodFor (2), midiNotePeriod (64), 1e-9));
    CHECK (near (m.periodFor (3), midiNotePeriod (67), 1e-9));

    // A voice past the chord falls back to its top note (never to silence);
    // voice 0 stays the last note played.
    CHECK (near (m.periodFor (4), midiNotePeriod (67), 1e-9));
    CHECK (near (m.periodFor (0), midiNotePeriod (67), 1e-9));

    // midifreq2 on that chord is the second-lowest note's pitch.
    auto f = parseOverrides ("f0=midifreq2");
    CHECK (f.has_value());
    CHECK (near (f->find (OvKey::F0)->eval (m), 1.0f / midiNotePeriod (64), 1e-3));

    // Past four notes the OLDEST is forgotten, so the chord follows what was
    // played last: 64 then 60 drop out, 48 and 55 come in.
    held.noteOn (72); held.noteOn (48); held.noteOn (55);
    held.fill (m);
    CHECK (m.heldCount == 4);
    CHECK (near (m.periodFor (1), midiNotePeriod (48), 1e-9));
    CHECK (near (m.periodFor (4), midiNotePeriod (72), 1e-9));

    held.noteOn (55);                  // retrigger: costs no voice
    CHECK (held.size() == 4);

    // Note-off removes just that note; an empty chord falls back to `last`.
    held.noteOff (48); held.noteOff (55); held.noteOff (67); held.noteOff (72);
    CHECK (held.size() == 0);
    held.fill (m);
    CHECK (m.heldCount == 0);
    CHECK (near (m.periodFor (2), m.last, 1e-9));

    held.noteOn (60);
    held.allNotesOff();                // panic
    CHECK (held.size() == 0);
    return 0;
}

//==============================================================================
int main()
{
    if (testDeterministicRandom())      return 1;
    if (testNoteDuration())             return 1;
    if (testArEnvelope())               return 1;
    if (testBitCrusher())               return 1;
    if (testDownsampler())              return 1;
    if (testSpectralFreeze())           return 1;
    if (testSpectralFreezeRetrigger())  return 1;
    if (testSpectralFreezeMulti())      return 1;
    if (testDelayLine())                return 1;
    if (testSaturator())                return 1;
    if (testGrainLooperAttack())        return 1;
    if (testStringSequencerAddWithId()) return 1;
    if (testStringSequencerGridShrink()) return 1;
    if (testStringSequencerCanPlace()) return 1;
    if (testStringSequencerMoveBlock()) return 1;
    if (testSequencerEngineMovedBlockExit()) return 1;
    if (testSequencerEngineEmptyBlocks()) return 1;
    if (testOverrideParser())           return 1;
    if (testMidiPolyphony())            return 1;

    std::printf ("All %d checks passed.\n", numChecks);
    return 0;
}
