/*
  ------------------------------------------------------------------------------
    PluginEditor.cpp
    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Dsp/OverrideParser.h"

using namespace mng;
using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

//==============================================================================
MangoAudioProcessorEditor::MangoAudioProcessorEditor (MangoAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), rack (p), busBar (p), configPanel (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (topBar);
    addAndMakeVisible (rack);
    addAndMakeVisible (busBar);

    // Presets: compact strip + toggle hosted by the top bar; the full
    // browser sits (hidden) over the right column until toggled.
    presetBar.setAccentColour (theme::globalAccent);
    presetToggle.setClickingTogglesState (true);
    presetToggle.setMouseClickGrabsKeyboardFocus (false);
    presetToggle.setColour (juce::TextButton::buttonColourId,  juce::Colours::black.withAlpha (0.4f));
    presetToggle.setColour (juce::TextButton::buttonOnColourId, juce::Colours::black.withAlpha (0.4f));
    presetToggle.setColour (juce::TextButton::textColourOffId, theme::globalAccent.brighter (0.3f));
    presetToggle.setColour (juce::TextButton::textColourOnId,  theme::globalAccent.brighter (0.6f));
    presetToggle.setTooltip ("Show / hide the preset browser");
    presetToggle.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe"));   // down triangle
    topBar.setRightControls (&presetBar, 230, &presetToggle, 30);
    addChildComponent (presetOverlay);   // shown on demand, over the right column

    // The preset browser covers the whole right column, so the two views are
    // mutually exclusive.
    presetToggle.onClick = [this]
    {
        const bool show = presetToggle.getToggleState();
        presetToggle.setButtonText (juce::String::fromUTF8 (show ? "\xe2\x96\xb4" : "\xe2\x96\xbe"));
        presetOverlay.setVisible (show);
        if (show)
        {
            presetOverlay.toFront (false);
            showConfigs (false);
        }
    };

    // ---- config bank ---------------------------------------------------------
    configToggle.setButtonText ("Configs");
    configToggle.setAccent (theme::globalAccent, theme::globalAccent.brighter (0.3f));
    configToggle.setTooltip ("Store / recall sequencer configurations");
    configToggle.onClick = [this] { showConfigs (configToggle.getToggleState()); };
    addAndMakeVisible (configToggle);
    addChildComponent (configPanel);

    // ---- globals -------------------------------------------------------------
    theme::styleKnob (drywetKnob, "Dry/Wet", theme::globalAccent);
    addAndMakeVisible (drywetKnob);
    drywetAtt = std::make_unique<SliderAttachment> (processor.apvts, pid::drywet, drywetKnob);

    theme::styleKnob (seedKnob, "Seed", theme::globalAccent);
    addAndMakeVisible (seedKnob);
    seedAtt = std::make_unique<SliderAttachment> (processor.apvts, pid::seed, seedKnob);

    theme::styleKnob (stepsKnob, "Steps", theme::globalAccent);
    addAndMakeVisible (stepsKnob);
    stepsAtt = std::make_unique<SliderAttachment> (processor.apvts, pid::numsteps, stepsKnob);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
            processor.apvts.getParameter (pid::stepsize)))
        stepSizeBox.addItemList (choice->choices, 1);
    theme::styleCombo (stepSizeBox, theme::globalAccent);
    addAndMakeVisible (stepSizeBox);
    stepSizeAtt = std::make_unique<ComboBoxAttachment> (processor.apvts, pid::stepsize, stepSizeBox);

    stepSizeLabel.setText ("step", juce::dontSendNotification);
    stepSizeLabel.setColour (juce::Label::textColourId, theme::dimText);
    stepSizeLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (stepSizeLabel);

    // ---- effect panels (one per lane x type, hidden until selected) ----------
    for (int lane = 0; lane < numLanes; ++lane)
        for (int t = 0; t < kNumEffectTypes; ++t)
        {
            auto& panel = panels[(size_t) lane][(size_t) t];
            panel = std::make_unique<EffectPanel> (processor.apvts, lane, (EffectType) t,
                                                   rack.colourOfLane (lane));
            panel->setVisible (false);
            addChildComponent (*panel);
        }

    // Panel accents follow the lane's bus colour.
    rack.onBusMapChanged = [this]
    {
        for (int lane = 0; lane < numLanes; ++lane)
            for (auto& panel : panels[(size_t) lane])
                panel->setAccent (rack.colourOfLane (lane));
    };

    // ---- block string --------------------------------------------------------
    addAndMakeVisible (blockText);
    blockText.onCommit = [this] (int lane, int block, const juce::String& text)
    {
        const bool ok = processor.setBlockContent (lane, block, text);
        rack.repaint();
        return ok;
    };

    // ---- selection flow -------------------------------------------------------
    rack.onBlockSelected = [this] (int lane, int blockId) { selectBlock (lane, blockId); };
    rack.onBlockContentChanged = [this] (int lane, int blockId)
    {
        if (lane == selectedLane && blockId == selectedBlock)
            refreshBlockText();
    };

    processor.addChangeListener (this);
    startTimerHz (10);

    setSize (1050, 650);
}

MangoAudioProcessorEditor::~MangoAudioProcessorEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MangoAudioProcessorEditor::paint (juce::Graphics& g)
{
    theme::paintBackground (g, getLocalBounds().toFloat());

    // Separation line between the top bar and the plugin controls.
    g.setColour (theme::globalAccent);
    g.fillRect (0, topBar.getBottom(), getWidth(), 2);
}

void MangoAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (54));

    auto right = r.removeFromRight (320).reduced (10, 8);
    rightColumnBounds = right;
    presetOverlay.setBounds (rightColumnBounds);

    // Left: the lane rack with the bus routing strip below it.
    auto left = r;
    busBar.setBounds (left.removeFromBottom (78).reduced (8, 4));
    rack.setBounds (left.reduced (8, 4));

    // Globals: three knobs + the step-size box.
    auto knobRow = right.removeFromTop (96);
    const int kw = knobRow.getWidth() / 3;
    drywetKnob.setBounds (knobRow.removeFromLeft (kw).reduced (2, 0));
    seedKnob.setBounds (knobRow.removeFromLeft (kw).reduced (2, 0));
    stepsKnob.setBounds (knobRow.reduced (2, 0));

    auto sizeRow = right.removeFromTop (26);
    stepSizeLabel.setBounds (sizeRow.removeFromLeft (40));
    configToggle.setBounds (sizeRow.removeFromRight (84).reduced (0, 1));
    sizeRow.removeFromRight (8);
    stepSizeBox.setBounds (sizeRow);
    right.removeFromTop (4);

    right.removeFromTop (8);

    right.removeFromTop (8);

    // Block string at the bottom, panel area in between.
    blockText.setBounds (right.removeFromBottom (mng::BlockTextPanel::kHeight));
    right.removeFromBottom (6);
    panelArea = right;

    for (auto& lanePanels : panels)
        for (auto& panel : lanePanels)
            panel->setBounds (panelArea);
    configPanel.setBounds (panelArea);
}

//==============================================================================
void MangoAudioProcessorEditor::selectBlock (int laneIndex, int blockId)
{
    selectedLane  = laneIndex;
    selectedBlock = blockId;
    refreshVisiblePanel();
    refreshBlockText();
}

void MangoAudioProcessorEditor::showConfigs (bool shouldShow)
{
    if (configsShown == shouldShow)
        return;

    configsShown = shouldShow;
    configToggle.setToggleState (shouldShow, juce::dontSendNotification);
    configPanel.setVisible (shouldShow);

    if (shouldShow)
    {
        configPanel.toFront (false);
        for (auto& lanePanels : panels)
            for (auto& panel : lanePanels)
                panel->setVisible (false);
        visibleLane = visibleType = -1;   // force a rebuild when we come back
    }
    else
    {
        refreshVisiblePanel();
    }
}

void MangoAudioProcessorEditor::refreshVisiblePanel()
{
    if (configsShown)
        return;   // the config bank has the panel area

    const int lane = selectedLane;
    const int type = lane >= 0
        ? (int) processor.apvts.getRawParameterValue (pid::laneType (lane))->load()
        : -1;

    if (lane == visibleLane && type == visibleType)
        return;

    for (auto& lanePanels : panels)
        for (auto& panel : lanePanels)
            panel->setVisible (false);

    if (lane >= 0 && type >= 0 && type < kNumEffectTypes)
        panels[(size_t) lane][(size_t) type]->setVisible (true);

    visibleLane = lane;
    visibleType = type;
}

void MangoAudioProcessorEditor::refreshBlockText()
{
    if (selectedLane >= 0 && selectedBlock >= 0)
        blockText.setBlock (selectedLane, selectedBlock,
                            processor.blockContent (selectedLane, selectedBlock),
                            processor.engine.blockHasParseError (selectedLane, selectedBlock));
    else
        blockText.setBlock (-1, -1, {}, false);
}

void MangoAudioProcessorEditor::timerCallback()
{
    // Follow lane-type changes (combo/automation) for the visible panel.
    refreshVisiblePanel();

    // Drop a selection that became hidden by a lane-count change.
    const int count = processor.engine.visibleLaneCount();
    if (selectedLane >= 0 && processor.engine.rowOfLane (selectedLane) >= count)
    {
        rack.deselectAllExcept (-1);
        selectBlock (-1, -1);
    }
}

void MangoAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // State load or grid change: everything may have moved.
    selectBlock (-1, -1);
    rack.refreshOrder();
}
