/*
  ------------------------------------------------------------------------------
    PluginProcessor.h

    Mango — modular sound glitcher / mangler. Six sequencer lanes, each with a
    selectable effect (gater, grain duplicator, delay, distortion, filter with
    envelope, quantizer); a lane's effect is active while the playhead is
    inside one of its blocks. Lane order (top to bottom) is the processing
    order. Takes MIDI input (the `mididur` magic value of the per-block
    override language is the period of the last note received).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class MangoAudioProcessor : public juce::AudioProcessor
{
public:
    MangoAudioProcessor();
    ~MangoAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "Parameters", createParameters() };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoAudioProcessor)
};
