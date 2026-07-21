/*
  ------------------------------------------------------------------------------
    PannerEffect.h

    Rhythmic panner: the gater's clock, but each step lands on one of three
    stereo positions instead of alternating between two gain values —
    -1 (hard left), 0 (centre), +1 (hard right). The step duration is drawn
    at block entry from the lane's probability weights exactly like the
    gater's open/closed length.

    Four ways of picking the position (`pan_mode`):

      Cycle ->   left, centre, right, repeat — sweeping rightwards, with the
                 jump back to the left at the wrap.
      Cycle <-   the same, mirrored: right, centre, left.
      Cycle <->  left, centre, right, centre — a period of four that turns
                 round instead of jumping, so it never lurches across the
                 image.
      Random     drawn per step from (seed, lane, block, step), the same
                 deterministic hashing as every other draw in Mango, so a
                 pattern pass always repeats its own sequence.

    `pan_glide` is the fraction of a step spent travelling to the new
    position (0 = instant, which clicks on anything but silence — hence the
    small default). `pan_mix` is the usual dry/wet, applied per channel as
    gain = 1-mix+mix*g, so at 0 the lane is transparent.

    The panning law is the balance one used by the buses: a hard position
    silences the far channel and leaves the near one at unity. On a mono
    main bus there is nothing to pan and the effect passes audio through.

    Overrides: dur, mode, glide, mix, and the weight keys.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

/** The three positions, left to right. */
inline constexpr float kPanStates[3] = { -1.0f, 0.0f, 1.0f };

/** Ping-pong visits the centre twice per period, once each way. */
inline constexpr int kPingPongOrder[4] = { 0, 1, 2, 1 };

enum class PanMode { CycleRight = 0, CycleLeft, CyclePingPong, Random };

/** The position step `stepIndex` lands on. Shared by the DSP and the block
    visual so the picture can never drift from the sound (see the invariant
    in doc/architecture.md §10). The `1` coordinate keeps these draws in a
    different hash stream from the block's duration draw, which uses 0. */
inline float panStateAt (PanMode mode, uint64_t seed, int laneIndex, int blockId,
                         int64_t stepIndex)
{
    if (stepIndex < 0)
        stepIndex = 0;

    switch (mode)
    {
        case PanMode::CycleRight:
            return kPanStates[(int) (stepIndex % 3)];
        case PanMode::CycleLeft:
            return kPanStates[2 - (int) (stepIndex % 3)];
        case PanMode::CyclePingPong:
            return kPanStates[kPingPongOrder[(int) (stepIndex % 4)]];
        case PanMode::Random:
            break;
    }

    const float u = fxme::detrand::u01 (seed, (uint64_t) laneIndex, (uint64_t) blockId,
                                        1, (uint64_t) stepIndex);
    return kPanStates[juce::jlimit (0, 2, (int) (u * 3.0f))];
}

class PannerEffect : public EffectBase
{
public:
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "pan_", nameP + "Pan");
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            lanePrefix + "pan_mode", nameP + "Pan Mode",
            juce::StringArray { "Cycle ->", "Cycle <-", "Cycle <->", "Random" }, 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "pan_glide", nameP + "Pan Glide",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.05f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "pan_mix", nameP + "Pan Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "pan_");
        modeParam  = apvts.getRawParameterValue (lanePrefix + "pan_mode");
        glideParam = apvts.getRawParameterValue (lanePrefix + "pan_glide");
        mixParam   = apvts.getRawParameterValue (lanePrefix + "pan_mix");
    }

    void prepare (double sr, int, int) override { sampleRate = sr; }
    void reset() override                        { pos = 0; cachedStep = -1; }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jmax (0.001f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));

        stepSamples = juce::jmax (1, (int) std::lround (durSec * ctx.sampleRate));

        // Draw inputs, fixed for the block.
        seed      = ctx.seed;
        laneIndex = ctx.laneIndex;
        blockId   = ctx.blockId;

        mode = (PanMode) juce::jlimit (0, 3,
            (int) overrideOr (ctx, OvKey::Mode, modeParam->load()));

        glideOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Glide) != nullptr;
        glideValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Glide, glideParam->load()));
        mixOverride   = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix) != nullptr;
        mixValue      = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));

        // A parameter refresh keeps the running position (and so the step
        // the playhead is inside); a real entry restarts the sequence.
        if (! ctx.isReEnter)
            pos = 0;
        cachedStep = -1;   // mode/rate may have changed under the cached states
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const int numCh = buffer.getNumChannels();
        if (numCh < 2)
        {
            pos += numSamples;   // keep the clock running: a mono bus may become stereo
            return;
        }

        const float glide = glideOverride ? glideValue : juce::jlimit (0.0f, 1.0f, glideParam->load());
        const float mix   = mixOverride   ? mixValue   : juce::jlimit (0.0f, 1.0f, mixParam->load());
        const int   glideSamples = (int) std::lround (glide * (float) stepSamples);

        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t step      = pos / stepSamples;
            const int     posInStep = (int) (pos - step * stepSamples);
            ++pos;

            if (step != cachedStep)
            {
                cachedStep = step;
                fromState  = step > 0 ? panStateAt (mode, seed, laneIndex, blockId, step - 1)
                                      : panStateAt (mode, seed, laneIndex, blockId, 0);
                toState    = panStateAt (mode, seed, laneIndex, blockId, step);
            }

            const float t   = glideSamples > 0
                            ? juce::jmin (1.0f, (float) posInStep / (float) glideSamples)
                            : 1.0f;
            const float pan = fromState + (toState - fromState) * t;

            // Balance law, as the buses use: the far channel closes, the
            // near one stays at unity.
            const float gL = pan > 0.0f ? 1.0f - pan : 1.0f;
            const float gR = pan < 0.0f ? 1.0f + pan : 1.0f;

            const int idx = startSample + i;
            left[idx]  *= 1.0f - mix + mix * gL;
            right[idx] *= 1.0f - mix + mix * gR;
        }
    }

private:
    DurationWeights weights;
    std::atomic<float>* modeParam  = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* mixParam   = nullptr;

    bool  glideOverride = false, mixOverride = false;
    float glideValue = 0.05f, mixValue = 1.0f;

    PanMode  mode      = PanMode::CycleRight;
    uint64_t seed      = 0;
    int      laneIndex = 0;
    int      blockId   = -1;

    double  sampleRate  = 44100.0;
    int     stepSamples = 1;
    int64_t pos         = 0;
    int64_t cachedStep  = -1;          // states recomputed only on step changes
    float   fromState   = 0.0f;
    float   toState     = 0.0f;
};

} // namespace mng
