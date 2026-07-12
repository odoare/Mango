/*
  ------------------------------------------------------------------------------
    FilterEnvEffect.h

    Filter with envelope. Three modes:
      - Lowpass / Highpass: the cutoff sweeps (exponentially) from the start
        to the end frequency over a ramp duration, with resonance Q.
      - Formant: two-formant vowel filter sweeping from a start vowel to an
        end vowel (fxme::FormantFilter).
    The ramp duration is drawn at block entry from the lane's probability
    weights (gater logic) and the sweep repeats at that rate for the whole
    block. Coefficients update every 32 samples.

    Overrides: dur (ramp), q, f0, f1, v0, v1 (vowels accept a e i o u), mode.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

class FilterEnvEffect : public EffectBase
{
public:
    enum Mode { Lowpass = 0, Highpass, Formant };
    static constexpr int kControlInterval = 32;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "flt_", nameP + "Filter");

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            lanePrefix + "flt_mode", nameP + "Filter Mode",
            juce::StringArray { "Lowpass", "Highpass", "Formant" }, 0));

        auto qRange = juce::NormalisableRange<float> (0.3f, 12.0f, 0.01f);
        qRange.setSkewForCentre (1.5f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "flt_q", nameP + "Filter Q", qRange, 0.9f));

        auto freqRange = juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f);
        freqRange.setSkewForCentre (800.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "flt_f0", nameP + "Filter Start Freq", freqRange, 200.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "flt_f1", nameP + "Filter End Freq", freqRange, 4000.0f));

        const juce::StringArray vowels { "A", "E", "I", "O", "U" };
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            lanePrefix + "flt_v0", nameP + "Filter Start Vowel", vowels, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            lanePrefix + "flt_v1", nameP + "Filter End Vowel", vowels, 4));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "flt_");
        modeParam = apvts.getRawParameterValue (lanePrefix + "flt_mode");
        qParam    = apvts.getRawParameterValue (lanePrefix + "flt_q");
        f0Param   = apvts.getRawParameterValue (lanePrefix + "flt_f0");
        f1Param   = apvts.getRawParameterValue (lanePrefix + "flt_f1");
        v0Param   = apvts.getRawParameterValue (lanePrefix + "flt_v0");
        v1Param   = apvts.getRawParameterValue (lanePrefix + "flt_v1");
    }

    void prepare (double sr, int, int numChannels) override
    {
        sampleRate = sr;
        const auto n = (size_t) juce::jmax (1, numChannels);
        biquads.resize (n);
        formants.resize (n);
        for (auto& f : formants)
            f.prepare (sr);
    }

    void reset() override
    {
        for (auto& b : biquads)  b.reset();
        for (auto& f : formants) f.reset();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, (uint64_t) ctx.loopIndex, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  rampSec    = juce::jmax (0.001f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));
        rampSamples = juce::jmax (kControlInterval, (int) std::lround (rampSec * ctx.sampleRate));

        mode = juce::jlimit (0, 2, (int) overrideOr (ctx, OvKey::Mode, modeParam->load()));
        q    = juce::jlimit (0.3f, 12.0f, overrideOr (ctx, OvKey::Q, qParam->load()));
        f0   = juce::jlimit (20.0f, 20000.0f, overrideOr (ctx, OvKey::F0, f0Param->load()));
        f1   = juce::jlimit (20.0f, 20000.0f, overrideOr (ctx, OvKey::F1, f1Param->load()));
        v0   = juce::jlimit (0, fxme::FormantFilter::kNumVowels - 1,
                             (int) overrideOr (ctx, OvKey::V0, v0Param->load()));
        v1   = juce::jlimit (0, fxme::FormantFilter::kNumVowels - 1,
                             (int) overrideOr (ctx, OvKey::V1, v1Param->load()));

        sinceControl = kControlInterval;   // force a coefficient update on entry
        if (! ctx.isReEnter)               // parameter refresh keeps the ramp phase
        {
            pos = 0;
            reset();
        }
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), (int) biquads.size());

        for (int i = 0; i < numSamples; ++i)
        {
            if (sinceControl >= kControlInterval)
            {
                sinceControl = 0;
                updateCoefficients();
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* data = buffer.getWritePointer (ch) + startSample;
                data[i] = mode == Formant ? formants[(size_t) ch].processSample (data[i])
                                          : biquads[(size_t) ch].processSample (data[i]);
            }

            ++pos;
            ++sinceControl;
        }
    }

private:
    void updateCoefficients()
    {
        const float t = (float) (pos % (int64_t) rampSamples) / (float) rampSamples;

        if (mode == Formant)
        {
            for (auto& f : formants)
                f.setVowelBlend ((fxme::FormantFilter::Vowel) v0,
                                 (fxme::FormantFilter::Vowel) v1, t);
        }
        else
        {
            const float freq = f0 * std::pow (f1 / f0, t);
            const auto coeffs = mode == Lowpass
                ? fxme::BiquadCoeffs::lowpass (sampleRate, freq, q)
                : fxme::BiquadCoeffs::highpass (sampleRate, freq, q);
            for (auto& b : biquads)
                b.c = coeffs;
        }
    }

    DurationWeights weights;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* qParam    = nullptr;
    std::atomic<float>* f0Param   = nullptr;
    std::atomic<float>* f1Param   = nullptr;
    std::atomic<float>* v0Param   = nullptr;
    std::atomic<float>* v1Param   = nullptr;

    std::vector<fxme::Biquad>        biquads;
    std::vector<fxme::FormantFilter> formants;

    double  sampleRate = 44100.0;
    int     mode = Lowpass, v0 = 0, v1 = 4;
    float   q = 0.9f, f0 = 200.0f, f1 = 4000.0f;
    int     rampSamples = 1;
    int     sinceControl = 0;
    int64_t pos = 0;
};

} // namespace mng
