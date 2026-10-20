/*
  ------------------------------------------------------------------------------
    BlockVisual.h

    The picture inside a sequencer block, per effect type, as Mango's lane
    rack draws it: from the lane's parameters, the block's overrides and the
    actual deterministic draws. A free function so that any host of a Mango
    effect (Otomatotamoto's agents) paints the same thing from its own
    parameters, seed and tempo.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Dsp/DurationWeights.h"
#include "../Dsp/EffectTypes.h"
#include "BlockGraphics.h"
#include "../Dsp/Effects/PannerEffect.h"      // panStateAt: the visual follows the DSP
#include "../Dsp/Effects/DelayEffect.h"       // kMaxDelaySeconds / kMaxFeedback
#include "../Dsp/Effects/QuantizerEffect.h"   // kMaxDownFactor
#include "../Dsp/Effects/RingModEffect.h"     // kMinFreq / kMaxFreq

namespace mng
{

/** What a block's picture depends on besides the block itself. */
struct BlockVisualContext
{
    juce::AudioProcessorValueTreeState& apvts;
    juce::String lanePrefix;          // the parameters' prefix, "l3_"
    int laneIndex = 0;                // the lane identity the draws are keyed on
    EffectType type = EffectType::Gater;
    uint64_t seed = 0;                // as the engine draws with it
    double stepBeats = 0.25;
    double bpm = 120.0;
    MidiNoteState midi;
    const ParsedOverrides* overrides = nullptr;   // the block's, nullptr: none or a parse error
};

/** Paints the effect-specific picture inside one block, from the lane's
    parameters, the block's overrides and — for the weight-drawn
    durations — the actual pass-0 deterministic draw. */
