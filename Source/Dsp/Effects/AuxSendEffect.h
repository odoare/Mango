/*
  ------------------------------------------------------------------------------
    AuxSendEffect.h

    Rhythmic aux send: the gater's envelope, but instead of cutting the
    signal it *routes* it. While the gate is open the shaped signal is added
    to the plugin's two auxiliary stereo outputs at their own send levels;
    the main path meanwhile passes at a constant `pass` level to whatever
    lane comes next.

    So `pass` is a passthrough, not a mix: at 1 the lane is transparent on
    the main path and the aux gets a copy on top (a classic post-fader
    send), at 0 the block's audio leaves the main chain entirely and only
    the aux outputs hear it. Anything between splits it.

    The gate timing, envelope and curves are exactly the gater's — same
    weighted duration draw at block entry, same attack/release fractions of
    the open phase, same gamma curves — so the two effects read and behave
    identically apart from where the sound goes. The envelope shapes the
    sends only; the main passthrough is a flat gain, so a lane can send in
    rhythm without chopping what it passes on.

    If the host has an aux bus disabled, the engine hands us a nullptr for
    it and that send is silently dropped.

    Overrides: dur, att, rel, attcurve, relcurve, aux1, aux2, pass.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"

namespace mng
{

class AuxSendEffect : public EffectBase
{
public:
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& lanePrefix, const juce::String& nameP)
    {
        DurationWeights::addParameters (params, lanePrefix + "aux_", nameP + "Aux");
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_att", nameP + "Aux Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.02f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_rel", nameP + "Aux Release",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.02f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_attcurve", nameP + "Aux Attack Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_relcurve", nameP + "Aux Release Curve",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_send1", nameP + "Aux 1 Send",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_send2", nameP + "Aux 2 Send",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "aux_pass", nameP + "Aux Passthrough",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        weights.bind (apvts, lanePrefix + "aux_");
        attParam      = apvts.getRawParameterValue (lanePrefix + "aux_att");
        relParam      = apvts.getRawParameterValue (lanePrefix + "aux_rel");
        attCurveParam = apvts.getRawParameterValue (lanePrefix + "aux_attcurve");
        relCurveParam = apvts.getRawParameterValue (lanePrefix + "aux_relcurve");
        send1Param    = apvts.getRawParameterValue (lanePrefix + "aux_send1");
        send2Param    = apvts.getRawParameterValue (lanePrefix + "aux_send2");
        passParam     = apvts.getRawParameterValue (lanePrefix + "aux_pass");
    }

    void prepare (double sr, int, int) override { sampleRate = sr; }
    void reset() override                        { pos = 0; }

    void setAuxBuffers (juce::AudioBuffer<float>* a1, juce::AudioBuffer<float>* a2) override
    {
        aux1 = a1;
        aux2 = a2;
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, 0);
        const double beats      = resolveTable (ctx, weights).drawBeats (u);
        const float  defaultSec = (float) (beats * 60.0 / ctx.bpm);
        const float  durSec     = juce::jmax (0.001f, overrideDurSeconds (ctx, OvKey::Dur, defaultSec));

        gateSamples = juce::jmax (1, (int) std::lround (durSec * ctx.sampleRate));

        float attFrac = overrideOr (ctx, OvKey::Att, attParam->load());
        float relFrac = overrideOr (ctx, OvKey::Rel, relParam->load());
        normaliseAttackRelease (attFrac, relFrac);
        attackSamples  = (int) std::lround (attFrac * (float) gateSamples);
        releaseSamples = (int) std::lround (relFrac * (float) gateSamples);

        attGamma = attackGammaFor (juce::jlimit (0.0f, 1.0f,
            overrideOr (ctx, OvKey::AttCurve, attCurveParam->load())));
        relGamma = releaseGammaFor (juce::jlimit (0.0f, 1.0f,
            overrideOr (ctx, OvKey::RelCurve, relCurveParam->load())));

        // Levels: pinned for the block when the string sets them, otherwise
        // read live from the knobs in process().
        resolveLevel (ctx, OvKey::Aux1, send1Override, send1Value);
        resolveLevel (ctx, OvKey::Aux2, send2Override, send2Value);
        resolveLevel (ctx, OvKey::Pass, passOverride,  passValue);

        if (! ctx.isReEnter)
            pos = 0;   // the cycle always starts with an open gate
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        const float send1 = send1Override ? send1Value : juce::jlimit (0.0f, 1.0f, send1Param->load());
        const float send2 = send2Override ? send2Value : juce::jlimit (0.0f, 1.0f, send2Param->load());
        const float pass  = passOverride  ? passValue  : juce::jlimit (0.0f, 1.0f, passParam->load());

        const int numCh = buffer.getNumChannels();
        const int ch1   = aux1 != nullptr ? juce::jmin (numCh, aux1->getNumChannels()) : 0;
        const int ch2   = aux2 != nullptr ? juce::jmin (numCh, aux2->getNumChannels()) : 0;
        const bool sending = (ch1 > 0 && send1 > 0.0f) || (ch2 > 0 && send2 > 0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            const int   cyclePos = (int) (pos % (int64_t) (2 * gateSamples));
            const float gain     = gateGain (cyclePos);
            ++pos;

            if (sending && gain > 0.0f)
            {
                const int idx = startSample + i;
                for (int ch = 0; ch < ch1; ++ch)
                    aux1->getWritePointer (ch)[idx] += gain * send1 * buffer.getSample (ch, idx);
                for (int ch = 0; ch < ch2; ++ch)
                    aux2->getWritePointer (ch)[idx] += gain * send2 * buffer.getSample (ch, idx);
            }
        }

        // The main path is a flat level: the send is rhythmic, what the
        // lane passes on is not.
        if (pass < 1.0f)
            for (int ch = 0; ch < numCh; ++ch)
                buffer.applyGain (ch, startSample, numSamples, pass);
    }

private:
    /** The gater envelope: full inside the open phase, shaped at its edges. */
    float gateGain (int cyclePos) const
    {
        if (cyclePos >= gateSamples)
            return 0.0f;

        float gain = 1.0f;
        if (attackSamples > 0 && cyclePos < attackSamples)
            gain = std::pow ((float) (cyclePos + 1) / (float) attackSamples, attGamma);
        if (releaseSamples > 0 && gateSamples - cyclePos < releaseSamples)
            gain = juce::jmin (gain,
                std::pow ((float) (gateSamples - cyclePos) / (float) releaseSamples, relGamma));
        return gain;
    }

    void resolveLevel (const BlockContext& ctx, OvKey key, bool& pinned, float& value) const
    {
        pinned = ctx.overrides != nullptr && ctx.overrides->find (key) != nullptr;
        if (pinned)
            value = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, key, 0.0f));
    }

    DurationWeights weights;
    std::atomic<float>* attParam      = nullptr;
    std::atomic<float>* relParam      = nullptr;
    std::atomic<float>* attCurveParam = nullptr;
    std::atomic<float>* relCurveParam = nullptr;
    std::atomic<float>* send1Param    = nullptr;
    std::atomic<float>* send2Param    = nullptr;
    std::atomic<float>* passParam     = nullptr;

    juce::AudioBuffer<float>* aux1 = nullptr;   // set per processBlock by the engine
    juce::AudioBuffer<float>* aux2 = nullptr;

    bool  send1Override = false, send2Override = false, passOverride = false;
    float send1Value = 1.0f, send2Value = 0.0f, passValue = 1.0f;

    double  sampleRate     = 44100.0;
    int     gateSamples    = 1;
    int     attackSamples  = 0;
    int     releaseSamples = 0;
    float   attGamma       = 1.0f;
    float   relGamma       = 1.0f;
    int64_t pos            = 0;
};

} // namespace mng
