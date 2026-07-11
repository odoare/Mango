/*
  ------------------------------------------------------------------------------
    PluginProcessor.cpp
    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

using namespace mng;

//==============================================================================
MangoAudioProcessor::MangoAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

MangoAudioProcessor::~MangoAudioProcessor() = default;

//==============================================================================
const juce::String MangoAudioProcessor::getName() const   { return JucePlugin_Name; }
bool MangoAudioProcessor::acceptsMidi() const              { return true; }
bool MangoAudioProcessor::producesMidi() const             { return false; }
bool MangoAudioProcessor::isMidiEffect() const             { return false; }
double MangoAudioProcessor::getTailLengthSeconds() const   { return 0.1; }

int MangoAudioProcessor::getNumPrograms()                  { return 1; }
int MangoAudioProcessor::getCurrentProgram()               { return 0; }
void MangoAudioProcessor::setCurrentProgram (int)          {}
const juce::String MangoAudioProcessor::getProgramName (int) { return {}; }
void MangoAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void MangoAudioProcessor::prepareToPlay (double, int)
{
}

void MangoAudioProcessor::releaseResources()
{
}

bool MangoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && in.size() <= out.size();
}

void MangoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MangoAudioProcessor::createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        pid::drywet, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        pid::seed, "Seed", 0, 99999, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        pid::stepsize, "Step size",
        juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1/1" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        pid::numsteps, "Steps", 1, 64, 16));

    return { params.begin(), params.end() };
}

//==============================================================================
bool MangoAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MangoAudioProcessor::createEditor()
{
    return new MangoAudioProcessorEditor (*this);
}

//==============================================================================
void MangoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);
    apvts.state.writeToStream (mos);
}

void MangoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
        apvts.replaceState (tree);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MangoAudioProcessor();
}
