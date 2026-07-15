/*
  ------------------------------------------------------------------------------
    ReverserEffect.h

    Reverser: chops the incoming audio into slices of a tempo-synced
    duration — drawn at block entry from the lane's probability weights
    (gater logic) — and plays every slice backwards. While slice k records,
    slice k−1 plays reversed; the first slice of a block passes the input
    through (there is nothing to reverse yet), so blocks always start with
    sound. `fade` is a 0–0.5 fraction of the slice faded in/out at the
    seams (grain convention). The slice grid restarts at block entry so
    every pass sounds the same; a live-tweak re-enter keeps the running
    position. Output samples are exact copies of input samples (pure
    time reversal, no interpolation).

    Overrides: dur (slice, note-value / mididur convention), fade, mix.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

class ReverserEffect : public EffectBase
{
public:
    static constexpr float kMaxSliceSeconds = 4.0f;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "rev_", nameP + "Reverser");

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "rev_fade", nameP + "Reverser Fade",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.02f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "rev_mix", nameP + "Reverser Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "rev_");
        fadeParam = apvts.getRawParameterValue (lanePrefix + "rev_fade");
        mixParam  = apvts.getRawParameterValue (lanePrefix + "rev_mix");
    }

    void prepare (double sr, int, int numChannels) override
    {
        maxSliceSamples = (int) std::ceil (kMaxSliceSeconds * sr);
        channels.resize ((size_t) juce::jmax (1, numChannels));
        for (auto& ch : channels)
        {
            ch.rec.assign ((size_t) maxSliceSamples, 0.0f);
            ch.prv.assign ((size_t) maxSliceSamples, 0.0f);
        }
        reset();
    }

    void reset() override
    {
        idx = 0;
        havePrev = false;
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  sliceSec   = juce::jmax (0.005f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));
        sliceSamples = juce::jlimit (32, maxSliceSamples, (int) std::lround (sliceSec * ctx.sampleRate));

        fadeFrac = juce::jlimit (0.0f, 0.5f, overrideOr (ctx, OvKey::Fade, fadeParam->load()));
        mixOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix) != nullptr;
        mixValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));

        if (! ctx.isReEnter)   // parameter refresh keeps the slice position
            reset();
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob value unless the block pinned it.
        const float mix = mixOverride ? mixValue : mixParam->load();

        const int   numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
        const float fadeN = fadeFrac * (float) sliceSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            if (idx >= sliceSamples)   // seam: the just-recorded slice becomes the source
            {
                for (auto& ch : channels)
                    std::swap (ch.rec, ch.prv);   // vector swap: pointer exchange only
                havePrev = true;
                idx = 0;
            }

            float g = 1.0f;
            if (fadeN >= 1.0f)
                g = juce::jmin (1.0f, (float) (idx + 1) / fadeN,
                                      (float) (sliceSamples - idx) / fadeN);

            for (int c = 0; c < numCh; ++c)
            {
                auto& ch = channels[(size_t) c];
                float* data = buffer.getWritePointer (c) + startSample;
                const float dry = data[i];
                ch.rec[(size_t) idx] = dry;
                const float wet = g * (havePrev ? ch.prv[(size_t) (sliceSamples - 1 - idx)] : dry);
                data[i] = dry + mix * (wet - dry);
            }
            ++idx;
        }
    }

private:
    struct Channel
    {
        std::vector<float> rec, prv;   // recording slice / reversed source
    };

    DurationWeights weights;
    std::atomic<float>* fadeParam = nullptr;
    std::atomic<float>* mixParam  = nullptr;

    std::vector<Channel> channels;
    int    maxSliceSamples = 1, sliceSamples = 32;
    float  fadeFrac = 0.02f;
    int    idx = 0;
    bool   havePrev = false;
    bool   mixOverride = false;
    float  mixValue    = 1.0f;
};

} // namespace mng
