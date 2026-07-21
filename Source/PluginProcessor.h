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
                            private juce::AsyncUpdater,
                            private juce::Timer
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

    /** Factory (BinaryData *_xml) + user presets; the side-state hooks keep
        the sequencer blocks (MangoSeq) inside preset files (see ctor). */
    fxme::PresetManager presetManager {
        apvts,
        fxme::PresetManager::getDefaultUserPresetDirectory ("Mango"),
        BinaryData::namedResourceList,
        BinaryData::namedResourceListSize,
        BinaryData::getNamedResource };

    fxme::PresetManager& getPresetManager() noexcept { return presetManager; }

    // ---- message-thread helpers used by the editor ---------------------------

    /** Stores a block's override string (under the engine lock) and re-parses
        the override cache. Returns false if the new text does not parse (it
        is stored anyway so the user can fix it). */
    bool setBlockContent (int laneIndex, int blockId, const juce::String& text);

    juce::String blockContent (int laneIndex, int blockId) const;

    // ---- sequencer config bank (message thread) -------------------------------
    // A config is a snapshot of the sequencer setup stored in the state tree
    // (child "MangoBank"), NOT in parameters: only the `config` selector is
    // automatable. Recalls go through that parameter so GUI clicks, host
    // automation and the active-slot indicator share one source of truth.

    bool configIsStored (int slot) const;
    bool configHasParams (int slot) const;

    /** Snapshots the current setup into `slot` (always: grid, lane
        count/order/types, bus grouping, seed, blocks; plus every effect
        parameter when `includeParams`). Makes `slot` the active config. */
    void storeConfig (int slot, bool includeParams);
    void clearConfig (int slot);

    bool canUndoStore() const { return undoSlot >= 0; }
    void undoStore();

    /** "Include effect parameters" — the store option, kept on the bank so
        it persists with the session/preset without costing a parameter. */
    bool configIncludeParams() const;
    void setConfigIncludeParams (bool shouldInclude);

    /** Requests a recall — applied immediately or at the next bar/pattern
        boundary depending on `configsync`. Re-clicking the active slot
        reloads it (a "revert to stored"). */
    void requestConfigRecall (int slot);

    int  activeConfig() const;
    int  armedConfig() const { return armedSlot; }

    /** True when the live setup has drifted from the active stored config
        (compares a cheap signature taken at the last load/store). */
    bool configIsModified() const;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void applyGridFromParameters();

    juce::ValueTree sequencersToTree() const;
    void sequencersFromTree (const juce::ValueTree& tree);

    juce::ValueTree bankTree();
    juce::ValueTree configTree (int slot) const;
    void applyConfig (int slot);
    void armRecall (int slot);
    void requestConfigRecallSelectorOnly (int slot);
    juce::uint64 configSignature (bool includeParams) const;
    void noteConfigApplied (int slot);

    std::atomic<int> pendingRecall { -1 };   // slot queued by the config parameter
    int armedSlot = -1;                      // waiting for a bar/pattern boundary
    uint32_t armedAtWrap = 0;
    int armedTicks = 0;
    int undoSlot = -1;                       // last overwritten slot...
    juce::ValueTree undoTree;                // ...and its previous contents
    juce::uint64 signatureAtApply = 0;
    bool signatureIncludesParams = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoAudioProcessor)
};
