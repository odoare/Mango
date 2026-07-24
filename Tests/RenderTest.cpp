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
#include "../Source/Dsp/Effects/PannerEffect.h"
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

    // Downsampling: with full bit depth and a hold of 8 samples the output
    // becomes piecewise constant — far fewer sample-to-sample changes than
    // the continuous input, but not silence/DC either.
    setParam (p, "l0_qnt_bits", 24.0f);
    setParam (p, "l0_qnt_down", 8.0f);
    const auto held = render (p, 0.4);
    int changes = 0, counted = 0;
    for (size_t i = (size_t) (0.05 * kSampleRate) + 1; i < held.size(); ++i, ++counted)
        if (held[i] != held[i - 1])
            ++changes;
    CHECK (changes < counted / 6);
    CHECK (changes > counted / 16);
}

//==============================================================================
static void testRingMod()
{
    MangoAudioProcessor p;
    setParam (p, "l0_type", 6.0f);            // Ring mod
    setParam (p, "l0_ring_f0", 1000.0f);
    setParam (p, "l0_ring_f1", 1000.0f);      // f0 = f1: static carrier
    setParam (p, "l0_ring_amp", 1.0f);
    addFullBlock (p, 0);

    // Full ring modulation of a sine by a sine halves the power:
    // RMS = 0.707 (input) * 0.707 (carrier) = 0.5.
    const float rmsFull = rmsOf (render (p, 0.4), 0.05, 0.4);
    CHECK (std::abs (rmsFull - 0.5f) < 0.03f);

    // Amount 0 is transparent: the input sine's RMS comes through.
    setParam (p, "l0_ring_amp", 0.0f);
    const float rmsDry = rmsOf (render (p, 0.4), 0.05, 0.4);
    CHECK (std::abs (rmsDry - 0.7071f) < 0.02f);

    std::printf ("ring mod: full-mod rms = %.3f, amp=0 rms = %.3f\n", rmsFull, rmsDry);
}

//==============================================================================
static void testReverser()
{
    MangoAudioProcessor p;
    setParam (p, "l0_type", 7.0f);            // Reverser
    const int blockId = addFullBlock (p, 0);
    CHECK (p.setBlockContent (0, blockId, "dur=0.25 fade=0"));   // 1-beat slices, no seam fade

    const auto out = render (p, 2.0);         // one pattern pass = 4 slices

    // Rebuild the input exactly as render() generated it.
    std::vector<float> in (out.size());
    {
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        for (auto& s : in)
        {
            s = (float) std::sin (phase);
            phase += inc;
        }
    }

    const int n = (int) (0.5 * kSampleRate);  // 1 beat at the free-run 120 bpm
    float maxDiff = 0.0f;

    // Slice 0 passes the input through (nothing recorded yet)...
    for (int i = 0; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (out[(size_t) i] - in[(size_t) i]));

    // ...then every slice is the previous one, sample-exact, backwards.
    for (int s = 1; s < 4; ++s)
        for (int j = 0; j < n; ++j)
            maxDiff = std::max (maxDiff, std::abs (out[(size_t) (s * n + j)]
                                                   - in[(size_t) (s * n - 1 - j)]));
    CHECK (maxDiff < 1e-6f);
    std::printf ("reverser: mirror max diff = %g\n", maxDiff);
}

