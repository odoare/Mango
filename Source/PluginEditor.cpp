/*
  ------------------------------------------------------------------------------
    PluginEditor.cpp
    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginEditor.h"
#include "ParamIDs.h"

using namespace mng;
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

//==============================================================================
MangoAudioProcessorEditor::MangoAudioProcessorEditor (MangoAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (topBar);

    theme::styleKnob (drywetKnob, "Dry/Wet", theme::drywetAccent);
    addAndMakeVisible (drywetKnob);
    drywetAtt = std::make_unique<SliderAttachment> (processor.apvts, pid::drywet, drywetKnob);

    setSize (1000, 650);
}

MangoAudioProcessorEditor::~MangoAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void MangoAudioProcessorEditor::paint (juce::Graphics& g)
{
    theme::paintBackground (g, getLocalBounds().toFloat());
}

void MangoAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (54));

    auto right = r.removeFromRight (260).reduced (10);
    drywetKnob.setBounds (right.removeFromTop (110).removeFromLeft (90));
}
