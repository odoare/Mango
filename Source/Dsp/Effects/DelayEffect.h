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

    Time, Feedback and Damping each have a paired *std* knob: every block
    draws its actual value around the knob's mean by a Gaussian
    (gaussianDraw()/gaussianFraction(), EffectBase.h) scaled by the std
    (0 = every block identical, the default), clamped back to the
    parameter's own valid range. Unlike the gater-family effects this one
    draws nothing else at block entry (dur/fb/damp aren't taken from a
    weighted table), so it owns draw indices 0-5 for itself.

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
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_durstd", nameP + "Delay Time Std",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        // Skewed so the resonator-grade range (>0.9) keeps knob travel.
        auto fbRange = juce::NormalisableRange<float> (0.0f, kMaxFeedback, 0.001f);
        fbRange.setSkewForCentre (0.6f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_fb", nameP + "Delay Feedback", fbRange, 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_fbstd", nameP + "Delay Feedback Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_damp", nameP + "Delay Damping",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_dampstd", nameP + "Delay Damping Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_porta", nameP + "Delay Portamento",
            juce::NormalisableRange<float> (kMinPortaMs, kMaxPortaMs, 0.1f), 30.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dly_mix", nameP + "Delay Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        durParam    = apvts.getRawParameterValue (lanePrefix + "dly_dur");
        durStdParam = apvts.getRawParameterValue (lanePrefix + "dly_durstd");
        fbParam     = apvts.getRawParameterValue (lanePrefix + "dly_fb");
        fbStdParam  = apvts.getRawParameterValue (lanePrefix + "dly_fbstd");
        dampParam   = apvts.getRawParameterValue (lanePrefix + "dly_damp");
        dampStdParam = apvts.getRawParameterValue (lanePrefix + "dly_dampstd");
        portaParam  = apvts.getRawParameterValue (lanePrefix + "dly_porta");
        mixParam    = apvts.getRawParameterValue (lanePrefix + "dly_mix");
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

        // A std knob > 0 pins a per-block Gaussian draw around the mean,
        // the same way a block-string override pins a value - only when
        // the block doesn't already set it explicitly (the mini-language
        // always wins). At std == 0 (the default) nothing changes here:
        // *Override stays false and process() keeps tracking the knob live,
        // exactly as before. Above 0 the knob stops gliding continuously
        // and instead snaps (still through the portamento smoothing) to a
        // freshly drawn value on every re-entry - a live tweak still
        // re-enters and redraws, so it doesn't feel unresponsive, but the
        // "randomised" character replaces true continuous tracking.
        if (! durOverride && durStdParam->load() > 0.0f)
        {
            durOverride = true;
            durValue = gaussianDraw (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                     0, durParam->load(), durStdParam->load(),
                                     0.001f, kMaxDelaySeconds);
        }
        if (! fbOverride && fbStdParam->load() > 0.0f)
        {
            fbOverride = true;
            fbValue = gaussianDraw (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                    2, fbParam->load(), fbStdParam->load(),
                                    0.0f, kMaxFeedback);
        }
        if (! dampOverride && dampStdParam->load() > 0.0f)
        {
            dampOverride = true;
            dampValue = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                          4, dampParam->load(), dampStdParam->load());
        }
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
    std::atomic<float>* durParam     = nullptr;
    std::atomic<float>* durStdParam  = nullptr;
    std::atomic<float>* fbParam      = nullptr;
    std::atomic<float>* fbStdParam   = nullptr;
    std::atomic<float>* dampParam    = nullptr;
    std::atomic<float>* dampStdParam = nullptr;
    std::atomic<float>* portaParam   = nullptr;
    std::atomic<float>* mixParam     = nullptr;

    std::vector<fxme::DelayLine> delays;
    bool  durOverride = false, fbOverride = false, dampOverride = false,
          portaOverride = false, mixOverride = false;
    float durValue = 0.25f, fbValue = 0.5f, dampValue = 0.0f, portaValue = 30.0f,
          mixValue = 1.0f;
};

} // namespace mng