//==============================================================================
static void testFreeze()
{
    auto renderFreeze = []
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 8.0f);        // Freeze
        addFullBlock (p, 0);
        return render (p, 0.4);
    };
    const auto out  = renderFreeze();
    const auto out2 = renderFreeze();

    std::vector<float> in (out.size());
    {
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        for (auto& s : in)
        {
            s = (float) std::sin (phase);
            phase += inc;
        }
    }

    // Pass-through during the ~43 ms capture window...
    float head = 0.0f;
    for (int i = 0; i < 2000; ++i)
        head = std::max (head, std::abs (out[(size_t) i] - in[(size_t) i]));
    CHECK (head < 1e-6f);

    // ...then a sustained wash at roughly the input level, decorrelated
    // from the live input (a pass-through would match the sine exactly).
    const float rms = rmsOf (out, 0.15, 0.4);
    CHECK (rms > 0.2f && rms < 0.9f);
    double l1 = 0.0;
    const size_t i0 = (size_t) (0.15 * kSampleRate);
    for (size_t i = i0; i < out.size(); ++i)
        l1 += std::abs ((double) out[i] - (double) in[i]);
    CHECK (l1 / (double) (out.size() - i0) > 0.05);

    // Deterministic: two renders are bit-identical.
    float md = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
        md = std::max (md, std::abs (out[i] - out2[i]));
    CHECK (md == 0.0f);

    // Width: the L/R washes have independent phase streams; width=0 blends
    // them to an identical mono pair, width=1 (default) leaves them apart.
    auto renderStereo = [] (float width)
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 8.0f);
        setParam (p, "l0_frz_width", width);
        addFullBlock (p, 0);
        p.prepareToPlay (kSampleRate, kBlockSize);

        std::pair<std::vector<float>, std::vector<float>> lr;
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        for (int b = 0; b < (int) std::ceil (0.3 * kSampleRate / kBlockSize); ++b)
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
            {
                lr.first.push_back (buffer.getSample (0, i));
                lr.second.push_back (buffer.getSample (1, i));
            }
        }
        return lr;
    };

    const auto mono = renderStereo (0.0f);
    float lrDiff = 0.0f;
    for (size_t i = 0; i < mono.first.size(); ++i)
        lrDiff = std::max (lrDiff, std::abs (mono.first[i] - mono.second[i]));
    CHECK (lrDiff < 1e-6f);

    const auto wide = renderStereo (1.0f);
    double lrL1 = 0.0;
    for (size_t i = (size_t) (0.15 * kSampleRate); i < wide.first.size(); ++i)
        lrL1 += std::abs ((double) wide.first[i] - (double) wide.second[i]);
    CHECK (lrL1 > 1.0);

    std::printf ("freeze: wash rms = %.3f, decorrelation L1 = %.3f, width ok\n",
                 rms, l1 / (double) (out.size() - i0));
}

//==============================================================================
static void testBuses()
{
    auto makeInput = [] (size_t count)
    {
        std::vector<float> in (count);
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        for (auto& s : in)
        {
            s = (float) std::sin (phase);
            phase += inc;
        }
        return in;
    };

    // Two parallel buses with no blocks anywhere: each bus passes its copy
    // of the dry input, so the summed output is exactly twice the input.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        const auto out = render (p, 0.2);
        const auto in  = makeInput (out.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (out[i] - 2.0f * in[i]));
        CHECK (maxDiff < 1e-6f);
    }

    // A quantized bus in parallel with a clean bus: out = quant(in) + in,
    // so out - in is only the 1-bit levels -1/0/+1 (a serial chain would
    // leave just the quantized levels themselves).
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 5.0f);            // Quantizer on bus 0
        setParam (p, "l0_qnt_bits", 1.0f);
        setParam (p, "l1_busstart", 1.0f);        // lane 2 starts bus 1 (idle = dry)
        addFullBlock (p, 0);
        const auto out = render (p, 0.4);
        const auto in  = makeInput (out.size());
        bool onlyLevels = true;
        for (size_t i = (size_t) (0.05 * kSampleRate); i < out.size(); ++i)
        {
            const float d = std::abs (out[i] - in[i]);
            onlyLevels = onlyLevels && (d < 1e-5f || std::abs (d - 1.0f) < 1e-5f);
        }
        CHECK (onlyLevels);
    }
    std::printf ("buses: parallel sum and idle pass-through verified\n");
}

//==============================================================================
/** Aux send: with the two aux output buses enabled, a lane running the
    AuxSend effect gates its signal into them while leaving the main path at
    the passthrough level. */
