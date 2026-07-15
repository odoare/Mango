/*
  ------------------------------------------------------------------------------
    DelayEffect.h

    Feedback delay, active while the block sounds (the delay buffer persists
    across blocks, so re-entering picks up the previous tail — a deliberate
    glitch flavour). Parameters: duration (seconds), feedback, damping (a
    lowpass in the feedback path, so repeats mellow as they decay) and
    portamento (the glide time of delay-time changes).

    With a short time the delay is a tuned comb: `dur=mididur fb=0.99`
    turns it into a Karplus-Strong style resonator following the last MIDI
    note, damping sets the string's brightness decay, and portamento slurs
    the pitch between notes.

    Overrides: dur (note-value / mididur convention), fb, damp,
    porta (milliseconds), mix.

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
    static constexpr float kMaxFeedback     = 0.999f;
    static constexpr float kMinPortaMs      = 1.0f;
    static constexpr float kMaxPortaMs      = 50.0f;

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        auto durRange = juce::NormalisableRange<float> (0.01f, kMaxDelaySeconds, 0.001f);
        durRange.setSkewForCentre (0.25f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_dur", nameP + "Delay Time", durRange, 0.25f));
        // Skewed so the resonator-grade range (>0.9) keeps knob travel.
        auto fbRange = juce::NormalisableRange<float> (0.0f, kMaxFeedback, 0.001f);
        fbRange.setSkewForCentre (0.6f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_fb", nameP + "Delay Feedback", fbRange, 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_damp", nameP + "Delay Damping",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_porta", nameP + "Delay Portamento",
            juce::NormalisableRange<float> (kMinPortaMs, kMaxPortaMs, 0.1f), 30.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_mix", nameP + "Delay Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        durParam   = apvts.getRawParameterValue (lanePrefix + "dly_dur");
        fbParam    = apvts.getRawParameterValue (lanePrefix + "dly_fb");
        dampParam  = apvts.getRawParameterValue (lanePrefix + "dly_damp");
        portaParam = apvts.getRawParameterValue (lanePrefix + "dly_porta");
        mixParam   = apvts.getRawParameterValue (lanePrefix + "dly_mix");
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
        durOverride   = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Dur)   != nullptr;
        fbOverride    = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Fb)    != nullptr;
        dampOverride  = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Damp)  != nullptr;
        portaOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Porta) != nullptr;
        mixOverride   = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix)   != nullptr;
        mixValue      = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));
        durValue   = juce::jlimit (0.001f, kMaxDelaySeconds,
                                   overrideDurSeconds (ctx, OvKey::Dur, durParam->load()));
        fbValue    = juce::jlimit (0.0f, kMaxFeedback, overrideOr (ctx, OvKey::Fb, fbParam->load()));
        dampValue  = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Damp, dampParam->load()));
        portaValue = juce::jlimit (kMinPortaMs, kMaxPortaMs,
                                   overrideOr (ctx, OvKey::Porta, portaParam->load()));
    }

    void onBlockExit() override {}   // tail persists in the buffer, processing stops

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Live knob values unless the block pinned them.
        const float dur   = durOverride   ? durValue   : durParam->load();
        const float fb    = fbOverride    ? fbValue    : fbParam->load();
        const float damp  = dampOverride  ? dampValue  : dampParam->load();
        const float porta = portaOverride ? portaValue : portaParam->load();
        const float mix   = mixOverride   ? mixValue   : mixParam->load();

        const int numCh = juce::jmin (buffer.getNumChannels(), (int) delays.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& d = delays[(size_t) ch];
            d.setDelaySeconds (dur);
            d.setFeedback (fb);
            d.setDamping (damp);
            d.setSmoothingSeconds (porta * 0.001f);

            float* data = buffer.getWritePointer (ch) + startSample;
            // The delay is additive, so mix simply scales the delayed
            // signal (the line keeps being fed at full level).
            for (int i = 0; i < numSamples; ++i)
                data[i] += mix * d.processSample (data[i]);
        }
    }

private:
    std::atomic<float>* durParam   = nullptr;
    std::atomic<float>* fbParam    = nullptr;
    std::atomic<float>* dampParam  = nullptr;
    std::atomic<float>* portaParam = nullptr;
    std::atomic<float>* mixParam   = nullptr;

    std::vector<fxme::DelayLine> delays;
    bool  durOverride = false, fbOverride = false, dampOverride = false,
          portaOverride = false, mixOverride = false;
    float durValue = 0.25f, fbValue = 0.5f, dampValue = 0.0f, portaValue = 30.0f,
          mixValue = 1.0f;
};

} // namespace mng
