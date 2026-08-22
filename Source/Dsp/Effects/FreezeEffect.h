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

    Retrigger: the wash is re-captured on a rhythmic grid drawn from the
    lane's duration weights (default straight 1/4), reusing the same
    (1/4..1/32 x straight/triplet/dotted) machinery as the other effects.
    Each retrigger is seamless (fxme::SpectralFreeze::retrigger keeps the
    wash playing and crossfades the spectrum), so it does not click. A
    retrigger boundary landing at or past the block's end never fires, so a
    freeze block no longer than the drawn interval is a single capture,
    exactly as before retrigger existed.

    Clicks: the capture->wash transition and each retrigger are crossfaded in
    the DSP; the remaining edge is the block *end*, where the wet wash would
    otherwise be cut to dry. The effect fades its mix to 0 over the last few
    ms of the block (using the block length from the context) so the wash
    settles back to dry before the lane goes silent.

    Determinism: the phase streams are keyed on (seed, lane, block,
    channel) with a frame counter that runs across retriggers, and the
    retrigger grid is drawn from (seed, lane, block); every pattern pass and
    every bounce produces the identical wash. A live-tweak re-enter keeps the
    running wash instead of re-capturing.

    Width has a paired *std* knob: every block draws its actual value around
    the knob's mean by a Gaussian (gaussianFraction(), EffectBase.h) scaled
    by the std (0 = every block identical, the default), clamped back to a
    valid 0..1 fraction, the same pin-for-the-block pattern DelayEffect uses
    for its live-tracked parameters (draw index 1; the retrigger-grid draw
    above owns index 0).

    Overrides: mix, width, and the weight keys w4..wdot (retrigger grid).

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include "../EffectBase.h"
#include "../DurationWeights.h"
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
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            lanePrefix + "frz_widthstd", nameP + "Freeze Width Std",
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        // Retrigger grid — defaults to straight 1/4 (DurationWeights' own
        // default), which never subdivides a freeze block of a quarter note
        // or less, so old sessions/presets keep their single-capture sound.
        DurationWeights::addParameters (params, lanePrefix + "frz_", nameP + "Freeze");
    }

    void bindParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& lanePrefix)
    {
        mixParam      = apvts.getRawParameterValue (lanePrefix + "frz_mix");
        widthParam    = apvts.getRawParameterValue (lanePrefix + "frz_width");
        widthStdParam = apvts.getRawParameterValue (lanePrefix + "frz_widthstd");
        weights.bind (apvts, lanePrefix + "frz_");
    }

    void prepare (double sr, int, int numChannels) override
    {
        sampleRate = sr;
        freezer.prepare (juce::jmin (kMaxChannels, juce::jmax (1, numChannels)));
    }

    void reset() override
    {
        freezer.reset();
        pos = 0;
    }

    void onBlockEnter (const BlockContext& ctx) override
    {
        mixOverride   = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Mix)   != nullptr;
        mixValue      = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Mix, mixParam->load()));
        widthOverride = ctx.overrides != nullptr && ctx.overrides->find (OvKey::Width) != nullptr;
        widthValue    = juce::jlimit (0.0f, 1.0f, overrideOr (ctx, OvKey::Width, widthParam->load()));

        // A std knob > 0 pins a per-block Gaussian draw around the mean,
        // the same way a block-string override pins a value (see
        // DelayEffect for the same pattern).
        if (! widthOverride && widthStdParam->load() > 0.0f)
        {
            widthOverride = true;
            widthValue = gaussianFraction (ctx.seed, (uint64_t) ctx.laneIndex, (uint64_t) ctx.blockId,
                                           1, widthParam->load(), widthStdParam->load());
        }

        // Retrigger interval (samples) drawn from the weights, and the block's
        // length for the retrigger grid and the end fade. Both resolved once,
        // at entry, so a live tweak doesn't reshuffle the running wash.
        const float u = fxme::detrand::u01 (ctx.seed, (uint64_t) ctx.laneIndex,
                                            (uint64_t) ctx.blockId, 0);
        const double beats = resolveTable (ctx, weights).drawBeats (u);
        retrigSamples   = juce::jmax (1, (int) std::lround (beats * 60.0 / ctx.bpm * ctx.sampleRate));
        blockLenSamples = ctx.blockLengthSamples;

        // Fade the wash back to dry over the block's last few ms (never more
        // than half of it) so its end doesn't click.
        releaseSamples = blockLenSamples > 0
                       ? juce::jmin ((int) std::lround (0.010 * ctx.sampleRate), blockLenSamples / 2)
                       : 0;

        if (! ctx.isReEnter)   // parameter refresh keeps the running wash
        {
            // Low 8 bits stay clear for SpectralFreezeMulti's channel index.
            freezer.setIdentity (ctx.seed,
                                 ((uint64_t) (uint32_t) ctx.laneIndex << 40)
                               | ((uint64_t) (uint32_t) ctx.blockId << 8));
            freezer.startCapture();
            pos       = 0;
            nextRetrig = (int64_t) retrigSamples;
        }
    }

    void onBlockExit() override {}

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        // Rhythmic re-capture at every grid boundary strictly inside the
        // block (a boundary at/after the end never fires, so a block no
        // longer than the interval stays a single capture).
        while (retrigSamples > 0 && nextRetrig < (int64_t) blockLenSamples
               && pos + numSamples > nextRetrig)
        {
            freezer.retrigger();
            nextRetrig += retrigSamples;
        }

        // Live knob values unless the block pinned them, scaled by the
        // end-of-block release so the wash fades to dry before the lane stops.
        const float base = mixOverride ? mixValue : mixParam->load();
        freezer.setMix (base * releaseGain());
        freezer.setWidth (widthOverride ? widthValue : widthParam->load());

        float* ptrs[kMaxChannels];
        const int numCh = juce::jmin (buffer.getNumChannels(), freezer.numChannels(), kMaxChannels);
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[ch] = buffer.getWritePointer (ch) + startSample;

        freezer.process (ptrs, numCh, numSamples);
        pos += numSamples;
    }

private:
    /** 1 for most of the block, ramping to 0 over its last `releaseSamples`. */
    float releaseGain() const
    {
        if (releaseSamples <= 0 || blockLenSamples <= 0)
            return 1.0f;
        const int64_t fadeStart = (int64_t) blockLenSamples - releaseSamples;
        if (pos < fadeStart)
            return 1.0f;
        return juce::jlimit (0.0f, 1.0f,
                             (float) ((int64_t) blockLenSamples - pos) / (float) releaseSamples);
    }

    std::atomic<float>* mixParam      = nullptr;
    std::atomic<float>* widthParam    = nullptr;
    std::atomic<float>* widthStdParam = nullptr;
    DurationWeights weights;

    fxme::SpectralFreezeMulti freezer;
    bool  mixOverride = false, widthOverride = false;
    float mixValue = 1.0f, widthValue = 1.0f;

    double  sampleRate      = 44100.0;
    int     retrigSamples   = 0;
    int     blockLenSamples = 0;
    int     releaseSamples  = 0;
    int64_t pos             = 0;
    int64_t nextRetrig      = 0;
};

} // namespace mng
