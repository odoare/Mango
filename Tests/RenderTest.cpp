/*
  ------------------------------------------------------------------------------
    RenderTest.cpp

    Offline end-to-end checks of the Mango processor (no GUI, no audio
    device): renders audio through processBlock and verifies

      1. the gater actually gates (open/closed alternation at the drawn
         rhythmic rate, free-running at 120 bpm),
      2. the same seed reproduces the exact same output; a different seed
         does not,
      3. the quantizer crushes to the expected levels,
      4. a block override string ("dur=...") changes the gate rate.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "../Source/PluginProcessor.h"
#include "../Source/Components/EffectPanel.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

#define CHECK(cond) do { if (! (cond)) { \
    std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    void setParam (MangoAudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Renders `seconds` of a 440 Hz sine through the processor. */
    std::vector<float> render (MangoAudioProcessor& p, double seconds)
    {
        p.prepareToPlay (kSampleRate, kBlockSize);

        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;

        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        const int numBlocks = (int) std::ceil (seconds * kSampleRate / kBlockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const float s = (float) std::sin (phase);
                phase += inc;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < kBlockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
        return out;
    }

    float rmsOf (const std::vector<float>& x, double t0, double t1)
    {
        const auto i0 = (size_t) (t0 * kSampleRate);
        const auto i1 = juce::jmin (x.size(), (size_t) (t1 * kSampleRate));
        if (i1 <= i0) return 0.0f;
        double acc = 0.0;
        for (size_t i = i0; i < i1; ++i)
            acc += (double) x[i] * x[i];
        return (float) std::sqrt (acc / (double) (i1 - i0));
    }

    /** Adds a block spanning the whole 16-step pattern on the given lane. */
    int addFullBlock (MangoAudioProcessor& p, int lane)
    {
        const juce::ScopedLock sl (p.engine.lock());
        return p.engine.sequencerFor (lane).addBlock (0, 16);
    }
}

//==============================================================================
static void testGater()
{
    MangoAudioProcessor p;
    setParam (p, "l0_type", 0.0f);           // Gater (lane 0 default anyway)
    addFullBlock (p, 0);

    // Defaults: P(1/4) = 1 straight only -> gate = 1 beat = 0.5 s at the
    // 120 bpm free-run: open [0, 0.5), closed [0.5, 1.0), ...
    const auto out = render (p, 2.0);

    const float openRms1   = rmsOf (out, 0.05, 0.45);
    const float closedRms1 = rmsOf (out, 0.55, 0.95);
    const float openRms2   = rmsOf (out, 1.05, 1.45);
    const float closedRms2 = rmsOf (out, 1.55, 1.95);

    CHECK (openRms1 > 0.5f);
    CHECK (closedRms1 < 0.01f);
    CHECK (openRms2 > 0.5f);
    CHECK (closedRms2 < 0.01f);
    std::printf ("gater: open %.3f / closed %.4f / open %.3f / closed %.4f\n",
                 openRms1, closedRms1, openRms2, closedRms2);
}

//==============================================================================
static void testGaterOverride()
{
    MangoAudioProcessor p;
    const int blockId = addFullBlock (p, 0);

    // dur=0.125 -> an eighth note (1/8 of a whole) = 0.25 s at 120 bpm:
    // open [0, 0.25), closed [0.25, 0.5).
    CHECK (p.setBlockContent (0, blockId, "dur=0.125"));

    const auto out = render (p, 1.0);
    CHECK (rmsOf (out, 0.02, 0.23) > 0.5f);    // open
    CHECK (rmsOf (out, 0.27, 0.48) < 0.01f);   // closed
    CHECK (rmsOf (out, 0.52, 0.73) > 0.5f);    // open again

    // And an unparsable string is rejected (but stored).
    CHECK (! p.setBlockContent (0, blockId, "dur=oops"));
    CHECK (p.blockContent (0, blockId) == "dur=oops");
    CHECK (p.engine.blockHasParseError (0, blockId));
}

