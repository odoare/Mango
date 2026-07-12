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
#include <FxmeTools/dsp/DelayLine.h>
#include <FxmeTools/dsp/Saturator.h>
#include <FxmeTools/dsp/GrainLooper.h>
#include <FxmeTools/midi/StringSequencer.h>
#include <FxmeTools/midi/SequencerEngine.h>
#include <Dsp/OverrideParser.h>

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
int main()
{
    if (testDeterministicRandom())      return 1;
    if (testNoteDuration())             return 1;
    if (testArEnvelope())               return 1;
    if (testBitCrusher())               return 1;
    if (testDelayLine())                return 1;
    if (testSaturator())                return 1;
    if (testGrainLooperAttack())        return 1;
    if (testStringSequencerAddWithId()) return 1;
    if (testSequencerEngineEmptyBlocks()) return 1;
    if (testOverrideParser())           return 1;

    std::printf ("All %d checks passed.\n", numChecks);
    return 0;
}
