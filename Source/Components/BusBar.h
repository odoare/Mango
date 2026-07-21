/*
  ------------------------------------------------------------------------------
    BusBar.h

    The bus routing strip under the lane rack: the lane-count −/+ chooser
    stacked above a button cycling the `busmode` configurations, a
    small diagram of the active buses in the current routing (numbered
    boxes in the bus colours, with feed lines in the serial modes), and a
    wet + pan knob pair per active bus. The diagram shows the *effective*
    routing — a mode that needs more buses than currently exist falls back
    to parallel, exactly like the engine (mng::effectiveBusMode).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Theme.h"

namespace mng
{

class BusBar : public juce::Component,
               private juce::Timer
{
public:
    explicit BusBar (MangoAudioProcessor& p) : processor (p)
    {
        // Lane count - / + (drives the numlanes parameter; the rack follows).
        lanesCount.setJustificationType (juce::Justification::centred);
        lanesCount.setColour (juce::Label::textColourId, theme::text);
        lanesCount.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        addAndMakeVisible (lanesCount);
        for (auto* b : { &lanesMinus, &lanesPlus })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2b2b));
            b->setColour (juce::TextButton::textColourOffId, theme::mangoGreen.brighter (0.4f));
            addAndMakeVisible (*b);
        }
        lanesMinus.onClick = [this] { adjustLaneCount (-1); };
        lanesPlus.onClick  = [this] { adjustLaneCount (+1); };

        modeButton.setMouseClickGrabsKeyboardFocus (false);
        modeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2b2b));
        modeButton.setColour (juce::TextButton::textColourOffId, theme::mangoGreen.brighter (0.4f));
        modeButton.onClick = [this]
        {
            if (auto* param = processor.apvts.getParameter (pid::busmode))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (
                    param->convertTo0to1 ((float) ((chosenMode + 1) % numBusModes)));
                param->endChangeGesture();
            }
        };
        addAndMakeVisible (modeButton);

        for (int b = 0; b < numBuses; ++b)
        {
            auto& grp = groups[(size_t) b];
            theme::styleKnob (grp.wet, "Wet", theme::busColour (b));
            theme::styleKnob (grp.pan, "Pan", theme::busColour (b), true);
            addAndMakeVisible (grp.wet);
            addAndMakeVisible (grp.pan);
            grp.wetAtt = std::make_unique<SliderAttachment> (processor.apvts, pid::busWet (b), grp.wet);
            grp.panAtt = std::make_unique<SliderAttachment> (processor.apvts, pid::busPan (b), grp.pan);
        }

        refresh();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        g.setColour (theme::dimText);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("lanes", lanesCaptionArea, juce::Justification::centred);

        paintDiagram (g, diagramArea.toFloat());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);

        // Left column: lanes chooser above the routing-mode button.
        auto ctrl = r.removeFromLeft (78);
        lanesCaptionArea = ctrl.removeFromTop (12);
        auto lanesRow = ctrl.removeFromTop (20);
        lanesMinus.setBounds (lanesRow.removeFromLeft (24));
        lanesPlus.setBounds (lanesRow.removeFromRight (24));
        lanesCount.setBounds (lanesRow);
        ctrl.removeFromTop (4);
        modeButton.setBounds (ctrl.removeFromTop (20));

        r.removeFromLeft (8);
        diagramArea = r.removeFromLeft (140);
        r.removeFromLeft (8);

        for (auto& grp : groups)
        {
            auto slot = r.removeFromLeft (108);
            grp.wet.setBounds (slot.removeFromLeft (52));
            grp.pan.setBounds (slot.removeFromLeft (52));
            r.removeFromLeft (4);
        }
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override { refresh(); }

    void adjustLaneCount (int delta)
    {
        if (auto* param = processor.apvts.getParameter (pid::numlanes))
        {
            const int current = (int) processor.apvts.getRawParameterValue (pid::numlanes)->load();
            const int wanted  = juce::jlimit (1, numLanes, current + delta);
            param->setValueNotifyingHost (param->convertTo0to1 ((float) wanted));
        }
    }

    void refresh()
    {
        // Lane-count readout (buttons, automation, state load).
        const int lanes = processor.engine.visibleLaneCount();
        if (lanes != shownLanes)
        {
            shownLanes = lanes;
            lanesCount.setText (juce::String (lanes), juce::dontSendNotification);
            lanesMinus.setEnabled (lanes > 1);
            lanesPlus.setEnabled (lanes < numLanes);
        }

        const int count = processor.engine.busCount();
        const int mode  = (int) processor.apvts.getRawParameterValue (pid::busmode)->load();
        if (count == busCount && mode == chosenMode)
            return;

        busCount   = count;
        chosenMode = mode;
        static const char* names[numBusModes] = { "parallel", "3 < 1+2", "4 < 1-3",
                                                 "1>2,3>4", "1>2,3,4" };
        modeButton.setButtonText (names[juce::jlimit (0, numBusModes - 1, chosenMode)]);
        for (int b = 0; b < numBuses; ++b)
        {
            groups[(size_t) b].wet.setVisible (b < busCount);
            groups[(size_t) b].pan.setVisible (b < busCount);
        }
        repaint();
    }

    //==========================================================================
    void paintBox (juce::Graphics& g, juce::Point<float> centre, int bus) const
    {
        const auto r = juce::Rectangle<float> (kBox, kBox).withCentre (centre);
        g.setColour (theme::busColour (bus));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (juce::String (bus + 1), r, juce::Justification::centred);
    }

    /** A routing drawn as rows of boxes: every box in a row feeds every box
        in the row below it. That one rule covers all the topologies —
        parallel is a single row, the diamond is three — leaving only mode
        1's unconnected bus 4 as a special case (`parallelBus`, parked at
        the end of the top row with no feed line). */
    struct Layout
    {
        int rows = 0;
        int count[3] {};
        int bus[3][numBuses] {};
        int parallelBus = -1;
    };

    static Layout layoutFor (int effective, int busCount)
    {
        Layout l;
        auto row = [&l] (std::initializer_list<int> buses)
        {
            const int r = l.rows++;
            for (int b : buses)
                l.bus[r][l.count[r]++] = b;
        };

        switch (effective)
        {
            case 1:
                row ({ 0, 1 });
                row ({ 2 });
                if (busCount >= 4)
                    l.parallelBus = 3;
                break;

            case 2:
                row ({ 0, 1, 2 });
                row ({ 3 });
                break;

            case 3:
                row ({ 0 });
                row ({ 1, 2 });
                row ({ 3 });
                break;

            case 4:
            {
                row ({ 0 });
                const int r = l.rows++;
                for (int b = 1; b < busCount; ++b)
                    l.bus[r][l.count[r]++] = b;
                break;
            }

            default:
            {
                const int r = l.rows++;
                for (int b = 0; b < busCount; ++b)
                    l.bus[r][l.count[r]++] = b;
                break;
            }
        }
        return l;
    }

    void paintDiagram (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto l = layoutFor (effectiveBusMode (chosenMode, busCount), busCount);
        if (l.rows < 1)
            return;

        const float cx   = area.getCentreX();
        const float yTop = area.getY() + kBox / 2 + 2.0f;
        const float yBot = area.getBottom() - kBox / 2 - 2.0f;

        auto rowY = [&] (int r)
        {
            return l.rows < 2 ? area.getCentreY()
                              : yTop + (yBot - yTop) * (float) r / (float) (l.rows - 1);
        };

        // Row 0 also carries mode 1's unconnected bus, so it is laid out (and
        // centred) as one box wider than it has feeders.
        auto boxesInRow = [&] (int r) { return l.count[r] + (r == 0 && l.parallelBus >= 0 ? 1 : 0); };

        auto centreOf = [&] (int r, int i)
        {
            const float w = (float) boxesInRow (r) * kStep - (kStep - kBox);
            return juce::Point<float> (cx - w / 2 + kBox / 2 + (float) i * kStep, rowY (r));
        };

        // Feed lines first, so the boxes sit on top of them.
        g.setColour (theme::dimText);
        for (int r = 0; r + 1 < l.rows; ++r)
            for (int i = 0; i < l.count[r]; ++i)
                for (int j = 0; j < l.count[r + 1]; ++j)
                    g.drawLine ({ centreOf (r, i).translated (0.0f, kBox / 2),
                                  centreOf (r + 1, j).translated (0.0f, -kBox / 2) }, 1.2f);

        for (int r = 0; r < l.rows; ++r)
            for (int i = 0; i < l.count[r]; ++i)
                paintBox (g, centreOf (r, i), l.bus[r][i]);

        if (l.parallelBus >= 0)
            paintBox (g, centreOf (0, l.count[0]), l.parallelBus);
    }

    struct Group
    {
        fxme::FxmeSlider wet, pan;
        std::unique_ptr<SliderAttachment> wetAtt, panAtt;
    };

    static constexpr float kBox = 16.0f, kStep = 26.0f;

    MangoAudioProcessor& processor;
    juce::TextButton modeButton;
    juce::TextButton lanesMinus { "-" }, lanesPlus { "+" };
    juce::Label lanesCount;
    std::array<Group, numBuses> groups;
    juce::Rectangle<int> diagramArea, lanesCaptionArea;
    int busCount = 0, chosenMode = -1, shownLanes = 0;   // force the first refresh()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusBar)
};

} // namespace mng
