/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    Mango editor: FX-Mechanics top bar; the sequencer lanes (header + rubber
    strip) in the centre, in the engine's display order; right column with
    the global controls, the selected lane's effect panel (one pre-built
    panel per lane x type, visibility-switched) and the selected block's
    override string.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "Components/LaneRackComponent.h"
#include "Components/EffectPanel.h"
#include "Components/BlockTextPanel.h"

//==============================================================================
class MangoAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer,
                                  private juce::ChangeListener
{
public:
    explicit MangoAudioProcessorEditor (MangoAudioProcessor&);
    ~MangoAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void selectBlock (int laneIndex, int blockId);
    void refreshVisiblePanel();
    void refreshBlockText();

    MangoAudioProcessor& processor;

    fxme::FxmeLookAndFeel lnf;

    fxme::TopBar topBar { "Mango", "modular sound glitcher",
                          JucePlugin_VersionString,
                          juce::ImageCache::getFromMemory (BinaryData::logo686_png,
                                                           BinaryData::logo686_pngSize) };

    mng::LaneRackComponent rack;

    // Right column: globals.
    fxme::FxmeSlider drywetKnob, seedKnob, stepsKnob;
    juce::ComboBox stepSizeBox;
    juce::Label stepSizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> drywetAtt, seedAtt, stepsAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stepSizeAtt;

    // Lane count: - / + buttons driving the numlanes parameter.
    juce::TextButton lanesMinusButton { "-" }, lanesPlusButton { "+" };
    juce::Label lanesCaption, lanesCountLabel;
    void adjustLaneCount (int delta);

    // Right column: one panel per (lane, effect type).
    std::array<std::array<std::unique_ptr<mng::EffectPanel>, mng::kNumEffectTypes>,
               mng::numLanes> panels;
    juce::Rectangle<int> panelArea;

    mng::BlockTextPanel blockText;

    int selectedLane  = -1;   // lane identity
    int selectedBlock = -1;
    int visibleLane = -1, visibleType = -1;

    // Declared last: fixes keyboard focus for all TextEditors under the editor
    // (including FxmeSlider's right-click value entry).
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoAudioProcessorEditor)
};
