/*
  ------------------------------------------------------------------------------
    GrainDupEffect.h

    Grain duplicator: at block entry a grain of the input is recorded and
    then looped for the whole block (fxme::GrainLooper per channel). The
    grain duration is drawn from the lane's probability weights, like the
    gater; `fade` sets the grain-seam crossfade.

    Overrides: dur, fade.

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
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "grain_");
        fadeParam = apvts.getRawParameterValue (lanePrefix + "grain_fade");
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
        const double beats      = weights.table().drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jlimit (0.005f, kMaxGrainSeconds,
                                                overrideDurSeconds (ctx, OvKey::Dur, defaultSec));
        const float  fade       = juce::jlimit (0.001f, 0.5f,
                                                overrideOr (ctx, OvKey::Fade, fadeParam->load()));

        for (auto& l : loopers)
        {
            l.setCrossfade (fade);
            l.trigger (durSec);
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
    std::atomic<float>* fadeParam = nullptr;
    std::vector<fxme::GrainLooper> loopers;
};

} // namespace mng
