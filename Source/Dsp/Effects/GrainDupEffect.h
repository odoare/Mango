/*
  ------------------------------------------------------------------------------
    GrainDupEffect.h

    Grain duplicator: at block entry a grain of the input is recorded and
    then looped for the whole block (fxme::GrainLooper per channel), via
    triggerForPeriod() so the loop period lands exactly on `dur` — plain
    trigger() would come up one crossfade short every cycle, which drifts
    the repeat out of sync with the beat over a long block (GrainLooper.h's
    class comment has the arithmetic). Each individual repetition is shaped
    by an attack and a release envelope (never the block as a whole): `att` /
    `rel` are lengths as fractions (0..1) of the grain, and when their sum
    exceeds 1 they share the grain proportionally to their values (att=1,
    rel=0.5 -> 2/3 and 1/3). `attcurve` / `relcurve` set the shapes (0 slow,
    0.5 linear, 1 very fast; a fast release looks like an exponential decay)
    — same conventions as the gater. `fade` is the seam crossfade itself,
    live-adjustable rather than fixed; a change only takes effect at the next
    re-trigger, same as `dur`, because GrainLooper fixes a grain's crossfade
    when it finishes recording.

    Each of the four envelope knobs (att, rel, attcurve, relcurve) has a
    paired *std* knob: every block draws its actual value around the knob's
    mean by a Gaussian (gaussianFraction(), EffectBase.h) scaled by the std
    (0 = every block identical, the default), clamped back to a valid 0..1
    fraction. So a rack of Grain lanes doesn't repeat with an identical
    envelope on every pass. The draw is keyed on (seed, lane, block), like
    every other per-block draw here.

    Overrides: dur, att, attcurve, rel, relcurve, mix.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
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
    static constexpr float kMaxGrainSeconds    = 2.0f;
    static constexpr float kDefaultFadeSeconds = 0.015f;   // Fade knob's default

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "grain_", nameP + "Grain");
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_att", nameP + "Grain Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_attstd", nameP + "Grain Attack Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_attcurve", nameP + "Grain Attack Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_attcurvestd", nameP + "Grain Attack Curve Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_rel", nameP + "Grain Release",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_relstd", nameP + "Grain Release Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_relcurve", nameP + "Grain Release Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_relcurvestd", nameP + "Grain Release Curve Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_fade", nameP + "Grain Fade",
            juce::NormalisableRange<float> (0.001f, 0.1f, 0.001f), kDefaultFadeSeconds));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "grain_mix", nameP + "Grain Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "grain_");
        attParam         = apvts.getRawParameterValue (lanePrefix + "grain_att");
        attStdParam      = apvts.getRawParameterValue (lanePrefix + "grain_attstd");
        attCurveParam    = apvts.getRawParameterValue (lanePrefix + "grain_attcurve");
        attCurveStdParam = apvts.getRawParameterValue (lanePrefix + "grain_attcurvestd");
        relParam         = apvts.getRawParameterValue (lanePrefix + "grain_rel");
        relStdParam      = apvts.getRawParameterValue (lanePrefix + "grain_relstd");
        relCurveParam    = apvts.getRawParameterValue (lanePrefix + "grain_relcurve");
        relCurveStdParam = apvts.getRawParameterValue (lanePrefix + "grain_relcurvestd");
        fadeParam        = apvts.getRawParameterValue (lanePrefix + "grain_fade");
        mixParam         = apvts.getRawParameterValue (lanePrefix + "grain_mix");
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
                                            (uint64_t) ctx.blockId, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jlimit (0.005f, kMaxGrainSeconds,
                                                overrideDurSeconds (ctx, OvKey::Dur, defaultSec));

        // Shaped per-repetition attack/release (gater conventions: curve
        // 0 slow, 0.5 linear, 1 fast; lengths share the grain
        // proportionally when their sum exceeds 1). Each block draws its
        // own value around the knob's mean, spread by the paired std knob
        // (0 = deterministic, same as every other effect here).
        float attFrac = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                          1, overrideOr (ctx, OvKey::Att, attParam->load()),
                                          attStdParam->load());
        float relFrac = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                          3, overrideOr (ctx, OvKey::Rel, relParam->load()),
                                          relStdParam->load());
        normaliseAttackRelease (attFrac, relFrac);
        const float attCurve = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                                 5, overrideOr (ctx, OvKey::AttCurve, attCurveParam->load()),
                                                 attCurveStdParam->load());
        const float relCurve = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                                 7, overrideOr (ctx, OvKey::RelCurve, relCurveParam->load()),
                                                 relCurveStdParam->load());
        const float attGamma = attackGammaFor (attCurve);
        const float relGamma = releaseGammaFor (relCurve);

        for (auto& l : loopers)
        {
            l.setAttack (attFrac, attGamma);
            l.setRelease (relFrac, relGamma);
        }

        mixOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix) != nullptr;
        mixValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));

        const float fadeSec = fadeParam->load();

        // On a parameter refresh keep the looping grain unless its duration
        // or fade actually changed (re-triggering records a fresh grain;
        // GrainLooper only picks up a new crossfade at that point too).
        if (! ctx.isReEnter || std::abs (durSec - lastDurSec) > 1.0e-4f
                            || std::abs (fadeSec - lastFadeSec) > 1.0e-4f)
        {
            for (auto& l : loopers)
                l.triggerForPeriod (durSec, fadeSec);
            lastDurSec  = durSec;
            lastFadeSec = fadeSec;
        }
    }

    void onBlockExit() override
    {
        for (auto& l : loopers)
            l.stop();
    }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob value unless the block pinned it.
        const float mix = mixOverride ? mixValue : mixParam->load();

        const int numCh = juce::jmin (buffer.getNumChannels(), (int) loopers.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& l = loopers[(size_t) ch];
            float* data = buffer.getWritePointer (ch) + startSample;
            for (int i = 0; i < numSamples; ++i)
            {
                const float dry = data[i];
                data[i] = dry + mix * (l.processSample (dry) - dry);
            }
        }
    }

private:
    DurationWeights weights;
    std::atomic<float>* attParam         = nullptr;
    std::atomic<float>* attStdParam      = nullptr;
    std::atomic<float>* attCurveParam    = nullptr;
    std::atomic<float>* attCurveStdParam = nullptr;
    std::atomic<float>* relParam         = nullptr;
    std::atomic<float>* relStdParam      = nullptr;
    std::atomic<float>* relCurveParam    = nullptr;
    std::atomic<float>* relCurveStdParam = nullptr;
    std::atomic<float>* fadeParam        = nullptr;
    std::atomic<float>* mixParam         = nullptr;
    std::vector<fxme::GrainLooper> loopers;
    float lastDurSec  = -1.0f;
    float lastFadeSec = -1.0f;
    bool  mixOverride = false;
    float mixValue    = 1.0f;
};

} // namespace mng
