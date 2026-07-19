/*
  ------------------------------------------------------------------------------
    PresetOverlay.h

    Opaque backdrop hosting the full FxmeTools preset browser
    (fxme::PresetComponent). Shown over the right-hand control column when
    the preset toggle in the top bar is switched on, so the browser fully
    covers the panels behind it (same pattern as AmbiRR2).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Theme.h"

namespace mng
{

class PresetOverlay : public juce::Component
{
public:
    explicit PresetOverlay (fxme::PresetManager& manager)
    {
        preset = std::make_unique<fxme::PresetComponent> (manager);
        preset->setAccentColour (theme::globalAccent);
        addAndMakeVisible (*preset);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (theme::globalAccent.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 6.0f, 1.2f);
    }

    void resized() override
    {
        preset->setBounds (getLocalBounds().reduced (6));
    }

private:
    std::unique_ptr<fxme::PresetComponent> preset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetOverlay)
};

} // namespace mng