static void testAuxSend()
{
    MangoAudioProcessor p;
    CHECK (p.enableAllBuses());
    CHECK (p.getTotalNumOutputChannels() == 6);

    setParam (p, "l0_type", 9.0f);          // Aux send
    setParam (p, "l0_aux_send1", 1.0f);
    setParam (p, "l0_aux_send2", 0.5f);
    setParam (p, "l0_aux_pass", 0.25f);
    setParam (p, "l0_aux_att", 0.0f);       // hard edges: exact levels to compare
    setParam (p, "l0_aux_rel", 0.0f);
    addFullBlock (p, 0);

    // Defaults draw a 1-beat gate = 0.5 s at the 120 bpm free-run: the send
    // is open on [0, 0.5) and closed on [0.5, 1.0).
    p.prepareToPlay (kSampleRate, kBlockSize);

    std::vector<float> mainOut, aux1Out, aux2Out;
    juce::AudioBuffer<float> buffer (6, kBlockSize);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
    const int numBlocks = (int) std::ceil (1.0 * kSampleRate / kBlockSize);

    for (int b = 0; b < numBlocks; ++b)
    {
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i)
        {
            const float s = (float) std::sin (phase);
            phase += inc;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < kBlockSize; ++i)
        {
            mainOut.push_back (buffer.getSample (0, i));
            aux1Out.push_back (buffer.getSample (2, i));
            aux2Out.push_back (buffer.getSample (4, i));
        }
    }

    const float dryRms = 1.0f / std::sqrt (2.0f);

    // Main path: the flat passthrough level, gate open or closed.
    CHECK (std::abs (rmsOf (mainOut, 0.05, 0.45) - 0.25f * dryRms) < 0.01f);
    CHECK (std::abs (rmsOf (mainOut, 0.55, 0.95) - 0.25f * dryRms) < 0.01f);

    // Aux 1 at full send, aux 2 at half — both gated, both silent when shut.
    CHECK (std::abs (rmsOf (aux1Out, 0.05, 0.45) - dryRms) < 0.01f);
    CHECK (std::abs (rmsOf (aux2Out, 0.05, 0.45) - 0.5f * dryRms) < 0.01f);
    CHECK (rmsOf (aux1Out, 0.55, 0.95) < 0.001f);
    CHECK (rmsOf (aux2Out, 0.55, 0.95) < 0.001f);

    std::printf ("aux send: main %.3f / aux1 %.3f %.4f / aux2 %.3f %.4f\n",
                 rmsOf (mainOut, 0.05, 0.45),
                 rmsOf (aux1Out, 0.05, 0.45), rmsOf (aux1Out, 0.55, 0.95),
                 rmsOf (aux2Out, 0.05, 0.45), rmsOf (aux2Out, 0.55, 0.95));

    // With the aux buses disabled again the effect is just a level control
    // on the main path — and must not touch anything outside it.
    {
        MangoAudioProcessor q;
        setParam (q, "l0_type", 9.0f);
        setParam (q, "l0_aux_pass", 0.5f);
        addFullBlock (q, 0);
        const auto out = render (q, 0.4);
        CHECK (std::abs (rmsOf (out, 0.05, 0.35) - 0.5f * dryRms) < 0.01f);
    }
}

//==============================================================================
/** Panner: three positions on the gater's clock. Checks the cycle order,
    and that the Random mode's audible sequence is exactly what panStateAt
    predicts — the invariant that keeps the block picture honest. */
