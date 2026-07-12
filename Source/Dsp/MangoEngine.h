/*
  ------------------------------------------------------------------------------
    MangoEngine.h

    The Mango core: six lanes, each a StringSequencer + SequencerEngine +
    one effect instance per type (pre-built, so switching a lane's type is
    allocation-free and every type keeps its settings). The lane display
    order IS the processing order (top to bottom); `laneOrder` is a
    permutation over the fixed lane identities 0..5 — parameters, sequences
    and random draws stay attached to the identity when lanes are moved.

    Locking contract: one CriticalSection (`lock()`) guards the six
    sequencers, the lane order and the parsed-override map. The audio thread
    holds it for the whole DSP section of process(); every message-thread
    critical section must therefore be tiny and allocation-free — parsing
    and map building happen OUTSIDE the lock (rebuildOverrides), only the
    swap happens inside. GUI sequencer edits must run under the same lock
    (see LaneRackComponent's locked rubber).

    Determinism: every random draw is fxme::detrand::u01(seed, laneIndex,
    blockId, drawIndex) — a pure function of the seed and the block, NOT of
    time. Every pattern pass therefore plays exactly the drawn sequence the
    block visuals display, and looping a section in the DAW or re-bouncing
    reproduces the same glitches; changing the seed re-rolls every block.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include "EffectBase.h"
#include "EffectTypes.h"
#include "../ParamIDs.h"

namespace mng
{

class MangoEngine
{
public:
    MangoEngine();

    // ---- message thread ------------------------------------------------------

    /** Declares every per-lane parameter (type choice + all six effects). */
    static void addLaneParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

    void bindParameters (juce::AudioProcessorValueTreeState& apvts);
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    juce::CriticalSection& lock() const noexcept { return seqLock; }

    fxme::StringSequencer& sequencerFor (int laneIndex) { return lanes[(size_t) laneIndex].seq; }
    const fxme::StringSequencer& sequencerFor (int laneIndex) const { return lanes[(size_t) laneIndex].seq; }

    /** Display/processing order: row -> lane identity. */
    std::array<int, numLanes> laneOrder() const;
    int laneAtRow (int row) const;
    void moveRow (int row, int delta);                       // swap two rows
    void setLaneOrder (const std::array<int, numLanes>&);    // state restore

    /** Applies the shared grid to all six sequencers (locks internally). */
    void setGrid (fxme::SeqStepSize stepSize, int numSteps);

    /** Re-parses every block string into the override cache. Parsing happens
        outside the lock; only the map swap is locked. Call after any content
        edit or state load. */
    void rebuildOverrides();

    /** True when the block has a non-empty string that does not parse. */
    bool blockHasParseError (int laneIndex, int blockId) const;

    // ---- audio thread ----------------------------------------------------------

    void setMididurSeconds (float s) noexcept { mididurSeconds.store (s); }

    /** A parameter of this lane changed: its active block is re-entered on
        the next process() so the audible result updates immediately (same
        block draw, new parameter values). Any thread. */
    void noteLaneParamsChanged (int laneIndex) noexcept
    {
        if (laneIndex >= 0 && laneIndex < numLanes)
            laneParamVersion[(size_t) laneIndex].fetch_add (1);
    }

    /** A global parameter affecting every lane changed (e.g. the seed). */
    void noteGlobalParamsChanged() noexcept
    {
        for (auto& v : laneParamVersion)
            v.fetch_add (1);
    }

    void process (juce::AudioBuffer<float>& buffer,
                  const juce::Optional<juce::AudioPlayHead::PositionInfo>& position,
                  float dryWet);

    // ---- GUI polling (atomics published from the audio thread) ---------------

    double guiPlayheadStep (int laneIndex) const { return guiStep[(size_t) laneIndex].load(); }
    int    guiActiveBlock (int laneIndex) const  { return guiActive[(size_t) laneIndex].load(); }
    float  guiMididur() const                    { return mididurSeconds.load(); }
    double guiBpm() const                        { return publishedBpm.load(); }

private:
    static constexpr int kChunk = 32;

    struct Lane
    {
        int laneIndex = 0;
        fxme::StringSequencer seq;
        std::unique_ptr<fxme::SequencerEngine> engine;
        std::array<std::unique_ptr<EffectBase>, kNumEffectTypes> effects;
        std::atomic<float>* typeParam = nullptr;
        std::atomic<float>* muteParam = nullptr;
        std::atomic<float>* soloParam = nullptr;
        int      currentType     = 0;      // audio-thread view of typeParam
        bool     active          = false;  // an entered block is sounding
        int      activeBlockId   = -1;
        uint32_t seenParamVersion = 0;
    };

    EffectBase& currentEffect (Lane& lane) { return *lane.effects[(size_t) lane.currentType]; }

    void handleBlockEnter (Lane& lane, int blockId, bool isReEnter = false);
    void handleBlockExit (Lane& lane);
    void syncEffectType (Lane& lane);

    static uint64_t overrideKey (int laneIndex, int blockId)
    {
        return ((uint64_t) (uint32_t) laneIndex << 32) | (uint64_t) (uint32_t) blockId;
    }

    std::array<Lane, numLanes> lanes;
    std::array<int, numLanes>  order {};   // row -> lane identity

    mutable juce::CriticalSection seqLock;
    std::unordered_map<uint64_t, ParsedOverrides> overrides;
    std::unordered_map<uint64_t, ParsedOverrides> overridesScratch;   // build target, message thread

    std::atomic<float>* seedParam = nullptr;

    juce::AudioBuffer<float> dryBuffer;

    double sampleRate = 44100.0;
    double currentBpm = 120.0;
    bool   pendingStart = true;   // engines start on the first process() (bpm known)
    double absoluteBeats = 0.0;        // audio thread; host ppq when synced
    std::atomic<float> mididurSeconds { 1.0f / 440.0f };

    std::array<std::atomic<double>, numLanes> guiStep {};
    std::array<std::atomic<int>, numLanes>    guiActive {};
    std::array<std::atomic<uint32_t>, numLanes> laneParamVersion {};
    std::atomic<double> publishedBpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoEngine)
};

} // namespace mng
