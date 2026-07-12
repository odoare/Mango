/*
  ------------------------------------------------------------------------------
    GrainDupEffect.h

    Grain duplicator: at block entry a grain of the input is recorded and
    then looped for the whole block (fxme::GrainLooper per channel). The
    grain duration is drawn from the lane's probability weights, like the
    gater; `fade` sets the grain-seam crossfade. Each repetition can also be
    shaped by an attack and a release envelope: `att` / `rel` are lengths as
    fractions of the grain, `attcurve` / `relcurve` their shapes (0 slow,
    0.5 linear, 1 very fast; a fast release looks like an exponential
    decay) — same convention as the gater.

    Overrides: dur, fade, att, attcurve, rel, relcurve.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

class GrainDupEffect : public EffectBase
{
public:
    static constexpr float kMaxGrainSeconds = 2.0f;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "grain_", nameP + "Grain");
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_fade", nameP + "Grain Fade",
            juce::NormalisableRange<float> (0.001f, 0.05f, 0.0001f, 0.5f), 0.015f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_att", nameP + "Grain Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_attcurve", nameP + "Grain Attack Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_rel", nameP + "Grain Release",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_relcurve", nameP + "Grain Release Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "grain_");
        fadeParam     = apvts.getRawParameterValue (lanePrefix + "grain_fade");
        attParam      = apvts.getRawParameterValue (lanePrefix + "grain_att");
        attCurveParam = apvts.getRawParameterValue (lanePrefix + "grain_attcurve");
        relParam      = apvts.getRawParameterValue (lanePrefix + "grain_rel");
        relCurveParam = apvts.getRawParameterValue (lanePrefix + "grain_relcurve");
    }

    void prepare (double sr, int, int numChannels) override
    {
        loopers.resize ((size_t) juce::jmax (1, numChannels));
        for (auto& l : loopers)
            l.prepare (sr, kMaxGrainSeconds);
    }

    void reset() override
    {
        for (auto& l : loopers)
            l.stop();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, (uint64_t) ctx.loopIndex, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jlimit (0.005f, kMaxGrainSeconds,
                                                overrideDurSeconds (ctx, OvKey::Dur, defaultSec));
        const float  fade       = juce::jlimit (0.001f, 0.5f,
                                                overrideOr (ctx, OvKey::Fade, fadeParam->load()));

        // Shaped per-repetition attack/release (gater curve convention:
        // 0 slow, 0.5 linear, 1 fast; the release exponent applies to the
        // remaining ramp, so its mapping is mirrored).
        const float attFrac  = juce::jlimit (0.0f, 1.0f,
                                             overrideOr (ctx, OvKey::Att, attParam->load()));
        const float attCurve = juce::jlimit (0.0f, 1.0f,
                                             overrideOr (ctx, OvKey::AttCurve, attCurveParam->load()));
        const float relFrac  = juce::jlimit (0.0f, 1.0f,
                                             overrideOr (ctx, OvKey::Rel, relParam->load()));
        const float relCurve = juce::jlimit (0.0f, 1.0f,
                                             overrideOr (ctx, OvKey::RelCurve, relCurveParam->load()));
        const float attGamma = attackGammaFor (attCurve);
        const float relGamma = releaseGammaFor (relCurve);

        for (auto& l : loopers)
        {
            l.setCrossfade (fade);
            l.setAttack (attFrac, attGamma);
            l.setRelease (relFrac, relGamma);
        }

        // On a parameter refresh keep the looping grain unless its duration
        // actually changed (re-triggering records a fresh grain).
        if (! ctx.isReEnter || std::abs (durSec - lastDurSec) > 1.0e-4f)
        {
            for (auto& l : loopers)
                l.trigger (durSec);
            lastDurSec = durSec;
        }
    }

    void onBlockExit() override
    {
        for (auto& l : loopers)
            l.stop();
    }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), (int) loopers.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch) + startSample;
            loopers[(size_t) ch].process (data, data, numSamples);
        }
    }

private:
    DurationWeights weights;
    std::atomic<float>* fadeParam     = nullptr;
    std::atomic<float>* attParam      = nullptr;
    std::atomic<float>* attCurveParam = nullptr;
    std::atomic<float>* relParam      = nullptr;
    std::atomic<float>* relCurveParam = nullptr;
    std::vector<fxme::GrainLooper> loopers;
    float lastDurSec = -1.0f;
};

} // namespace mng
