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
    : AudioProcessorEditor (&p), processor (p), rack (p), busBar (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (topBar);
    addAndMakeVisible (rack);
    addAndMakeVisible (busBar);

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
    stepSizeBox.setBounds (sizeRow);
    right.removeFromTop (4);

    right.removeFromTop (8);

    // Block string at the bottom, panel area in between.
    blockText.setBounds (right.removeFromBottom (mng::BlockTextPanel::kHeight));
    right.removeFromBottom (6);
    panelArea = right;

    for (auto& lanePanels : panels)
        for (auto& panel : lanePanels)
            panel->setBounds (panelArea);
}

//==============================================================================
void MangoAudioProcessorEditor::selectBlock (int laneIndex, int blockId)
{
    selectedLane  = laneIndex;
    selectedBlock = blockId;
    refreshVisiblePanel();
    refreshBlockText();
}

void MangoAudioProcessorEditor::refreshVisiblePanel()
{
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
