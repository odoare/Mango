/*
  ------------------------------------------------------------------------------
    QuantizerEffect.h

    Bit-depth reduction (fxme::BitCrusher), 1..24 bits.

    Overrides: bits.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"

namespace mng
{

class QuantizerEffect : public EffectBase
{
public:
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        auto bitsRange = juce::NormalisableRange<float> (1.0f, 24.0f, 0.1f);
        bitsRange.setSkewForCentre (6.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "qnt_bits", nameP + "Quantizer Bits", bitsRange, 8.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        bitsParam = apvts.getRawParameterValue (lanePrefix + "qnt_bits");
    }

    void prepare (double, int, int) override {}
    void reset() override                    {}

    void onBlockEnter (const BlockContext& ctx) override
    {
        bitsOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Bits) != nullptr;
        bitsValue    = overrideOr (ctx, OvKey::Bits, bitsParam->load());
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        crusher.setBits (bitsOverride ? bitsValue : bitsParam->load());

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer (ch) + startSample;
            for (int i = 0; i < numSamples; ++i)
                data[i] = crusher.processSample (data[i]);
        }
    }

private:
    std::atomic<float>* bitsParam = nullptr;
    fxme::BitCrusher crusher;
    bool  bitsOverride = false;
    float bitsValue    = 8.0f;
};

} // namespace mng
