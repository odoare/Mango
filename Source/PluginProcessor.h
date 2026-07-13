/*
  ------------------------------------------------------------------------------
    PluginProcessor.h

    Mango — modular sound glitcher / mangler. Up to eight sequencer lanes
    (numlanes parameter, default 4), each with a
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
#include "Dsp/MangoEngine.h"

//==============================================================================
class MangoAudioProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
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

    mng::MangoEngine engine;

    // ---- message-thread helpers used by the editor ---------------------------

    /** Stores a block's override string (under the engine lock) and re-parses
        the override cache. Returns false if the new text does not parse (it
        is stored anyway so the user can fix it). */
    bool setBlockContent (int laneIndex, int blockId, const juce::String& text);

    juce::String blockContent (int laneIndex, int blockId) const;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void applyGridFromParameters();

    juce::ValueTree sequencersToTree() const;
    void sequencersFromTree (const juce::ValueTree& tree);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoAudioProcessor)
};
