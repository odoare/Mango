/*
  ------------------------------------------------------------------------------
    MeterStrip.h

    The four stereo level meters parked in the top bar, left of the preset
    strip: input, main output, aux 1, aux 2. Each is a labelled pair of
    fxme::VuMeterComponent bars fed from the processor's fxme::VuMeter taps
    by a 20 Hz timer (the bars repaint themselves at 30 Hz).

    The bars are all one colour rather than sampling the identity ramp: on a
    meter, red means clipping, and aux bars that were permanently red would
    read as a fault rather than as a colour code.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Theme.h"

namespace mng
{

class MeterStrip : public juce::Component,
                   private juce::Timer
{
public:
    /** Width to reserve in the top bar (see kGroupWidth below). */
    static constexpr int kNumGroups  = MangoAudioProcessor::kNumMeterTaps;
    static constexpr int kGroupWidth = 28;
    static constexpr int kWidth      = kNumGroups * kGroupWidth;

    explicit MeterStrip (MangoAudioProcessor& p) : processor (p)
    {
        for (auto& bar : bars)
        {
            // -60..0 dBFS with the unity mark at -6: loud enough to see the
            // top of the range without the bars sitting pinned.
            bar.setRange (-60.0f, 0.0f);
            bar.setZeroLevel (-6.0f);
            bar.setMeterColor (theme::mangoGreen);
            addAndMakeVisible (bar);
        }
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        static const char* labels[kNumGroups] = { "IN", "OUT", "AX1", "AX2" };

        g.setColour (theme::dimText);
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        for (int group = 0; group < kNumGroups; ++group)
            g.drawText (labels[group], labelAreaFor (group),
                        juce::Justification::centredTop);
    }

    void resized() override
    {
        for (int group = 0; group < kNumGroups; ++group)
        {
            auto area = groupBounds (group);
            area.removeFromBottom (kLabelHeight);

            // Two bars, centred as a pair inside the group's column.
            const int pairWidth = 2 * kBarWidth + kBarGap;
            auto pair = area.withSizeKeepingCentre (pairWidth, area.getHeight());
            bars[(size_t) (group * 2)].setBounds (pair.removeFromLeft (kBarWidth));
            pair.removeFromLeft (kBarGap);
            bars[(size_t) (group * 2 + 1)].setBounds (pair.removeFromLeft (kBarWidth));
        }
    }

private:
    static constexpr int kLabelHeight = 9;
    static constexpr int kBarWidth    = 6;
    static constexpr int kBarGap      = 3;

    juce::Rectangle<int> groupBounds (int group) const
    {
        const int w = getWidth() / kNumGroups;
        return { group * w, 0, w, getHeight() };
    }

    juce::Rectangle<int> labelAreaFor (int group) const
    {
        return groupBounds (group).removeFromBottom (kLabelHeight);
    }

    void timerCallback() override
    {
        for (int group = 0; group < kNumGroups; ++group)
            for (int ch = 0; ch < 2; ++ch)
                bars[(size_t) (group * 2 + ch)].setValue (
                    processor.meterLevelDb (group, ch));
    }

    MangoAudioProcessor& processor;
    std::array<fxme::VuMeterComponent, kNumGroups * 2> bars;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterStrip)
};

} // namespace mng
