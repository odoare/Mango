/*
  ------------------------------------------------------------------------------
    Theme.h

    Mango colour scheme, after the Spread / AmbiRR2 pattern: dark diagonal
    gradient backdrop, warm coral identity accent, and one accent colour per
    sequencer lane so a lane's strip, header and control panel all share a hue.

    Also centralises the FxmeTools control styling (styleKnob / styleCombo):
    dark disc body, accent on the value arc / outline / pointer.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "ParamIDs.h"

namespace mng::theme
{
    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const auto base = juce::Colour::fromFloatRGBA (0.16f, 0.14f, 0.20f, 1.0f);
        juce::ColourGradient grad (base.darker().darker().darker(), b.getBottomLeft(),
                                   base, b.getTopRight(), false);
        g.setGradientFill (grad);
        g.fillRect (b);
    }

    inline const juce::Colour panel     { 0xff23202c };
    inline const juce::Colour panelLine { 0xff403a4c };
    inline const juce::Colour text      { 0xffd8d8e0 };
    inline const juce::Colour dimText   { 0xff9a9aa8 };

    inline const juce::Colour accent    { 0xffe0784a };   // coral
    inline const juce::Colour topBarBg  { 0xff14101a };   // near-black header

    // Global-control accents.
    inline const juce::Colour drywetAccent { 0xffe0784a };   // coral
    inline const juce::Colour seedAccent   { 0xff9ac93c };   // lime
    inline const juce::Colour gridAccent   { 0xff4cc9f0 };   // cyan

    // One accent per effect bus (0..3): a lane's strip, header and control
    // panel are coloured by the bus it currently belongs to (which follows
    // the display order and the per-lane bus-start switches).
    inline juce::Colour busColour (int busIndex) noexcept
    {
        static const juce::Colour colours[numBuses] = {
            juce::Colour (0xff4cc9f0),   // cyan
            juce::Colour (0xffe0784a),   // coral
            juce::Colour (0xff9ac93c),   // lime
            juce::Colour (0xffd96cd0),   // orchid
        };
        return colours[juce::jlimit (0, numBuses - 1, busIndex)];
    }

    /** Bus colour shaded by the lane's position inside the bus: the bus's
        first lane gets the pure colour, each following lane steps a little
        towards white — stacked lanes stay distinguishable while clearly
        sharing the bus hue. */
    inline juce::Colour busColour (int busIndex, int depthInBus) noexcept
    {
        const float t = juce::jlimit (0.0f, 0.6f, 0.15f * (float) juce::jmax (0, depthInBus));
        return busColour (busIndex).interpolatedWith (juce::Colours::white, t);
    }

    // FxmeTools rotary knob: dark disc, one accent per control on the value
    // arc / outline / pointer; FxmeLookAndFeel draws the value read-out inside
    // the knob and the label (the slider's name) just below it.
    inline void styleKnob (fxme::FxmeSlider& s, const juce::String& name, juce::Colour a,
                           bool bipolar = false)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setName (name);
        s.setShowLabel (true);
        s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff2b2b2b));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, a.darker (1.6f));
        s.setColour (juce::Slider::trackColourId,               a);
        s.setColour (juce::Slider::thumbColourId,               a.brighter (0.4f));
        if (bipolar)
            s.setCentralValue (0.0);
    }

    inline void styleCombo (juce::ComboBox& c, juce::Colour a)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2b2b2b));
        c.setColour (juce::ComboBox::outlineColourId,    a.darker());
        c.setColour (juce::ComboBox::arrowColourId,      a.brighter (0.3f));
        c.setColour (juce::ComboBox::textColourId,       text);
    }
}
