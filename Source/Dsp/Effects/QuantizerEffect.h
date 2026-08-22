/*
  ------------------------------------------------------------------------------
    QuantizerEffect.h

    Lo-fi: bit-depth reduction (fxme::BitCrusher, 1..24 bits) plus
    sample-rate reduction (fxme::Downsampler — a raw sample & hold, the
    downsample factor is in samples so ÷1 is bit-transparent), with a
    wet/dry mix to soften the destruction.

    Bits and Downsample each have a paired *std* knob: every block draws its
    actual value around the knob's mean by a Gaussian (gaussianDraw(),
    EffectBase.h) scaled by the std (0 = every block identical, the
    default), clamped back to the parameter's own valid range. Draws nothing
    else at block entry, so it owns draw indices 0-3 for itself.

    Overrides: bits, down, mix.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"

namespace mng
{

class QuantizerEffect : public EffectBase
{
public:
    static constexpr float kMaxDownFactor = 64.0f;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        auto bitsRange = juce::NormalisableRange<float> (1.0f, 24.0f, 0.1f);
        bitsRange.setSkewForCentre (6.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "qnt_bits", nameP + "Quantizer Bits", bitsRange, 8.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "qnt_bitsstd", nameP + "Quantizer Bits Std",
            juce::NormalisableRange<float> (0.0f, 8.0f, 0.05f), 0.0f));
        auto downRange = juce::NormalisableRange<float> (1.0f, kMaxDownFactor, 0.1f);
        downRange.setSkewForCentre (8.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "qnt_down", nameP + "Quantizer Downsample", downRange, 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "qnt_downstd", nameP + "Quantizer Downsample Std",
            juce::NormalisableRange<float> (0.0f, 32.0f, 0.1f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "qnt_mix", nameP + "Quantizer Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        bitsParam    = apvts.getRawParameterValue (lanePrefix + "qnt_bits");
        bitsStdParam = apvts.getRawParameterValue (lanePrefix + "qnt_bitsstd");
        downParam    = apvts.getRawParameterValue (lanePrefix + "qnt_down");
        downStdParam = apvts.getRawParameterValue (lanePrefix + "qnt_downstd");
        mixParam     = apvts.getRawParameterValue (lanePrefix + "qnt_mix");
    }

    void prepare (double, int, int numChannels) override
    {
        downsamplers.resize ((size_t) juce::jmax (1, numChannels));
        reset();
    }

    void reset() override
    {
        for (auto& d : downsamplers)
            d.reset();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        bitsOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Bits) != nullptr;
        downOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Down) != nullptr;
        mixOverride  = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix)  != nullptr;
        bitsValue = overrideOr (ctx, OvKey::Bits, bitsParam->load());
        downValue = juce::jlimit (1.0f, kMaxDownFactor, overrideOr (ctx, OvKey::Down, downParam->load()));
        mixValue  = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));

        // A std knob > 0 pins a per-block Gaussian draw around the mean,
        // the same way a block-string override pins a value (see
        // DelayEffect for the same pattern, including the live-tracking
        // tradeoff once a std is non-zero).
        if (! bitsOverride && bitsStdParam->load() > 0.0f)
        {
            bitsOverride = true;
            bitsValue = gaussianDraw (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                      0, bitsParam->load(), bitsStdParam->load(), 1.0f, 24.0f);
        }
        if (! downOverride && downStdParam->load() > 0.0f)
        {
            downOverride = true;
            downValue = gaussianDraw (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                      2, downParam->load(), downStdParam->load(), 1.0f, kMaxDownFactor);
        }

        // Each block latches its first sample, so entries line up across
        // passes; a live-tweak re-enter keeps the running hold phase.
        if (! ctx.isReEnter)
            for (auto& d : downsamplers)
                d.reset();
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob values unless the block pinned them.
        crusher.setBits (bitsOverride ? bitsValue : bitsParam->load());
        const float down = downOverride ? downValue : downParam->load();
        const float mix  = mixOverride  ? mixValue  : mixParam->load();

        const int numCh = juce::jmin (buffer.getNumChannels(), (int) downsamplers.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& ds = downsamplers[(size_t) ch];
            ds.setFactor (down);

            float* data = buffer.getWritePointer (ch) + startSample;
            for (int i = 0; i < numSamples; ++i)
            {
                const float dry = data[i];
                const float wet = crusher.processSample (ds.processSample (dry));
                data[i] = dry + mix * (wet - dry);
            }
        }
    }

private:
    std::atomic<float>* bitsParam    = nullptr;
    std::atomic<float>* bitsStdParam = nullptr;
    std::atomic<float>* downParam    = nullptr;
    std::atomic<float>* downStdParam = nullptr;
    std::atomic<float>* mixParam     = nullptr;

    fxme::BitCrusher crusher;
    std::vector<fxme::Downsampler> downsamplers;
    bool  bitsOverride = false, downOverride = false, mixOverride = false;
    float bitsValue = 8.0f, downValue = 1.0f, mixValue = 1.0f;
};

} // namespace mng
