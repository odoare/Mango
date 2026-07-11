/*
  ------------------------------------------------------------------------------
    DelayEffect.h

    Feedback delay, active while the block sounds (the delay buffer persists
    across blocks, so re-entering picks up the previous tail — a deliberate
    glitch flavour). Parameters: duration (seconds) and feedback.

    Overrides: dur (note-value / mididur convention), fb.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"

namespace mng
{

class DelayEffect : public EffectBase
{
public:
    static constexpr float kMaxDelaySeconds = 2.0f;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        auto durRange = juce::NormalisableRange<float> (0.01f, kMaxDelaySeconds, 0.001f);
        durRange.setSkewForCentre (0.25f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_dur", nameP + "Delay Time", durRange, 0.25f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_fb", nameP + "Delay Feedback",
            juce::NormalisableRange<float> (0.0f, 0.98f, 0.01f), 0.5f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        durParam = apvts.getRawParameterValue (lanePrefix + "dly_dur");
        fbParam  = apvts.getRawParameterValue (lanePrefix + "dly_fb");
    }

    void prepare (double sr, int, int numChannels) override
    {
        delays.resize ((size_t) juce::jmax (1, numChannels));
        for (auto& d : delays)
            d.prepare (sr, kMaxDelaySeconds);
    }

    void reset() override
    {
        for (auto& d : delays)
            d.reset();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        durOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Dur) != nullptr;
        fbOverride  = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Fb)  != nullptr;
        durValue    = juce::jlimit (0.001f, kMaxDelaySeconds,
                                    overrideDurSeconds (ctx, OvKey::Dur, durParam->load()));
        fbValue     = juce::jlimit (0.0f, 0.98f, overrideOr (ctx, OvKey::Fb, fbParam->load()));
    }

    void onBlockExit() override {}   // tail persists in the buffer, processing stops

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob values unless the block pinned them.
        const float dur = durOverride ? durValue : durParam->load();
        const float fb  = fbOverride  ? fbValue  : fbParam->load();

        const int numCh = juce::jmin (buffer.getNumChannels(), (int) delays.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& d = delays[(size_t) ch];
            d.setDelaySeconds (dur);
            d.setFeedback (fb);

            float* data = buffer.getWritePointer (ch) + startSample;
            for (int i = 0; i < numSamples; ++i)
                data[i] += d.processSample (data[i]);
        }
    }

private:
    std::atomic<float>* durParam = nullptr;
    std::atomic<float>* fbParam  = nullptr;

    std::vector<fxme::DelayLine> delays;
    bool  durOverride = false, fbOverride = false;
    float durValue = 0.25f, fbValue = 0.5f;
};

} // namespace mng