//==============================================================================
static void testSeedDeterminism()
{
    auto renderWithSeed = [] (float seed)
    {
        MangoAudioProcessor p;
        addFullBlock (p, 0);
        // Several possible durations so the draw actually matters.
        setParam (p, "l0_gate_w4", 1.0f);
        setParam (p, "l0_gate_w8", 0.8f);
        setParam (p, "l0_gate_w16", 0.6f);
        setParam (p, "l0_gate_w32", 0.4f);
        setParam (p, "l0_gate_wtrip", 0.5f);
        setParam (p, "seed", seed);
        return render (p, 8.0);   // 4 pattern passes
    };

    const auto a = renderWithSeed (42.0f);
    const auto b = renderWithSeed (42.0f);
    const auto c = renderWithSeed (43.0f);

    CHECK (a == b);   // bit-exact reproduction

    double diff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        diff += std::abs ((double) a[i] - (double) c[i]);
    CHECK (diff > 1.0);   // different seed -> different gating somewhere
    std::printf ("determinism: same-seed identical, seed 42 vs 43 L1 diff = %.1f\n", diff);
}

//==============================================================================
static void testQuantizer()
{
    MangoAudioProcessor p;
    setParam (p, "l0_type", 5.0f);            // Quantizer
    setParam (p, "l0_qnt_bits", 1.0f);        // levels: -1, 0, +1 only
    addFullBlock (p, 0);

    const auto out = render (p, 0.4);
    bool onlyLevels = true;
    for (size_t i = (size_t) (0.05 * kSampleRate); i < out.size(); ++i)
    {
        const float v = std::abs (out[i]);
        onlyLevels = onlyLevels && (v < 1e-6f || std::abs (v - 1.0f) < 1e-6f);
    }
    CHECK (onlyLevels);
}

//==============================================================================
static void testStateRoundTrip()
{
    juce::MemoryBlock state;
    std::vector<float> outA;
    {
        MangoAudioProcessor a;
        const int blockId = addFullBlock (a, 0);
        CHECK (a.setBlockContent (0, blockId, "att=0.1 rel=0.1"));
        {
            const juce::ScopedLock sl (a.engine.lock());
            a.engine.sequencerFor (2).addBlock (4, 8);
        }
        a.engine.moveRow (0, +1);   // reorder lanes
        setParam (a, "seed", 7.0f);
        setParam (a, "l0_gate_w8", 0.7f);
        a.getStateInformation (state);
        outA = render (a, 4.0);
    }

    MangoAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    // Structure survived: blocks with ids/contents, lane order, params.
    {
        const juce::ScopedLock sl (b.engine.lock());
        const auto& blocks0 = b.engine.sequencerFor (0).blocks();
        CHECK (blocks0.size() == 1 && blocks0[0].content == "att=0.1 rel=0.1");
        CHECK (b.engine.sequencerFor (2).blocks().size() == 1);
    }
    const auto order = b.engine.laneOrder();
    CHECK (order[0] == 1 && order[1] == 0);
    CHECK (std::abs (b.apvts.getRawParameterValue ("l0_gate_w8")->load() - 0.7f) < 1e-4f);

    // And the same audio comes out (block ids seed the draws).
    const auto outB = render (b, 4.0);
    CHECK (outA == outB);
    std::printf ("state round-trip: identical structure and audio.\n");
}

//==============================================================================
/** Fake host playhead: playing, fixed bpm, ppq advancing per block. */
struct FakePlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        info.setPpqPosition (ppq);
        return info;
    }
    double bpm = 100.0, ppq = 0.0;
};

static void testHostSync()
{
    MangoAudioProcessor p;
    addFullBlock (p, 0);

    FakePlayHead playHead;
    p.setPlayHead (&playHead);
    p.prepareToPlay (kSampleRate, kBlockSize);

    // At 100 bpm a straight quarter gate is 0.6 s: open [0,0.6), closed [0.6,1.2).
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    const int numBlocks = (int) std::ceil (2.4 * kSampleRate / kBlockSize);
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        playHead.ppq = blk * kBlockSize * playHead.bpm / (60.0 * kSampleRate);
        for (int i = 0; i < kBlockSize; ++i)
        {
            buffer.setSample (0, i, 1.0f);
            buffer.setSample (1, i, 1.0f);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < kBlockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }
    p.setPlayHead (nullptr);

    CHECK (rmsOf (out, 0.05, 0.55) > 0.9f);    // open
    CHECK (rmsOf (out, 0.65, 1.15) < 0.01f);   // closed
    CHECK (rmsOf (out, 1.25, 1.75) > 0.9f);    // open (next cycle)
    std::printf ("host sync: gate follows 100 bpm transport.\n");
}

