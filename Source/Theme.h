/*
  ------------------------------------------------------------------------------
    Theme.h

    Mango colour scheme, after the Spread / AmbiRR2 pattern.

    The identity is the fruit: a yellow -> green -> red ramp (mangoAt), used
    both as a gradient — the line under the top bar, the config bank's eight
    slots — and as three discrete accents that divide the global controls by
    what they do:

        yellow  the mix side: dry/wet, seed, and the preset chrome
        green   the structure: step size / count, lane count, bus routing
        red     the config bank: its button, its panel and its options

    The backdrop is the same ramp taken down to near-black, so the whole
    window sits inside the fruit's colours without competing with them.
    Lane/bus colours are deliberately *outside* this scheme (see busColour):
    they mark identity, not chrome, and must stay told apart from it.

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
    // The mango ramp. Deliberately literals rather than cross-references:
    // inline variables in a header must not depend on each other's
    // initialisation order.
    inline const juce::Colour mangoYellow { 0xfff5c542 };
    inline const juce::Colour mangoGreen  { 0xff86c232 };
    inline const juce::Colour mangoRed    { 0xffe8483c };

    /** The ramp at `t`: 0 = yellow, 0.5 = green, 1 = red. The single source
        of the colour code — anything spreading across the identity (the
        separation line, the eight config slots) samples this. */
    inline juce::Colour mangoAt (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t < 0.5f ? mangoYellow.interpolatedWith (mangoGreen, t * 2.0f)
                        : mangoGreen.interpolatedWith (mangoRed, (t - 0.5f) * 2.0f);
    }

    /** Fills `b` with the ramp running left to right — the 2 px line under
        the top bar, and anything else that wants the full identity. */
    inline void paintMangoRamp (juce::Graphics& g, juce::Rectangle<float> b)
    {
        juce::ColourGradient grad (mangoYellow, b.getTopLeft(),
                                   mangoRed,    b.getTopRight(), false);
        grad.addColour (0.5, mangoGreen);
        g.setGradientFill (grad);
        g.fillRect (b);
    }

    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        // The same ramp taken to near-black: red in the bottom-left corner
        // through green to a warm amber top-right. Saturation is pulled well
        // down — at these brightnesses full saturation reads as coloured
        // noise rather than as a tint.
        auto backdrop = [] (juce::Colour c, float brightness)
        {
            return c.withMultipliedSaturation (0.45f).withBrightness (brightness);
        };

        juce::ColourGradient grad (backdrop (mangoRed, 0.085f), b.getBottomLeft(),
                                   backdrop (mangoYellow, 0.155f), b.getTopRight(), false);
        grad.addColour (0.5, backdrop (mangoGreen, 0.105f));
        g.setGradientFill (grad);
        g.fillRect (b);
    }

    inline const juce::Colour panel     { 0xff262320 };   // warm neutral dark
    inline const juce::Colour panelLine { 0xff453f36 };
    inline const juce::Colour text      { 0xffe0dcd4 };
    inline const juce::Colour dimText   { 0xffa39d92 };

    inline const juce::Colour topBarBg  { 0xff17140f };   // near-black header

    // One accent per effect bus (0..3): a lane's strip, header and control
    // panel are coloured by the bus it currently belongs to (which follows
    // the display order and the per-lane bus-start switches).
    //
    // These stay off the mango ramp on purpose: they identify *content*,
    // and four hues sampled from a three-colour ramp would neither tell
    // each other apart nor separate from the chrome around them.
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

    /** Round "i" help button: accent on the disc, the panel palette on the
        callout so it matches whatever it sits in. */
    inline void styleInfo (fxme::InfoButton& b, juce::Colour a)
    {
        fxme::InfoButton::Colours c;
        c.accent    = a;
        c.text      = text;
        c.panel     = panel;
        c.panelLine = panelLine;
        b.setColours (c);

        // The callout is a modal popup; keep the click from stirring the
        // keyboard focus at all (see fxme::TextEntryFocusFixer's conventions).
        b.setMouseClickGrabsKeyboardFocus (false);
    }

    /** Side of the square info buttons, and the gap after a title. */
    inline constexpr int infoSize = 15;
    inline constexpr int infoGap  = 6;

    inline void styleCombo (juce::ComboBox& c, juce::Colour a)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2b2b2b));
        c.setColour (juce::ComboBox::outlineColourId,    a.darker());
        c.setColour (juce::ComboBox::arrowColourId,      a.brighter (0.3f));
        c.setColour (juce::ComboBox::textColourId,       text);
    }
}
