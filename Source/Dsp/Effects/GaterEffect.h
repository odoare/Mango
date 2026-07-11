/*
  ------------------------------------------------------------------------------
    GaterEffect.h

    Rhythmic gate: while the block is active the sound alternates
    open(dur) / closed(dur) / open(dur) ... starting open. The duration is
    drawn at block entry from the lane's probability weights (1/4..1/32 x
    straight/triplet/dotted); attack and release smooth the gate edges, each
    limited to 25% of the gate duration.

    Overrides: dur, att, rel.

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
            juce::NormalisableRange<float> (0.0f, 0.25f, 0.001f), 0.02f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "gate_rel", nameP + "Gate Release",
            juce::NormalisableRange<float> (0.0f, 0.25f, 0.001f), 0.02f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "gate_");
        attParam = apvts.getRawParameterValue (lanePrefix + "gate_att");
        relParam = apvts.getRawParameterValue (lanePrefix + "gate_rel");
    }

    void prepare (double sr, int, int) override { sampleRate = sr; }
    void reset() override                        { pos = 0; }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, (uint64_t) ctx.loopIndex, 0);
        const double beats      = weights.table().drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jmax (0.001f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));

        gateSamples = juce::jmax (1, (int) std::lround (durSec * ctx.sampleRate));

        const float attFrac = juce::jlimit (0.0f, 0.25f, overrideOr (ctx, OvKey::Att, attParam->load()));
        const float relFrac = juce::jlimit (0.0f, 0.25f, overrideOr (ctx, OvKey::Rel, relParam->load()));
        attackSamples  = (int) std::lround (attFrac * (float) gateSamples);
        releaseSamples = (int) std::lround (relFrac * (float) gateSamples);

        pos = 0;   // the sequence always starts with an open gate
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const int numCh = buffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i)
        {
            const int cyclePos = (int) (pos % (int64_t) (2 * gateSamples));
            float gain = 0.0f;
            if (cyclePos < gateSamples)
            {
                const float a = attackSamples > 0
                    ? juce::jmin (1.0f, (float) (cyclePos + 1) / (float) attackSamples) : 1.0f;
                const float r = releaseSamples > 0
                    ? juce::jmin (1.0f, (float) (gateSamples - cyclePos) / (float) releaseSamples) : 1.0f;
                gain = juce::jmin (a, r);
            }
            ++pos;

            if (gain < 1.0f)
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.getWritePointer (ch)[startSample + i] *= gain;
        }
    }

private:
    DurationWeights weights;
    std::atomic<float>* attParam = nullptr;
    std::atomic<float>* relParam = nullptr;

    double  sampleRate     = 44100.0;
    int     gateSamples    = 1;
    int     attackSamples  = 0;
    int     releaseSamples = 0;
    int64_t pos            = 0;
};

} // namespace mng
