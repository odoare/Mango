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

    // ---- level metering (fxme::VuMeter, fed from the audio thread) -----------
    // Four stereo taps for the top-bar meter strip. The aux ones read the
    // buses whether or not the host enabled them: a disabled bus is fed
    // silence so its bars fall away instead of freezing at their last level.

    enum MeterTap { MeterInput = 0, MeterOutput, MeterAux1, MeterAux2, kNumMeterTaps };

    /** RMS level in dBFS of one tap's channel (0 = left). Message thread;
        reads an atomic published by the audio thread. */
    float meterLevelDb (int tap, int channel) const
    {
        return meters[(size_t) juce::jlimit (0, kNumMeterTaps - 1, tap)]
                     [(size_t) juce::jlimit (0, 1, channel)].getRMS();
    }

    // ---- editor view state ----------------------------------------------------
    // What the GUI looked like: the selected block and which right-column
    // panel was open. Deliberately NOT parameters and NOT part of
    // apvts.state — it is appended at getStateInformation time, like
    // MangoSeq, so it rides along in the session while staying out of
    // presets: loading a sound must not rearrange someone's GUI.
    //
    // The editor is free to come and go; this outlives it because the
    // processor does.

    struct ViewState
    {
        int  selectedLane  = -1;      // lane identity; -1 = nothing selected
        int  selectedBlock = -1;
        bool configsShown  = false;   // mutually exclusive with presetsShown
        bool presetsShown  = false;
    };

    /** Message thread only (the editor owns this). */
    ViewState& view() noexcept { return viewState; }
    const ViewState& view() const noexcept { return viewState; }

    /** True the first time it is called on this processor, false ever after.
        The editor uses it to show the splash once — the flag has to live
        here, not on the editor, because the editor is destroyed and rebuilt
        every time the window is closed and reopened. Not serialised: a
        reloaded session is a new run and gets its splash. */
    bool claimSplash() noexcept
    {
        const bool first = ! splashClaimed;
        splashClaimed = true;
        return first;
    }

    // ---- message-thread helpers used by the editor ---------------------------

    /** Stores a block's override string (under the engine lock) and re-parses
        the override cache. Returns false if the new text does not parse (it
        is stored anyway so the user can fix it). */
    bool setBlockContent (int laneIndex, int blockId, const juce::String& text);

    juce::String blockContent (int laneIndex, int blockId) const;

    /** True when that lane still holds that block — a restored selection can
        point at one a grid change or a config recall has since removed. */
    bool blockExists (int laneIndex, int blockId) const;

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

    juce::ValueTree viewToTree() const;
    void viewFromTree (const juce::ValueTree& tree);
    ViewState viewState;
    bool splashClaimed = false;   // runtime only, never saved

    juce::ValueTree bankTree();
    juce::ValueTree configTree (int slot) const;
    void applyConfig (int slot);
    void armRecall (int slot);
    void requestConfigRecallSelectorOnly (int slot);
    juce::uint64 configSignature (bool includeParams) const;
    void noteConfigApplied (int slot);

    /** Feeds one stereo tap, mirroring a mono source onto both bars.
        `numChannels` overrides how many of `source`'s channels really carry
        signal — a mono input sits in a stereo main buffer whose second
        channel we cleared, and metering that as silence would be a lie. */
    void feedMeter (int tap, const juce::AudioBuffer<float>& source, int numSamples,
                    int numChannels = -1);

    std::array<std::array<fxme::VuMeter, 2>, kNumMeterTaps> meters;
    juce::AudioBuffer<float> silence;        // fed to taps with nothing to read

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
