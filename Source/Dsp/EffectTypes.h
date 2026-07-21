/*
  ------------------------------------------------------------------------------
    EffectTypes.h

    The registry of Mango lane-effect types. To add a new effect: write the
    EffectBase subclass, then extend the enum, the display names, and the
    three switch helpers in MangoEngine.cpp / EffectPanel.h — everything else
    (parameters, panels, processing order) picks it up from here.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace mng
{

enum class EffectType { Gater = 0, Grain, Delay, Distortion, FilterEnv, Quantizer, RingMod, Reverser, Freeze, AuxSend };

inline constexpr int kNumEffectTypes = 10;

inline const juce::StringArray& effectTypeNames()
{
    static const juce::StringArray names { "Gater", "Grain", "Delay", "Dist", "Filter", "Quant", "Ring", "Rev", "Freeze", "Aux" };
    return names;
}

} // namespace mng
