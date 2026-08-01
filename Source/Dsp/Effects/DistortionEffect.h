/*
  ------------------------------------------------------------------------------
    DistortionEffect.h

    Tube-style saturation (fxme::Saturator per channel): four models
    (Standard, Dynamic, Triode, Class AB), drive, bias, supply sag, an
    output (makeup) gain in dB on the saturated signal — saturation
    loudness depends on the input level, so compensation is manual — and
    an internal dry/wet mix (the gain sits before the mix, so it only
    scales the wet part).

    Overrides: model, drive, bias, sag, gain (dB), mix.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"

namespace mng
{

class DistortionEffect : public EffectBase
{
public:
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            lanePrefix + "dist_model", nameP + "Dist Model",
            juce::StringArray { "Standard", "Dynamic", "Triode", "Class AB" }, 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dist_drive", nameP + "Dist Drive",
            juce::NormalisableRange<float> (0.0f, 40.0f, 0.1f), 12.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dist_bias", nameP + "Dist Bias",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dist_sag", nameP + "Dist Sag",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dist_gain", nameP + "Dist Out Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "dist_mix", nameP + "Dist Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        modelParam = apvts.getRawParameterValue (lanePrefix + "dist_model");
        driveParam = apvts.getRawParameterValue (lanePrefix + "dist_drive");
        biasParam  = apvts.getRawParameterValue (lanePrefix + "dist_bias");
        sagParam   = apvts.getRawParameterValue (lanePrefix + "dist_sag");
        gainParam  = apvts.getRawParameterValue (lanePrefix + "dist_gain");
        mixParam   = apvts.getRawParameterValue (lanePrefix + "dist_mix");
    }

    void prepare (double sr, int, int numChannels) override
    {
        sats.resize ((size_t) juce::jmax (1, numChannels));
        for (auto& s : sats)
            s.prepare (sr);
    }

    void reset() override
    {
        for (auto& s : sats)
            s.reset();
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        ov = ctx.overrides;
        ovModel = resolve (OvKey::Model, ctx);
        ovDrive = resolve (OvKey::Drive, ctx);
        ovBias  = resolve (OvKey::Bias, ctx);
        ovSag   = resolve (OvKey::Sag, ctx);
        ovGain  = resolve (OvKey::Gain, ctx);
        ovMix   = resolve (OvKey::Mix, ctx);
    }

    void onBlockExit() override { ov = nullptr; }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const int   model = (int) pick (ovModel, modelParam);
        const float drive = pick (ovDrive, driveParam);
        const float bias  = pick (ovBias, biasParam);
        const float sag   = pick (ovSag, sagParam);
        const float mix   = juce::jlimit (0.0f, 1.0f, pick (ovMix, mixParam));
        const float gain  = juce::Decibels::decibelsToGain (
                                juce::jlimit (-24.0f, 24.0f, pick (ovGain, gainParam)));

        const int numCh = juce::jmin (buffer.getNumChannels(), (int) sats.size());
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& s = sats[(size_t) ch];
            s.setModel ((fxme::Saturator::Model) juce::jlimit (0, 3, model));
            s.setDriveDb (drive);
            s.setBias (bias);
            s.setSag (sag);

            float* data = buffer.getWritePointer (ch) + startSample;
            for (int i = 0; i < numSamples; ++i)
                data[i] = mix * gain * s.processSample (data[i]) + (1.0f - mix) * data[i];
        }
    }

private:
    struct Resolved { bool set = false; float value = 0.0f; };

    Resolved resolve (OvKey k, const BlockContext& ctx) const
    {
        if (ctx.overrides != nullptr)
            if (const auto* e = ctx.overrides->find (k))
                return { true, e->eval (ctx.midi) };
        return {};
    }

    static float pick (const Resolved& r, const std::atomic<float>* p)
    {
        return r.set ? r.value : (p != nullptr ? p->load() : 0.0f);
    }

    std::atomic<float>* modelParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* biasParam  = nullptr;
    std::atomic<float>* sagParam   = nullptr;
    std::atomic<float>* gainParam  = nullptr;
    std::atomic<float>* mixParam   = nullptr;

    std::vector<fxme::Saturator> sats;
    const ParsedOverrides* ov = nullptr;
    Resolved ovModel, ovDrive, ovBias, ovSag, ovGain, ovMix;
};

} // namespace mng
