/*
  ------------------------------------------------------------------------------
    GaterEffect.h

    Rhythmic gate: while the block is active the sound alternates
    open(dur) / closed(dur) / open(dur) ... starting open. The duration is
    drawn at block entry from the lane's probability weights (1/4..1/32 x
    straight/triplet/dotted). Attack and release lengths are fractions
    (0..1) of the open phase; if their sum exceeds 1 they share it
    proportionally to their values (att=1, rel=0.5 -> 2/3 and 1/3), so the
    envelope can become a full attack/release triangle with no sustain.

    The attack/release *curve* parameters (0..1) shape those edges: 0.5 is
    the linear ramp, 0 is slow (the ramp lingers near silence / near full
    level before moving), 1 is very fast — a fast release looks like an
    exponential decay. Implemented as gain = ramp^gamma with
    gamma = kCurveRange^(±(1 - 2*curve)).

    Each of the four envelope knobs (att, rel, attcurve, relcurve) has a
    paired *std* knob: every block draws its actual value around the knob's
    mean by a Gaussian (gaussianFraction(), EffectBase.h) scaled by the std
    (0 = every block identical, the default), clamped back to a valid 0..1
    fraction. So a rack of Gater lanes doesn't chop with an identical
    envelope on every pass.

    Overrides: dur, att, rel, attcurve, relcurve, mix.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

class GaterEffect : public EffectBase
{
public:
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "gate_", nameP + "Gate");
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_att", nameP + "Gate Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.02f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_attstd", nameP + "Gate Attack Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_rel", nameP + "Gate Release",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.02f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_relstd", nameP + "Gate Release Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_attcurve", nameP + "Gate Attack Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_attcurvestd", nameP + "Gate Attack Curve Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_relcurve", nameP + "Gate Release Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_relcurvestd", nameP + "Gate Release Curve Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_mix", nameP + "Gate Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "gate_");
        attParam         = apvts.getRawParameterValue (lanePrefix + "gate_att");
        attStdParam      = apvts.getRawParameterValue (lanePrefix + "gate_attstd");
        relParam         = apvts.getRawParameterValue (lanePrefix + "gate_rel");
        relStdParam      = apvts.getRawParameterValue (lanePrefix + "gate_relstd");
        attCurveParam    = apvts.getRawParameterValue (lanePrefix + "gate_attcurve");
        attCurveStdParam = apvts.getRawParameterValue (lanePrefix + "gate_attcurvestd");
        relCurveParam    = apvts.getRawParameterValue (lanePrefix + "gate_relcurve");
        relCurveStdParam = apvts.getRawParameterValue (lanePrefix + "gate_relcurvestd");
        mixParam         = apvts.getRawParameterValue (lanePrefix + "gate_mix");
    }

    void prepare (double sr, int, int) override { sampleRate = sr; }
    void reset() override                        { pos = 0; }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jmax (0.001f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));

        gateSamples = juce::jmax (1, (int) std::lround (durSec * ctx.sampleRate));

        // Each block draws its own attack/release around the knob's mean,
        // spread by the paired std knob (0 = deterministic, same as every
        // other effect here).
        float attFrac = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                          1, overrideOr (ctx, OvKey::Att, attParam->load()),
                                          attStdParam->load());
        float relFrac = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                          3, overrideOr (ctx, OvKey::Rel, relParam->load()),
                                          relStdParam->load());
        normaliseAttackRelease (attFrac, relFrac);
        attackSamples  = (int) std::lround (attFrac * (float) gateSamples);
        releaseSamples = (int) std::lround (relFrac * (float) gateSamples);

        // Curve 0..1 -> exponent: 0.5 linear, 0 slow (gamma = kCurveRange),
        // 1 very fast (gamma = 1/kCurveRange). The release exponent applies
        // to the *remaining* ramp, so its mapping is mirrored.
        const float attCurve = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                                 5, overrideOr (ctx, OvKey::AttCurve, attCurveParam->load()),
                                                 attCurveStdParam->load());
        const float relCurve = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                                 7, overrideOr (ctx, OvKey::RelCurve, relCurveParam->load()),
                                                 relCurveStdParam->load());
        attGamma = attackGammaFor (attCurve);
        relGamma = releaseGammaFor (relCurve);

        mixOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix) != nullptr;
        mixValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));

        // The sequence always starts with an open gate; a parameter refresh
        // keeps the running phase and just applies the new timing.
        if (! ctx.isReEnter)
            pos = 0;
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob value unless the block pinned it. mix scales the gate
        // depth: the applied gain is 1 - mix + mix*gain (mix=0 transparent).
        const float mix = mixOverride ? mixValue : mixParam->load();

        const int numCh = buffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i)
        {
            const int cyclePos = (int) (pos % (int64_t) (2 * gateSamples));
            float gain = 0.0f;
            if (cyclePos < gateSamples)
            {
                gain = 1.0f;
                if (attackSamples > 0 && cyclePos < attackSamples)
                    gain = std::pow ((float) (cyclePos + 1) / (float) attackSamples, attGamma);
                if (releaseSamples > 0 && gateSamples - cyclePos < releaseSamples)
                    gain = juce::jmin (gain,
                        std::pow ((float) (gateSamples - cyclePos) / (float) releaseSamples, relGamma));
            }
            ++pos;

            const float eff = 1.0f - mix + mix * gain;
            if (eff < 1.0f)
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.getWritePointer (ch)[startSample + i] *= eff;
        }
    }

private:
    DurationWeights weights;
    std::atomic<float>* attParam         = nullptr;
    std::atomic<float>* attStdParam      = nullptr;
    std::atomic<float>* relParam         = nullptr;
    std::atomic<float>* relStdParam      = nullptr;
    std::atomic<float>* attCurveParam    = nullptr;
    std::atomic<float>* attCurveStdParam = nullptr;
    std::atomic<float>* relCurveParam    = nullptr;
    std::atomic<float>* relCurveStdParam = nullptr;
    std::atomic<float>* mixParam         = nullptr;
    bool  mixOverride = false;
    float mixValue    = 1.0f;

    double  sampleRate     = 44100.0;
    int     gateSamples    = 1;
    int     attackSamples  = 0;
    int     releaseSamples = 0;
    float   attGamma       = 1.0f;
    float   relGamma       = 1.0f;
    int64_t pos            = 0;
};

} // namespace mng