//==============================================================================
/** Changing a weight while a block is sounding must retime the gate
    immediately — no block re-entry, no transport restart. */
static void testLiveWeightChange()
{
    MangoAudioProcessor p;
    addFullBlock (p, 0);   // one block spanning the whole 2 s pattern
    p.prepareToPlay (kSampleRate, kBlockSize);

    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    const int numBlocks = (int) std::ceil (1.2 * kSampleRate / kBlockSize);
    bool switched = false;

    for (int blk = 0; blk < numBlocks; ++blk)
    {
        const double t = blk * kBlockSize / kSampleRate;
        if (! switched && t >= 0.6)   // mid-block, inside the closed phase
        {
            // Quarters -> straight thirty-seconds (0.0625 s at 120 bpm).
            setParam (p, "l0_gate_w4", 0.0f);
            setParam (p, "l0_gate_w32", 1.0f);
            switched = true;
        }
        for (int i = 0; i < kBlockSize; ++i)
        {
            buffer.setSample (0, i, 1.0f);
            buffer.setSample (1, i, 1.0f);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < kBlockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    // Before the change: quarter gate at 120 bpm = open [0,0.5), closed [0.5,1).
    CHECK (rmsOf (out, 0.05, 0.45) > 0.9f);
    CHECK (rmsOf (out, 0.52, 0.58) < 0.01f);

    // After the change the gate must cycle fast (~16 cycles/s): within
    // [0.75, 1.05] both loud and quiet 15 ms slices must exist.
    int loud = 0, quiet = 0;
    for (double t = 0.75; t < 1.05; t += 0.015)
    {
        const float r = rmsOf (out, t, t + 0.015);
        loud  += r > 0.5f ? 1 : 0;
        quiet += r < 0.1f ? 1 : 0;
    }
    CHECK (loud >= 3 && quiet >= 3);
    std::printf ("live weight change: gate retimes mid-block (loud %d / quiet %d slices).\n",
                 loud, quiet);
}

//==============================================================================
/** The release-curve parameter shapes the gate edge: with rel = 0.25 the
    release spans [0.375, 0.5) of the 0.5 s gate. For gain = r^gamma over a
    full-level sine the release-window RMS is 0.707*sqrt(1/(2*gamma+1)):
    fast (curve 1, gamma 6) ~ 0.20, linear ~ 0.41, slow (curve 0) ~ 0.61. */
static void testGateCurves()
{
    auto releaseRms = [] (float curve)
    {
        MangoAudioProcessor p;
        addFullBlock (p, 0);
        setParam (p, "l0_gate_att", 0.0f);
        setParam (p, "l0_gate_rel", 0.25f);
        setParam (p, "l0_gate_relcurve", curve);
        const auto out = render (p, 0.6);
        return rmsOf (out, 0.375, 0.499);
    };

    const float fast = releaseRms (1.0f);
    const float lin  = releaseRms (0.5f);
    const float slow = releaseRms (0.0f);

    CHECK (fast < 0.25f);
    CHECK (std::abs (lin - 0.41f) < 0.05f);
    CHECK (slow > 0.55f);
    CHECK (fast < lin && lin < slow);
    std::printf ("gate curves: release RMS fast %.2f / linear %.2f / slow %.2f\n",
                 fast, lin, slow);
}

//==============================================================================
/** att=1, rel=0.5 must share the open phase 2/3 : 1/3 — with linear curves
    and the default 0.5 s gate the envelope peaks at t = 1/3 s. */
static void testAttRelSharing()
{
    float a = 1.0f, r = 0.5f;
    mng::normaliseAttackRelease (a, r);
    CHECK (std::abs (a - 2.0f / 3.0f) < 1e-6f);
    CHECK (std::abs (r - 1.0f / 3.0f) < 1e-6f);

    MangoAudioProcessor p;
    addFullBlock (p, 0);
    setParam (p, "l0_gate_att", 1.0f);
    setParam (p, "l0_gate_rel", 0.5f);

    const auto out = render (p, 1.0);
    CHECK (rmsOf (out, 0.00, 0.05) < 0.15f);   // early attack: still quiet
    CHECK (rmsOf (out, 0.30, 0.36) > 0.6f);    // around the 1/3 s peak
    CHECK (rmsOf (out, 0.46, 0.495) < 0.2f);   // release tail
    CHECK (rmsOf (out, 0.55, 0.95) < 0.01f);   // closed phase
    std::printf ("att/rel sharing: peak at 2/3 of the open phase.\n");
}

//==============================================================================
static void testMuteSolo()
{
    MangoAudioProcessor p;
    addFullBlock (p, 0);
    setParam (p, "l0_mute", 1.0f);
    auto out = render (p, 1.0);
    CHECK (rmsOf (out, 0.55, 0.95) > 0.5f);   // gate bypassed: closed phase passes

    setParam (p, "l0_mute", 0.0f);
    setParam (p, "l3_solo", 1.0f);            // solo elsewhere also bypasses lane 0
    out = render (p, 1.0);
    CHECK (rmsOf (out, 0.55, 0.95) > 0.5f);

    setParam (p, "l3_solo", 0.0f);
    out = render (p, 1.0);
    CHECK (rmsOf (out, 0.55, 0.95) < 0.01f);  // gating again
    std::printf ("mute/solo: lane bypass works.\n");
}

//==============================================================================
/** With random duration weights, every pattern pass must play the same
    drawn sequence — the one the block visuals display (draws depend on
    seed/lane/block only, never on the pass). Pattern = 16 sixteenths = 2 s
    at the 120 bpm free-run; render two passes and check both follow the
    same (independently recomputed) drawn duration. */
static void testPassRepeatability()
{
    // Recompute the expected draw exactly as the engine does, choosing a
    // seed whose draw differs from the 1-beat fallback so the check cannot
    // pass by accident.
    fxme::WeightedDurationTable table;
    table.baseWeights[0] = 1.0f; table.baseWeights[1] = 0.8f;
    table.baseWeights[2] = 0.6f; table.baseWeights[3] = 0.4f;
    uint64_t seed = 0;
    double d = 1.0;
    for (uint64_t s = 1; s < 50 && d == 1.0; ++s)
    {
        seed = s;
        d = table.drawBeats (fxme::detrand::u01 (seed, 0, 0, 0));
    }
    CHECK (d != 1.0);

    MangoAudioProcessor p;
    addFullBlock (p, 0);   // block id 0
    setParam (p, "l0_gate_w4", 1.0f);
    setParam (p, "l0_gate_w8", 0.8f);
    setParam (p, "l0_gate_w16", 0.6f);
    setParam (p, "l0_gate_w32", 0.4f);
    setParam (p, "seed", (float) seed);

    const auto out = render (p, 4.0);
    for (double passStart : { 0.0, 2.0 })
    {
        const double durSec = d * 0.5;   // beats -> seconds at 120 bpm
        CHECK (rmsOf (out, passStart + 0.4 * durSec, passStart + 0.6 * durSec) > 0.6f);
        CHECK (rmsOf (out, passStart + 1.4 * durSec, passStart + 1.6 * durSec) < 0.01f);
    }
    std::printf ("pass repeatability: both passes follow the drawn %.3f beats.\n", d);
}

//==============================================================================
/** A host loop jump back into the same block must re-enter it: the pass at
    the same timeline position reproduces the same draw (by design), and the
    gate phase restarts from "open" at the block start. A dotted quarter
    (1.5 beats) does not divide the 4-beat pattern, so a missing re-enter
    would leave the gate phase-misaligned on the second pass. */
static void testLoopJumpReenter()
{
    MangoAudioProcessor p;
    addFullBlock (p, 0);
    setParam (p, "l0_gate_wstr", 0.0f);
    setParam (p, "l0_gate_wdot", 1.0f);   // only dotted quarters possible

    FakePlayHead playHead;
    playHead.bpm = 120.0;
    p.setPlayHead (&playHead);
    p.prepareToPlay (kSampleRate, kBlockSize);

    // Loop the 4-beat (2 s) pattern twice via ppq wrapping.
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    const int numBlocks = (int) std::ceil (4.0 * kSampleRate / kBlockSize);
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        const double beats = blk * kBlockSize * playHead.bpm / (60.0 * kSampleRate);
        playHead.ppq = std::fmod (beats, 4.0);
        for (int i = 0; i < kBlockSize; ++i)
        {
            buffer.setSample (0, i, 1.0f);
            buffer.setSample (1, i, 1.0f);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < kBlockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }
    p.setPlayHead (nullptr);

    // Dotted quarter = 0.75 s: each pass must start open [0, 0.75) then
    // close [0.75, 1.5). Without the re-enter, pass 2 would still be open
    // in [2.8, 3.4] (stale phase from pass 1).
    for (double passStart : { 0.0, 2.0 })
    {
        CHECK (rmsOf (out, passStart + 0.05, passStart + 0.70) > 0.9f);
        CHECK (rmsOf (out, passStart + 0.80, passStart + 1.45) < 0.01f);
    }
    std::printf ("loop jump: block re-enters, gate phase restarts each pass.\n");
}

//==============================================================================
static void dumpEditorSnapshot (const juce::String& path)
{
    MangoAudioProcessor p;
    {
        const juce::ScopedLock sl (p.engine.lock());
        const int id = p.engine.sequencerFor (0).addBlock (0, 6);
        p.engine.sequencerFor (0).setContent (id, "dur=0.125");
        p.engine.sequencerFor (1).addBlock (2, 8);    // grain
        p.engine.sequencerFor (2).addBlock (8, 6);    // delay
        p.engine.sequencerFor (3).addBlock (4, 6);    // dist
        p.engine.sequencerFor (4).addBlock (10, 6);   // filter
        p.engine.sequencerFor (5).addBlock (0, 8);    // quant
    }
    p.engine.rebuildOverrides();
    setParam (p, "numsteps", 32.0f);   // > the old 20 px/step limit: must still fit
    setParam (p, "l0_gate_att", 0.2f);
    setParam (p, "l0_gate_rel", 0.25f);
    setParam (p, "l0_gate_relcurve", 1.0f);
    setParam (p, "l1_grain_att", 1.0f);    // att+rel > 1: proportional sharing
    setParam (p, "l1_grain_rel", 0.5f);
    setParam (p, "l1_grain_relcurve", 0.8f);
    setParam (p, "l5_qnt_bits", 2.0f);

    auto writePng = [] (juce::Component& c, const juce::File& file)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds());
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (img, stream);
    };

    // Let the async grid update (and any pending UI messages) run — a real
    // host pumps the message loop; this harness must do it explicitly.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (1000, 650);
    const juce::File base (path);
    writePng (*editor, base);

    // The two most complex effect panels, standalone.
    fxme::FxmeLookAndFeel lnf;
    for (auto [type, name] : { std::pair { mng::EffectType::Gater, "gater" },
                               std::pair { mng::EffectType::FilterEnv, "filter" } })
    {
        mng::EffectPanel panel (p.apvts, 0, type, mng::theme::laneColour (0));
        panel.setLookAndFeel (&lnf);
        panel.setSize (250, 460);
        writePng (panel, base.getSiblingFile (base.getFileNameWithoutExtension()
                                              + "_" + name + ".png"));
        panel.setLookAndFeel (nullptr);
    }

    std::printf ("snapshots written next to %s\n", path.toRawUTF8());
}

//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 1)
    {
        dumpEditorSnapshot (argv[1]);
        return 0;
    }

    testGater();
    testGaterOverride();
    testSeedDeterminism();
    testQuantizer();
    testStateRoundTrip();
    testHostSync();
    testLiveWeightChange();
    testGateCurves();
    testAttRelSharing();
    testMuteSolo();
    testPassRepeatability();
    testLoopJumpReenter();

    if (failures == 0)
        std::printf ("RenderTest: all checks passed.\n");
    return failures == 0 ? 0 : 1;
}
