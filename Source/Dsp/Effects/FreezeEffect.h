/*
  ------------------------------------------------------------------------------
    FreezeEffect.h

    Spectral freeze (fxme::SpectralFreeze — WDL FFT): at block entry the
    effect captures one FFT window (~43 ms) of the incoming audio — passed
    through while it records — then sustains its magnitude spectrum as a
    static, non-periodic wash for the rest of the block (random-phase
    resynthesis). Each channel gets its own phase stream, so the wash is
    wide and diffuse. Blocks shorter than the capture window just pass
    audio through.

    Determinism: the phase streams are keyed on (seed, lane, block,
    channel) with a frame counter that restarts at block entry, so every
    pattern pass and every bounce produces the identical wash; a
    live-tweak re-enter keeps the running wash instead of re-capturing.

    Overrides: mix.

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
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "frz_mix", nameP + "Freeze Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        mixParam = apvts.getRawParameterValue (lanePrefix + "frz_mix");
    }

    void prepare (double, int, int numChannels) override
    {
        freezers.resize ((size_t) juce::jmax (1, numChannels));
        for (auto& f : freezers)
            f.prepare();
    }

    void reset() override
    {
        for (auto& f : freezers)
            f.reset();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        mixOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix) != nullptr;
        mixValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));

        if (! ctx.isReEnter)   // parameter refresh keeps the running wash
        {
            for (size_t ch = 0; ch < freezers.size(); ++ch)
            {
                freezers[ch].setIdentity (ctx.seed,
                                          ((uint64_t) (uint32_t) ctx.laneIndex << 40)
                                        | ((uint64_t) (uint32_t) ctx.blockId << 8)
                                        | (uint64_t) ch);
                freezers[ch].startCapture();
            }
        }
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob value unless the block pinned it.
        const float mix = mixOverride ? mixValue : mixParam->load();

        const int numCh = juce::jmin (buffer.getNumChannels(), (int) freezers.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& f = freezers[(size_t) ch];
            float* data = buffer.getWritePointer (ch) + startSample;
            for (int i = 0; i < numSamples; ++i)
            {
                const float dry = data[i];
                data[i] = dry + mix * (f.processSample (dry) - dry);
            }
        }
    }

private:
    std::atomic<float>* mixParam = nullptr;

    std::vector<fxme::SpectralFreeze> freezers;
    bool  mixOverride = false;
    float mixValue    = 1.0f;
};

} // namespace mng