static void testPanner()
{
    // Renders `seconds` and returns the two channels separately.
    auto renderStereo = [] (MangoAudioProcessor& p, double seconds)
    {
        p.prepareToPlay (kSampleRate, kBlockSize);

        std::vector<float> l, rr;
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
            {
                l.push_back (buffer.getSample (0, i));
                rr.push_back (buffer.getSample (1, i));
            }
        }
        return std::make_pair (l, rr);
    };

    const float dryRms = 1.0f / std::sqrt (2.0f);

    // Cycle: left, centre, right on successive 1-beat steps (0.5 s at the
    // 120 bpm free-run).
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 10.0f);        // Panner
        setParam (p, "l0_pan_mode", 0.0f);     // Cycle ->
        setParam (p, "l0_pan_glide", 0.0f);    // hard steps: exact levels
        addFullBlock (p, 0);

        const auto [l, r] = renderStereo (p, 1.5);

        CHECK (std::abs (rmsOf (l, 0.05, 0.45) - dryRms) < 0.01f);   // step 0: hard left
        CHECK (rmsOf (r, 0.05, 0.45) < 0.001f);
        CHECK (std::abs (rmsOf (l, 0.55, 0.95) - dryRms) < 0.01f);   // step 1: centre
        CHECK (std::abs (rmsOf (r, 0.55, 0.95) - dryRms) < 0.01f);
        CHECK (rmsOf (l, 1.05, 1.45) < 0.001f);                      // step 2: hard right
        CHECK (std::abs (rmsOf (r, 1.05, 1.45) - dryRms) < 0.01f);

        std::printf ("panner cycle: L %.3f %.3f %.4f / R %.4f %.3f %.3f\n",
                     rmsOf (l, 0.05, 0.45), rmsOf (l, 0.55, 0.95), rmsOf (l, 1.05, 1.45),
                     rmsOf (r, 0.05, 0.45), rmsOf (r, 0.55, 0.95), rmsOf (r, 1.05, 1.45));
    }

    // Cycle <->: left, centre, right, centre — it turns round rather than
    // jumping back across the image.
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 10.0f);
        setParam (p, "l0_pan_mode", 2.0f);     // Cycle <->
        setParam (p, "l0_pan_glide", 0.0f);
        addFullBlock (p, 0);

        const auto [l, r] = renderStereo (p, 2.0);
        const float want[4][2] = { { 1.0f, 0.0f }, { 1.0f, 1.0f },
                                   { 0.0f, 1.0f }, { 1.0f, 1.0f } };
        bool ok = true;
        for (int step = 0; step < 4; ++step)
        {
            const double t0 = 0.5 * step + 0.05, t1 = 0.5 * (step + 1) - 0.05;
            ok = ok && std::abs (rmsOf (l, t0, t1) - want[step][0] * dryRms) < 0.01f
                    && std::abs (rmsOf (r, t0, t1) - want[step][1] * dryRms) < 0.01f;
        }
        CHECK (ok);
    }

    // Cycle <- is the mirror of Cycle ->: same sequence, channels swapped.
    {
        for (int step = 0; step < 6; ++step)
            CHECK (mng::panStateAt (mng::PanMode::CycleLeft, 0, 0, 0, step)
                   == -mng::panStateAt (mng::PanMode::CycleRight, 0, 0, 0, step));
    }

    // Random: the audible balance of every step must match panStateAt, the
    // same function the block visual draws from.
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 10.0f);
        setParam (p, "l0_pan_mode", 3.0f);     // Random
        setParam (p, "l0_pan_glide", 0.0f);
        setParam (p, "seed", 4242.0f);
        const int blockId = addFullBlock (p, 0);

        const auto [l, r] = renderStereo (p, 2.0);

        bool allMatch = true;
        for (int step = 0; step < 4; ++step)
        {
            const float pan = mng::panStateAt (mng::PanMode::Random, 4242, 0, blockId, step);
            const float t0 = 0.5 * step + 0.05, t1 = 0.5 * (step + 1) - 0.05;
            const float wantL = (pan > 0.0f ? 1.0f - pan : 1.0f) * dryRms;
            const float wantR = (pan < 0.0f ? 1.0f + pan : 1.0f) * dryRms;

            allMatch = allMatch && std::abs (rmsOf (l, t0, t1) - wantL) < 0.01f
                                && std::abs (rmsOf (r, t0, t1) - wantR) < 0.01f;
        }
        CHECK (allMatch);

        // ...and the draws really do wander over all three positions (a
        // constant sequence would satisfy the match check above).
        bool seen[3] = {};
        for (int step = 0; step < 60; ++step)
        {
            const float pan = mng::panStateAt (mng::PanMode::Random, 4242, 0, blockId, step);
            seen[pan < -0.5f ? 0 : (pan > 0.5f ? 2 : 1)] = true;
        }
        CHECK (seen[0] && seen[1] && seen[2]);
        std::printf ("panner random: sequence matches panStateAt\n");
    }

    // Mix at 0 is transparent, whatever the position.
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 10.0f);
        setParam (p, "l0_pan_mix", 0.0f);
        addFullBlock (p, 0);
        const auto [l, r] = renderStereo (p, 0.6);
        CHECK (std::abs (rmsOf (l, 0.05, 0.55) - dryRms) < 0.001f);
        CHECK (std::abs (rmsOf (r, 0.05, 0.55) - dryRms) < 0.001f);
    }
}

