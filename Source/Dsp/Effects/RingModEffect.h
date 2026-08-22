/*
  ------------------------------------------------------------------------------
    RingModEffect.h

    Ring modulator: multiplies the signal by a sine carrier whose frequency
    glides (exponentially) from the start to the end frequency over a
    tempo-synced ramp — drawn at block entry from the lane's probability
    weights (gater logic) — and the glide repeats at that rate for the
    whole block. Amount blends between clean (0) and full ring modulation
    by the ±1 carrier (1); in between it is a partial AM, so low carrier
    frequencies give tremolo. The carrier phase restarts at block entry so
    every pass sounds the same; one carrier feeds all channels.

    Start Freq and End Freq each have a paired *std* knob: every block draws
    its actual value around the knob's mean by a Gaussian (gaussianDraw(),
    EffectBase.h) scaled by the std (0 = every block identical, the
    default), clamped back to [kMinFreq, kMaxFreq].

    Overrides: dur (glide ramp), f0, f1, amp.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

class RingModEffect : public EffectBase
{
public:
    static constexpr int   kControlInterval = 32;
    static constexpr float kMinFreq = 0.5f, kMaxFreq = 10000.0f;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "ring_", nameP + "Ring");

        auto freqRange = juce::NormalisableRange<float> (kMinFreq, kMaxFreq, 0.01f);
        freqRange.setSkewForCentre (150.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "ring_f0", nameP + "Ring Start Freq", freqRange, 100.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "ring_f0std", nameP + "Ring Start Freq Std",
            juce::NormalisableRange<float> (0.0f, 2000.0f, 1.0f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "ring_f1", nameP + "Ring End Freq", freqRange, 800.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "ring_f1std", nameP + "Ring End Freq Std",
            juce::NormalisableRange<float> (0.0f, 2000.0f, 1.0f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "ring_amp", nameP + "Ring Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "ring_");
        f0Param    = apvts.getRawParameterValue (lanePrefix + "ring_f0");
        f0StdParam = apvts.getRawParameterValue (lanePrefix + "ring_f0std");
        f1Param    = apvts.getRawParameterValue (lanePrefix + "ring_f1");
        f1StdParam = apvts.getRawParameterValue (lanePrefix + "ring_f1std");
        ampParam   = apvts.getRawParameterValue (lanePrefix + "ring_amp");
    }

    void prepare (double sr, int, int) override { sampleRate = sr; }

    void reset() override
    {
        phase = 0.0;
        pos   = 0;
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  rampSec    = juce::jmax (0.001f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));
        rampSamples = juce::jmax (kControlInterval, (int) std::lround (rampSec * ctx.sampleRate));

        // Each block draws its own start/end frequency around the knob's
        // mean, spread by the paired std knob (0 = deterministic).
        f0  = gaussianDraw (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId, 1,
                            overrideOr (ctx, OvKey::F0, f0Param->load()), f0StdParam->load(),
                            kMinFreq, kMaxFreq);
        f1  = gaussianDraw (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId, 3,
                            overrideOr (ctx, OvKey::F1, f1Param->load()), f1StdParam->load(),
                            kMinFreq, kMaxFreq);
        amp = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Amp, ampParam->load()));

        sinceControl = kControlInterval;   // recompute the increment on entry
        if (! ctx.isReEnter)               // parameter refresh keeps carrier + ramp phase
            reset();
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const int numCh = buffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            if (sinceControl >= kControlInterval)
            {
                sinceControl = 0;
                const float t    = (float) (pos % (int64_t) rampSamples) / (float) rampSamples;
                const float freq = f0 * std::pow (f1 / f0, t);
                phaseInc = juce::MathConstants<double>::twoPi * freq / sampleRate;
            }

            const float carrier = (float) std::sin (phase);
            const float m = 1.0f - amp + amp * carrier;
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[startSample + i] *= m;

            phase += phaseInc;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            ++pos;
            ++sinceControl;
        }
    }

private:
    DurationWeights weights;
    std::atomic<float>* f0Param    = nullptr;
    std::atomic<float>* f0StdParam = nullptr;
    std::atomic<float>* f1Param    = nullptr;
    std::atomic<float>* f1StdParam = nullptr;
    std::atomic<float>* ampParam   = nullptr;

    double  sampleRate = 44100.0;
    float   f0 = 100.0f, f1 = 800.0f, amp = 1.0f;
    int     rampSamples = 1;
    int     sinceControl = 0;
    double  phase = 0.0, phaseInc = 0.0;
    int64_t pos = 0;
};

} // namespace mng
