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
        return render (p, 8.0);   // 4 pattern passes -> 4 independent draws
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
static void dumpEditorSnapshot (const juce::String& path)
{
    MangoAudioProcessor p;
    {
        const juce::ScopedLock sl (p.engine.lock());
        const int id = p.engine.sequencerFor (0).addBlock (0, 4);
        p.engine.sequencerFor (0).setContent (id, "dur=0.125");
        p.engine.sequencerFor (2).addBlock (4, 6);
    }
    p.engine.rebuildOverrides();

    auto writePng = [] (juce::Component& c, const juce::File& file)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds());
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (img, stream);
    };

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

    if (failures == 0)
        std::printf ("RenderTest: all checks passed.\n");
    return failures == 0 ? 0 : 1;
}
