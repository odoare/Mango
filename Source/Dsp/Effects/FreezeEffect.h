/*
  ------------------------------------------------------------------------------
    FreezeEffect.h

    Spectral freeze — a thin APVTS adapter around fxme::SpectralFreezeMulti
    (WDL FFT), which holds all the DSP: at block entry the effect captures
    one FFT window (~43 ms) of the incoming audio — passed through while it
    records — then sustains its magnitude spectrum as a static,
    non-periodic wash for the rest of the block (random-phase resynthesis).
    Each channel gets its own phase stream; `width` blends the two wet
    channels back together with an equal-power law (1 = fully
    decorrelated/wide, 0 = mono, constant energy across the sweep). Blocks
    shorter than the capture window just pass audio through.

    Determinism: the phase streams are keyed on (seed, lane, block,
    channel) with a frame counter that restarts at block entry, so every
    pattern pass and every bounce produces the identical wash; a
    live-tweak re-enter keeps the running wash instead of re-capturing.

    Overrides: mix, width.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include <FxmeTools/dsp/SpectralFreeze.h>

namespace mng
{

class FreezeEffect : public EffectBase
{
public:
    static constexpr int kMaxChannels = 8;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "frz_mix", nameP + "Freeze Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "frz_width", nameP + "Freeze Width",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        mixParam   = apvts.getRawParameterValue (lanePrefix + "frz_mix");
        widthParam = apvts.getRawParameterValue (lanePrefix + "frz_width");
    }

    void prepare (double, int, int numChannels) override
    {
        freezer.prepare (juce::jmin (kMaxChannels, juce::jmax (1, numChannels)));
    }

    void reset() override
    {
        freezer.reset();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        mixOverride   = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix)   != nullptr;
        mixValue      = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));
        widthOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Width) != nullptr;
        widthValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Width, widthParam->load()));

        if (! ctx.isReEnter)   // parameter refresh keeps the running wash
        {
            // Low 8 bits stay clear for SpectralFreezeMulti's channel index.
            freezer.setIdentity (ctx.seed,
                                 ((uint64_t) (uint32_t) ctx.laneIndex << 40)
                               | ((uint64_t) (uint32_t) ctx.blockId << 8));
            freezer.startCapture();
        }
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob values unless the block pinned them.
        freezer.setMix (mixOverride ? mixValue : mixParam->load());
        freezer.setWidth (widthOverride ? widthValue : widthParam->load());

        float* ptrs[kMaxChannels];
        const int numCh = juce::jmin (buffer.getNumChannels(), freezer.numChannels(), kMaxChannels);
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[ch] = buffer.getWritePointer (ch) + startSample;

        freezer.process (ptrs, numCh, numSamples);
    }

private:
    std::atomic<float>* mixParam   = nullptr;
    std::atomic<float>* widthParam = nullptr;

    fxme::SpectralFreezeMulti freezer;
    bool  mixOverride = false, widthOverride = false;
    float mixValue = 1.0f, widthValue = 1.0f;
};

} // namespace mng