//==============================================================================
static void testBusModes()
{
    auto makeInput = [] (size_t count)
    {
        std::vector<float> in (count);
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        for (auto& s : in)
        {
            s = (float) std::sin (phase);
            phase += inc;
        }
        return in;
    };

    // Mode 2: buses 1-3 (idle -> dry copies) feed bus 4, whose 1-bit
    // quantizer sees the tripled input: the output is pure integers up to
    // +-3 — impossible in parallel routing.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        setParam (p, "l2_busstart", 1.0f);
        setParam (p, "l3_busstart", 1.0f);
        setParam (p, "busmode", 2.0f);
        setParam (p, "l3_type", 5.0f);           // Quantizer on the post bus
        setParam (p, "l3_qnt_bits", 1.0f);
        addFullBlock (p, 3);
        const auto out = render (p, 0.4);
        bool integers = true;
        float peak = 0.0f;
        for (size_t i = (size_t) (0.05 * kSampleRate); i < out.size(); ++i)
        {
            integers = integers && std::abs (out[i] - std::round (out[i])) < 1e-4f;
            peak = std::max (peak, std::abs (out[i]));
        }
        CHECK (integers);
        CHECK (peak > 2.5f);
    }

    // Mode 1: buses 1+2 feed bus 3 (quantizer -> integers up to +-2) while
    // bus 4 stays parallel and adds the clean input on top.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        setParam (p, "l2_busstart", 1.0f);
        setParam (p, "l3_busstart", 1.0f);
        setParam (p, "busmode", 1.0f);
        setParam (p, "l2_type", 5.0f);           // Quantizer on the post bus (bus 3)
        setParam (p, "l2_qnt_bits", 1.0f);
        addFullBlock (p, 2);
        const auto out = render (p, 0.4);
        const auto in  = makeInput (out.size());
        bool integers = true;
        float peak = 0.0f;
        for (size_t i = (size_t) (0.05 * kSampleRate); i < out.size(); ++i)
        {
            const float d = out[i] - in[i];     // remove the parallel bus 4
            integers = integers && std::abs (d - std::round (d)) < 1e-4f;
            peak = std::max (peak, std::abs (d));
        }
        CHECK (integers);
        CHECK (peak > 1.5f);
    }

    // Mode 3, the diamond: bus 1 -> buses 2 and 3 -> bus 4. With every bus
    // idle each just passes what it is fed, so the split-and-remix doubles
    // the input exactly — and nothing else: parallel would give 4x, modes 1
    // and 2 give 3x.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        setParam (p, "l2_busstart", 1.0f);
        setParam (p, "l3_busstart", 1.0f);
        setParam (p, "busmode", 3.0f);
        const auto out = render (p, 0.2);
        const auto in  = makeInput (out.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (out[i] - 2.0f * in[i]));
        CHECK (maxDiff < 1e-6f);
    }

    // ...and bus 4 really sees the *mix* of 2 and 3: a 1-bit quantizer there
    // is handed 2x the input, so the output is integers reaching +-2.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        setParam (p, "l2_busstart", 1.0f);
        setParam (p, "l3_busstart", 1.0f);
        setParam (p, "busmode", 3.0f);
        setParam (p, "l3_type", 5.0f);           // Quantizer on the tail bus
        setParam (p, "l3_qnt_bits", 1.0f);
        addFullBlock (p, 3);
        const auto out = render (p, 0.4);
        bool integers = true;
        float peak = 0.0f;
        for (size_t i = (size_t) (0.05 * kSampleRate); i < out.size(); ++i)
        {
            integers = integers && std::abs (out[i] - std::round (out[i])) < 1e-4f;
            peak = std::max (peak, std::abs (out[i]));
        }
        CHECK (integers);
        CHECK (peak > 1.5f);
        CHECK (peak < 2.5f);                     // not the 3x of modes 1/2
    }

    // Mode 4, the fan-out: bus 1 is the common front end for buses 2-4. A
    // 1-bit quantizer on bus 1 means all three tails carry the same -1/0/+1,
    // so the sum only ever lands on -3, 0 or +3. Parallel routing would put
    // the clean input alongside it and break that.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        setParam (p, "l2_busstart", 1.0f);
        setParam (p, "l3_busstart", 1.0f);
        setParam (p, "busmode", 4.0f);
        setParam (p, "l0_type", 5.0f);           // Quantizer on the head bus
        setParam (p, "l0_qnt_bits", 1.0f);
        addFullBlock (p, 0);
        const auto out = render (p, 0.4);
        bool tripled = true;
        for (size_t i = (size_t) (0.05 * kSampleRate); i < out.size(); ++i)
        {
            const float v = std::abs (out[i]);
            tripled = tripled && (v < 1e-4f || std::abs (v - 3.0f) < 1e-4f);
        }
        CHECK (tripled);
    }

    // Fan-out degrades instead of falling back: with only two buses it is a
    // plain 1 -> 2 series, so two idle buses pass the input once (parallel
    // would double it).
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);       // two buses only
        setParam (p, "busmode", 4.0f);
        const auto out = render (p, 0.2);
        const auto in  = makeInput (out.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (out[i] - in[i]));
        CHECK (maxDiff < 1e-6f);
    }

    // The diamond needs all four buses; with three it falls back to
    // parallel, which passes three idle copies.
    {
        MangoAudioProcessor p;
        setParam (p, "l1_busstart", 1.0f);
        setParam (p, "l2_busstart", 1.0f);
        setParam (p, "busmode", 3.0f);
        const auto out = render (p, 0.2);
        const auto in  = makeInput (out.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (out[i] - 3.0f * in[i]));
        CHECK (maxDiff < 1e-6f);
    }

    // Per-bus volume is a plain output gain: at 0 the bus is silent, it does
    // NOT fall back to passing its input through.
    {
        MangoAudioProcessor p;
        setParam (p, "l0_type", 5.0f);
        setParam (p, "l0_qnt_bits", 1.0f);
        setParam (p, "bus1_vol", 0.0f);
        addFullBlock (p, 0);
        const auto out = render (p, 0.2);
        float peak = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            peak = std::max (peak, std::abs (out[i]));
        CHECK (peak < 1e-6f);        // muted, not dry-through
    }

    // Half volume halves the output; a single bus at vol 1 is bit-transparent.
    {
        MangoAudioProcessor p;
        setParam (p, "bus1_vol", 0.5f);
        const auto out = render (p, 0.2);
        const auto in  = makeInput (out.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (out[i] - 0.5f * in[i]));
        CHECK (maxDiff < 1e-6f);
    }

    // ...and pan hard right silences the bus's left channel.
    {
        MangoAudioProcessor p;
        setParam (p, "bus1_pan", 1.0f);
        p.prepareToPlay (kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        float maxL = 0.0f, maxRDiff = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            std::vector<float> ref ((size_t) kBlockSize);
            for (int i = 0; i < kBlockSize; ++i)
            {
                ref[(size_t) i] = (float) std::sin (phase);
                phase += inc;
                buffer.setSample (0, i, ref[(size_t) i]);
                buffer.setSample (1, i, ref[(size_t) i]);
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < kBlockSize; ++i)
            {
                maxL     = std::max (maxL, std::abs (buffer.getSample (0, i)));
                maxRDiff = std::max (maxRDiff, std::abs (buffer.getSample (1, i) - ref[(size_t) i]));
            }
        }
        CHECK (maxL < 1e-6f);
        CHECK (maxRDiff < 1e-6f);
    }

    std::printf ("bus modes: serial feeds, volume mute and pan verified\n");
}

