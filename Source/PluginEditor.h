/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    Mango editor: FX-Mechanics top bar, six sequencer lanes in the centre,
    lane headers (reorder arrows + effect type) on the left, and a right
    column with the global controls, the selected lane's effect panel and the
    selected block's override string.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

//==============================================================================
class MangoAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MangoAudioProcessorEditor (MangoAudioProcessor&);
    ~MangoAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MangoAudioProcessor& processor;

    fxme::FxmeLookAndFeel lnf;

    fxme::TopBar topBar { "Mango", "modular sound glitcher",
                          JucePlugin_VersionString,
                          juce::ImageCache::getFromMemory (BinaryData::logo686_png,
                                                           BinaryData::logo686_pngSize) };

    fxme::FxmeSlider drywetKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> drywetAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoAudioProcessorEditor)
};
