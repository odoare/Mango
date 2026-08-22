/*
  ------------------------------------------------------------------------------
    EffectTypes.h

    The registry of Mango lane-effect types. To add a new effect: write the
    EffectBase subclass, then extend the enum, the display names, and the
    three switch helpers in MangoEngine.cpp / EffectPanel.h — everything else
    (parameters, panels, processing order) picks it up from here.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace mng
{

enum class EffectType { Gater = 0, Grain, Delay, Distortion, FilterEnv, Quantizer, RingMod, Reverser, Freeze, AuxSend, Panner };

inline constexpr int kNumEffectTypes = 11;

inline const juce::StringArray& effectTypeNames()
{
    static const juce::StringArray names { "Gater", "Grain", "Delay", "Dist", "Filter", "Quant", "Ring", "Rev", "Freeze", "Aux", "Pan" };
    return names;
}

} // namespace mng