//==============================================================================
static void testEffectMix()
{
    // Gater chopping at full depth, but mix=0: the effect must be
    // bit-transparent (the universal per-effect mix, representative case).
    MangoAudioProcessor p;
    addFullBlock (p, 0);
    setParam (p, "l0_gate_mix", 0.0f);
    const auto out = render (p, 0.3);

    std::vector<float> in (out.size());
    {
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
        for (auto& s : in)
        {
            s = (float) std::sin (phase);
            phase += inc;
        }
    }
    float maxDiff = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (out[i] - in[i]));
    CHECK (maxDiff < 1e-6f);
    std::printf ("effect mix: gate at mix=0 is transparent\n");
}

//==============================================================================
static void testConfigBank()
{
    auto pump    = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); };
    auto getP    = [] (MangoAudioProcessor& p, const juce::String& id)
                   { return p.apvts.getRawParameterValue (id)->load(); };
    auto numBlocks = [] (MangoAudioProcessor& p, int lane)
    {
        const juce::ScopedLock sl (p.engine.lock());
        return (int) p.engine.sequencerFor (lane).blocks().size();
    };

    MangoAudioProcessor p;
    setParam (p, "configsync", 0.0f);   // immediate recall

    // --- config 1: a block on lane 0, seed 7, gate attack 0.4, WITH params.
    const int blockA = addFullBlock (p, 0);
    CHECK (p.setBlockContent (0, blockA, "dur=0.25"));
    setParam (p, "seed", 7.0f);
    setParam (p, "l0_gate_att", 0.4f);
    p.storeConfig (0, true);
    CHECK (p.configIsStored (0));
    CHECK (p.configHasParams (0));
    CHECK (p.activeConfig() == 0);
    CHECK (! p.configIsModified());

    // --- config 2: different blocks / seed / attack, stored WITHOUT params.
    {
        const juce::ScopedLock sl (p.engine.lock());
        p.engine.sequencerFor (0).clear();
        p.engine.sequencerFor (1).addBlock (0, 8);
    }
    setParam (p, "seed", 21.0f);
    setParam (p, "l0_gate_att", 0.9f);
    p.storeConfig (1, false);
    CHECK (p.configIsStored (1));
    CHECK (! p.configHasParams (1));

    // --- recall 1: blocks, seed and (stored with params) attack all return.
    p.requestConfigRecall (0);
    pump();
    CHECK (p.activeConfig() == 0);
    CHECK (numBlocks (p, 0) == 1);
    CHECK (numBlocks (p, 1) == 0);
    CHECK (std::abs (getP (p, "seed") - 7.0f) < 0.5f);
    CHECK (std::abs (getP (p, "l0_gate_att") - 0.4f) < 1e-3f);
    CHECK (p.blockContent (0, blockA) == "dur=0.25");

    // --- recall 2: blocks-only, so the attack keeps its live value while
    // the blocks and the seed (an "always" member) come from the config.
    setParam (p, "l0_gate_att", 0.7f);
    p.requestConfigRecall (1);
    pump();
    CHECK (numBlocks (p, 0) == 0);
    CHECK (numBlocks (p, 1) == 1);
    CHECK (std::abs (getP (p, "seed") - 21.0f) < 0.5f);
    CHECK (std::abs (getP (p, "l0_gate_att") - 0.7f) < 1e-3f);   // untouched

    // --- mute is never stored: a recall must not clobber performance state.
    setParam (p, "l0_mute", 1.0f);
    p.requestConfigRecall (0);
    pump();
    CHECK (getP (p, "l0_mute") > 0.5f);
    setParam (p, "l0_mute", 0.0f);

    // --- an empty slot never clobbers the live setup.
    const int before = numBlocks (p, 0);
    CHECK (! p.configIsStored (5));
    p.requestConfigRecall (5);
    pump();
    CHECK (numBlocks (p, 0) == before);

    // --- the modified marker follows edits to the active config.
    p.requestConfigRecall (0);
    pump();
    CHECK (! p.configIsModified());
    setParam (p, "seed", 99.0f);
    CHECK (p.configIsModified());

    // --- bus volume/pan are structure, not voicing: a blocks-only config
    // restores them (a bus's membership is defined by the config, so its
    // level and pan have to travel with it).
    setParam (p, "bus1_vol", 0.5f);
    setParam (p, "bus1_pan", -0.75f);
    p.storeConfig (2, false);
    CHECK (! p.configHasParams (2));
    setParam (p, "bus1_vol", 1.0f);
    setParam (p, "bus1_pan", 0.0f);
    p.requestConfigRecall (2);
    pump();
    CHECK (std::abs (getP (p, "bus1_vol") - 0.5f) < 1e-3f);
    CHECK (std::abs (getP (p, "bus1_pan") + 0.75f) < 1e-3f);

    // --- undo restores what a store overwrote.
    p.requestConfigRecall (0);
    pump();
    p.storeConfig (1, false);          // overwrite config 2 with config 1's setup
    CHECK (numBlocks (p, 0) == 1);
    CHECK (p.canUndoStore());
    p.undoStore();
    p.requestConfigRecall (1);
    pump();
    CHECK (numBlocks (p, 0) == 0);     // config 2's original lane-1 pattern is back
    CHECK (numBlocks (p, 1) == 1);

    std::printf ("config bank: store/recall, blocks-only, never-set, undo verified\n");
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

        // GUI view: not a parameter, but it rides along in the session.
        a.view().selectedLane  = 0;
        a.view().selectedBlock = blockId;
        a.view().configsShown  = true;

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

    // The GUI view came back, and points at a block that still exists.
    CHECK (b.view().selectedLane == 0);
    CHECK (b.view().selectedBlock >= 0);
    CHECK (b.view().configsShown);
    CHECK (! b.view().presetsShown);
    CHECK (b.blockExists (b.view().selectedLane, b.view().selectedBlock));

    // A fresh processor has no view, and a stale selection is rejected
    // rather than pointing the editor at a block that is gone.
    {
        MangoAudioProcessor c;
        CHECK (c.view().selectedLane == -1);
        CHECK (! c.view().configsShown);
        CHECK (! c.blockExists (0, 12345));
    }

    // And the same audio comes out (block ids seed the draws).
    const auto outB = render (b, 4.0);
    CHECK (outA == outB);
    std::printf ("state round-trip: identical structure, audio and GUI view.\n");
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
/** Rows beyond the numlanes parameter (default 4) are bypassed: they keep
    sequencing but do not process audio. */
