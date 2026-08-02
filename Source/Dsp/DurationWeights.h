/*
  ------------------------------------------------------------------------------
    DurationWeights.h

    The 5+3 probability-weight parameters (durations 1/4..1/64, modifiers
    straight/triplet/dotted) shared by every effect that draws a random
    rhythmic duration (gater, grain duplicator, filter ramp), and their
    binding to a fxme::WeightedDurationTable.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "EffectBase.h"

namespace mng
{

struct DurationWeights
{
    static constexpr const char* baseSuffixes[fxme::kNumNoteBases] = { "w4", "w8", "w16", "w32", "w64" };
    static constexpr const char* baseLabels[fxme::kNumNoteBases]   = { "1/4", "1/8", "1/16", "1/32", "1/64" };
    static constexpr const char* modSuffixes[fxme::kNumNoteMods]   = { "wstr", "wtrip", "wdot" };
    static constexpr const char* modLabels[fxme::kNumNoteMods]     = { "Str", "Trip", "Dot" };

    /** Adds the seven weight parameters with ids `<prefix>w4` .. `<prefix>wdot`
        (prefix e.g. "l3_gate_"). Defaults: only straight quarters. */
    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                               const juce::String& prefix, const juce::String& namePrefix)
    {
        auto weight = [&] (const char* suffix, const char* label, float def)
        {
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                prefix + suffix, namePrefix + " P(" + label + ")",
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), def));
        };
        for (int b = 0; b < fxme::kNumNoteBases; ++b)
            weight (baseSuffixes[b], baseLabels[b], b == 0 ? 1.0f : 0.0f);
        for (int m = 0; m < fxme::kNumNoteMods; ++m)
            weight (modSuffixes[m], modLabels[m], m == 0 ? 1.0f : 0.0f);
    }

    void bind (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
    {
        for (int b = 0; b < fxme::kNumNoteBases; ++b)
            base[b] = apvts.getRawParameterValue (prefix + baseSuffixes[b]);
        for (int m = 0; m < fxme::kNumNoteMods; ++m)
            mod[m] = apvts.getRawParameterValue (prefix + modSuffixes[m]);
    }

    /** Snapshot the current weights (audio thread, lock-free). */
    fxme::WeightedDurationTable table() const
    {
        fxme::WeightedDurationTable t;
        for (int b = 0; b < fxme::kNumNoteBases; ++b)
            t.baseWeights[b] = base[b] != nullptr ? base[b]->load() : 0.0f;
        for (int m = 0; m < fxme::kNumNoteMods; ++m)
            t.modWeights[m] = mod[m] != nullptr ? mod[m]->load() : 0.0f;
        return t;
    }

    std::atomic<float>* base[fxme::kNumNoteBases] = {};
    std::atomic<float>* mod[fxme::kNumNoteMods]   = {};
};

/** The lane's weight table with the block's w4..wdot overrides applied. */
inline fxme::WeightedDurationTable resolveTable (const BlockContext& ctx,
                                                 const DurationWeights& weights)
{
    auto t = weights.table();
    static constexpr OvKey baseKeys[fxme::kNumNoteBases] = { OvKey::W4, OvKey::W8,
                                                             OvKey::W16, OvKey::W32,
                                                             OvKey::W64 };
    static constexpr OvKey modKeys[fxme::kNumNoteMods]   = { OvKey::Wstr, OvKey::Wtrip,
                                                             OvKey::Wdot };
    for (int b = 0; b < fxme::kNumNoteBases; ++b)
        t.baseWeights[b] = overrideOr (ctx, baseKeys[b], t.baseWeights[b]);
    for (int m = 0; m < fxme::kNumNoteMods; ++m)
        t.modWeights[m] = overrideOr (ctx, modKeys[m], t.modWeights[m]);
    return t;
}

} // namespace mng
