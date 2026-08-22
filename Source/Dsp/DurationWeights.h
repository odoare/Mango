/*
  ------------------------------------------------------------------------------
    DurationWeights.h

    The 5+3 probability-weight parameters (durations 1/4..1/64, modifiers
    straight/triplet/dotted) shared by every effect that draws a random
    rhythmic duration (gater, grain duplicator, filter ramp), and their
    binding to a fxme::WeightedDurationTable.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "EffectBase.h"

namespace mng
{

struct DurationWeights
{
    // The four tables are parallel and must stay that way: every one is sized
    // by fxme::kNumNoteBases / kNumNoteMods rather than deduced, so adding a
    // note value is a compile error anywhere it was not followed through.
    // (A deduced-size copy of baseKeys elsewhere is exactly what broke when
    // 1/64 was added — read out of bounds, no warning.)
    static constexpr const char* baseSuffixes[fxme::kNumNoteBases] = { "w4", "w8", "w16", "w32", "w64" };
    static constexpr const char* baseLabels[fxme::kNumNoteBases]   = { "1/4", "1/8", "1/16", "1/32", "1/64" };
    static constexpr OvKey       baseKeys[fxme::kNumNoteBases]     = { OvKey::W4, OvKey::W8, OvKey::W16,
                                                                       OvKey::W32, OvKey::W64 };
    static constexpr const char* modSuffixes[fxme::kNumNoteMods]   = { "wstr", "wtrip", "wdot" };
    static constexpr const char* modLabels[fxme::kNumNoteMods]     = { "Str", "Trip", "Dot" };
    static constexpr OvKey       modKeys[fxme::kNumNoteMods]       = { OvKey::Wstr, OvKey::Wtrip,
                                                                       OvKey::Wdot };

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

/** Compile-time proof that the key table and the parameter suffixes line up
    entry for entry — `baseKeys[i]` must be the override key literally named
    `baseSuffixes[i]`. Sizing the arrays explicitly stops a short one being
    read out of bounds; this stops a short one being silently zero-filled with
    OvKey::Dur. Adding a note value now fails the build wherever it was
    missed, which is what should have happened when 1/64 went in. */
namespace detail
{
    inline constexpr bool weightTablesAgree()
    {
        for (int b = 0; b < fxme::kNumNoteBases; ++b)
            if (! sameKeyName (keyNames[(int) DurationWeights::baseKeys[b]],
                               DurationWeights::baseSuffixes[b]))
                return false;
        for (int m = 0; m < fxme::kNumNoteMods; ++m)
            if (! sameKeyName (keyNames[(int) DurationWeights::modKeys[m]],
                               DurationWeights::modSuffixes[m]))
                return false;
        return true;
    }
}
static_assert (detail::weightTablesAgree(),
               "DurationWeights: the override keys and the parameter suffixes "
               "disagree — every wN key must match its wN suffix, and both "
               "tables must be filled to kNumNoteBases / kNumNoteMods.");

/** The lane's weight table with the block's w4..wdot overrides applied. */
inline fxme::WeightedDurationTable resolveTable (const BlockContext& ctx,
                                                 const DurationWeights& weights)
{
    auto t = weights.table();
    for (int b = 0; b < fxme::kNumNoteBases; ++b)
        t.baseWeights[b] = overrideOr (ctx, DurationWeights::baseKeys[b], t.baseWeights[b]);
    for (int m = 0; m < fxme::kNumNoteMods; ++m)
        t.modWeights[m] = overrideOr (ctx, DurationWeights::modKeys[m], t.modWeights[m]);
    return t;
}

} // namespace mng