static void testLaneCount()
{
    MangoAudioProcessor p;
    setParam (p, "l4_type", 0.0f);        // lane 4 (row 4) -> Gater
    addFullBlock (p, 4);

    // Default 4 lanes: row 4 hidden -> pass-through everywhere.
    auto out = render (p, 1.0);
    CHECK (rmsOf (out, 0.55, 0.95) > 0.5f);

    setParam (p, "numlanes", 5.0f);       // row 4 shown -> it gates
    out = render (p, 1.0);
    CHECK (rmsOf (out, 0.55, 0.95) < 0.01f);
    std::printf ("lane count: hidden row bypassed, shown row processes.\n");
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
    setParam (p, "numlanes", 8.0f);    // show every lane in the snapshot
    setParam (p, "numsteps", 32.0f);   // > the old 20 px/step limit: must still fit
    setParam (p, "l2_busstart", 1.0f); // four buses -> four lane colours
    setParam (p, "l4_busstart", 1.0f);
    setParam (p, "l6_busstart", 1.0f);
    setParam (p, "busmode", 1.0f);     // show a serial routing in the bus bar
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
    editor->setSize (1050, 650);
    const juce::File base (path);
    writePng (*editor, base);

    // The two most complex effect panels, standalone.
    fxme::FxmeLookAndFeel lnf;
    for (auto [type, name] : { std::pair { mng::EffectType::Gater, "gater" },
                               std::pair { mng::EffectType::FilterEnv, "filter" } })
    {
        mng::EffectPanel panel (p.apvts, 0, type, mng::theme::busColour (0));
        panel.setLookAndFeel (&lnf);
        panel.setSize (300, 334);   // the editor's real panel-area size
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
    testRingMod();
    testReverser();
    testFreeze();
    testBuses();
    testAuxSend();
    testPanner();
    testBusModes();
    testEffectMix();
    testConfigBank();
    testStateRoundTrip();
    testHostSync();
    testLiveWeightChange();
    testGateCurves();
    testAttRelSharing();
    testLaneCount();
    testMuteSolo();
    testPassRepeatability();
    testLoopJumpReenter();

    if (failures == 0)
        std::printf ("RenderTest: all checks passed.\n");
    return failures == 0 ? 0 : 1;
}