inline void paintEffectVisual (juce::Graphics& g, juce::Rectangle<float> r,
                               const BlockVisualContext& c, const fxme::SeqBlock& b)
{
    auto& apvts = c.apvts;
    const auto& prefix = c.lanePrefix;
    const int laneIndex = c.laneIndex;
    auto param = [&] (const char* suffix)
    {
        return apvts.getRawParameterValue (prefix + suffix)->load();
    };

    const double blockBeats = (b.endStep - b.startStep) * c.stepBeats;
    const double bpm        = c.bpm;
    const auto&  midi       = c.midi;
    const ParsedOverrides* ov = c.overrides;

    auto ovOr = [&] (OvKey k, float fallback)
    {
        if (ov)
            if (const auto* e = ov->find (k))
                return e->eval (midi);
        return fallback;
    };
    auto ovDurBeats = [&] (double fallbackBeats)
    {
        if (ov)
            if (const auto* e = ov->find (OvKey::Dur))
                return e->kind != Expr::Const
                     ? (double) e->eval (midi) * bpm / 60.0   // seconds -> beats
                     : (double) e->value * 4.0;                  // whole-note fraction
        return fallbackBeats;
    };

    // The duration this block draws on pass 0 (same hash as the engine).
    auto drawnBeats = [&] (const char* weightPrefix)
    {
        // DurationWeights owns the key tables: a local copy here was left
        // at four entries when 1/64 was added and read out of bounds.
        fxme::WeightedDurationTable t;
        for (int i = 0; i < fxme::kNumNoteBases; ++i)
            t.baseWeights[i] = ovOr (DurationWeights::baseKeys[i],
                apvts.getRawParameterValue (prefix + weightPrefix
                                            + DurationWeights::baseSuffixes[i])->load());
        for (int i = 0; i < fxme::kNumNoteMods; ++i)
            t.modWeights[i] = ovOr (DurationWeights::modKeys[i],
                apvts.getRawParameterValue (prefix + weightPrefix
                                            + DurationWeights::modSuffixes[i])->load());
        const auto seed = c.seed;
        return t.drawBeats (fxme::detrand::u01 (seed, (uint64_t) laneIndex,
                                                (uint64_t) b.id, 0, 0));
    };

    const auto curveColour  = juce::Colours::white.withAlpha (0.85f);
    const auto filterColour = juce::Colour (0xff14101a).withAlpha (0.85f);

    switch (c.type)
    {
        case EffectType::Gater:
        {
            const double dur = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("gate_")));
            const auto seed = c.seed;
            blockgfx::EnvShape s;
            s.cycleBeats = 2.0 * dur;
            s.openBeats  = dur;
            // Same draw indices as GaterEffect::onBlockEnter.
            s.attFrac    = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 1,
                                             ovOr (OvKey::Att, param ("gate_att")),
                                             param ("gate_attstd"));
            s.relFrac    = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 3,
                                             ovOr (OvKey::Rel, param ("gate_rel")),
                                             param ("gate_relstd"));
            normaliseAttackRelease (s.attFrac, s.relFrac);
            const float attCurve = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 5,
                                                     ovOr (OvKey::AttCurve, param ("gate_attcurve")),
                                                     param ("gate_attcurvestd"));
            const float relCurve = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 7,
                                                     ovOr (OvKey::RelCurve, param ("gate_relcurve")),
                                                     param ("gate_relcurvestd"));
            s.attGamma   = attackGammaFor (attCurve);
            s.relGamma   = releaseGammaFor (relCurve);
            blockgfx::paintEnvCurve (g, r, blockBeats, s, curveColour);
            break;
        }

        case EffectType::Grain:
        {
            const double dur = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("grain_")));
            const auto seed = c.seed;
            blockgfx::EnvShape s;
            s.cycleBeats = dur;   // repetitions are contiguous
            s.openBeats  = dur;
            // Same draw indices as GrainDupEffect::onBlockEnter, so the
            // picture matches the actual per-block Gaussian draw, not
            // just the knobs' mean.
            s.attFrac    = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 1,
                                             ovOr (OvKey::Att, param ("grain_att")),
                                             param ("grain_attstd"));
            s.relFrac    = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 3,
                                             ovOr (OvKey::Rel, param ("grain_rel")),
                                             param ("grain_relstd"));
            normaliseAttackRelease (s.attFrac, s.relFrac);
            const float attCurve = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 5,
                                                     ovOr (OvKey::AttCurve, param ("grain_attcurve")),
                                                     param ("grain_attcurvestd"));
            const float relCurve = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 7,
                                                     ovOr (OvKey::RelCurve, param ("grain_relcurve")),
                                                     param ("grain_relcurvestd"));
            s.attGamma   = attackGammaFor (attCurve);
            s.relGamma   = releaseGammaFor (relCurve);
            blockgfx::paintMirroredEnv (g, r, blockBeats, s, curveColour);
            break;
        }

        case EffectType::Delay:
        {
            const auto seed = c.seed;

            // Std knobs pin a Gaussian draw around the mean exactly like
            // a block-string override would (see DelayEffect), so a
            // string override still wins here and the un-overridden
            // path draws with the same indices onBlockEnter uses.
            double delayBeats;
            if (ov && ov->find (OvKey::Dur) != nullptr)
            {
                const auto* e = ov->find (OvKey::Dur);
                delayBeats = e->kind != Expr::Const
                           ? (double) e->eval (midi) * bpm / 60.0
                           : (double) e->value * 4.0;
            }
            else
            {
                const float durSec = gaussianDraw (seed, (uint64_t) laneIndex, (uint64_t) b.id, 0,
                                                    param ("dly_dur"), param ("dly_durstd"),
                                                    0.001f, DelayEffect::kMaxDelaySeconds);
                delayBeats = (double) durSec * bpm / 60.0;
            }

            const float fb = (ov && ov->find (OvKey::Fb) != nullptr)
                            ? ovOr (OvKey::Fb, 0.0f)
                            : gaussianDraw (seed, (uint64_t) laneIndex, (uint64_t) b.id, 2,
                                            param ("dly_fb"), param ("dly_fbstd"),
                                            0.0f, DelayEffect::kMaxFeedback);
            blockgfx::paintDelayLines (g, r, blockBeats, delayBeats, fb, curveColour);
            break;
        }

        case EffectType::Distortion:
            blockgfx::paintDistWave (g, r, ovOr (OvKey::Drive, param ("dist_drive")),
                                     curveColour);
            break;

        case EffectType::FilterEnv:
        {
            const double ramp = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("flt_")));
            const bool formant = (int) ovOr (OvKey::Mode, param ("flt_mode")) == 2;
            const bool rising = formant
                ? ovOr (OvKey::V1, param ("flt_v1")) >= ovOr (OvKey::V0, param ("flt_v0"))
                : ovOr (OvKey::F1, param ("flt_f1")) >= ovOr (OvKey::F0, param ("flt_f0"));
            blockgfx::paintRampCurve (g, r, blockBeats, ramp, rising, filterColour);
            break;
        }

        case EffectType::Quantizer:
        {
            const auto seed = c.seed;
            const float bits = (ov && ov->find (OvKey::Bits) != nullptr)
                              ? ovOr (OvKey::Bits, 0.0f)
                              : gaussianDraw (seed, (uint64_t) laneIndex, (uint64_t) b.id, 0,
                                              param ("qnt_bits"), param ("qnt_bitsstd"),
                                              1.0f, 24.0f);
            const float down = (ov && ov->find (OvKey::Down) != nullptr)
                              ? ovOr (OvKey::Down, 0.0f)
                              : gaussianDraw (seed, (uint64_t) laneIndex, (uint64_t) b.id, 2,
                                              param ("qnt_down"), param ("qnt_downstd"),
                                              1.0f, QuantizerEffect::kMaxDownFactor);
            blockgfx::paintStairsWave (g, r, bits, down, curveColour);
            break;
        }

        case EffectType::RingMod:
        {
            const double ramp = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("ring_")));
            const auto seed = c.seed;
            const float f0 = (ov && ov->find (OvKey::F0) != nullptr)
                            ? ovOr (OvKey::F0, 0.0f)
                            : gaussianDraw (seed, (uint64_t) laneIndex, (uint64_t) b.id, 1,
                                            param ("ring_f0"), param ("ring_f0std"),
                                            RingModEffect::kMinFreq, RingModEffect::kMaxFreq);
            const float f1 = (ov && ov->find (OvKey::F1) != nullptr)
                            ? ovOr (OvKey::F1, 0.0f)
                            : gaussianDraw (seed, (uint64_t) laneIndex, (uint64_t) b.id, 3,
                                            param ("ring_f1"), param ("ring_f1std"),
                                            RingModEffect::kMinFreq, RingModEffect::kMaxFreq);
            const bool rising = f1 >= f0;
            blockgfx::paintChirpWave (g, r, blockBeats, ramp, rising,
                                      ovOr (OvKey::Amp, param ("ring_amp")),
                                      curveColour);
            break;
        }

        case EffectType::Reverser:
        {
            // Falling ramps, one per slice: time running backwards.
            const double slice = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("rev_")));
            blockgfx::paintRampCurve (g, r, blockBeats, slice, false, curveColour);
            break;
        }

        case EffectType::Freeze:
            blockgfx::paintFreezeLines (g, r, ovOr (OvKey::Mix, param ("frz_mix")),
                                        curveColour);
            break;

        case EffectType::AuxSend:
        {
            const double dur = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("aux_")));
            const auto seed = c.seed;
            blockgfx::EnvShape s;
            s.cycleBeats = 2.0 * dur;
            s.openBeats  = dur;
            // Same draw indices as AuxSendEffect::onBlockEnter.
            s.attFrac    = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 1,
                                             ovOr (OvKey::Att, param ("aux_att")),
                                             param ("aux_attstd"));
            s.relFrac    = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 3,
                                             ovOr (OvKey::Rel, param ("aux_rel")),
                                             param ("aux_relstd"));
            normaliseAttackRelease (s.attFrac, s.relFrac);
            const float attCurve = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 5,
                                                     ovOr (OvKey::AttCurve, param ("aux_attcurve")),
                                                     param ("aux_attcurvestd"));
            const float relCurve = gaussianFraction (seed, (uint64_t) laneIndex, (uint64_t) b.id, 7,
                                                     ovOr (OvKey::RelCurve, param ("aux_relcurve")),
                                                     param ("aux_relcurvestd"));
            s.attGamma   = attackGammaFor (attCurve);
            s.relGamma   = releaseGammaFor (relCurve);
            const float send = juce::jmax (ovOr (OvKey::Aux1, param ("aux_send1")),
                                           ovOr (OvKey::Aux2, param ("aux_send2")));
            blockgfx::paintSendEnv (g, r, blockBeats, s, send,
                                    ovOr (OvKey::Pass, param ("aux_pass")), curveColour);
            break;
        }

        case EffectType::Panner:
        {
            const double step = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("pan_")));
            const auto mode = (PanMode) juce::jlimit (0, 3,
                (int) ovOr (OvKey::Mode, param ("pan_mode")));
            const auto seed = c.seed;
            blockgfx::paintPanSteps (g, r, blockBeats, step,
                                     ovOr (OvKey::Glide, param ("pan_glide")),
                                     [&] (int64_t k) { return panStateAt (mode, seed, laneIndex, b.id, k); },
                                     curveColour);
            break;
        }
    }
}

} // namespace mng
