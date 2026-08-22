/*
  ------------------------------------------------------------------------------
    MangoEngine.h

    The Mango core: eight lanes (numlanes shows 1..8 of them), each a
    StringSequencer + SequencerEngine +
    one effect instance per type (pre-built, so switching a lane's type is
    allocation-free and every type keeps its settings). The lane display
    order IS the processing order (top to bottom); `laneOrder` is a
    permutation over the fixed lane identities 0..7 — parameters, sequences
    and random draws stay attached to the identity when lanes are moved.

    Routing: the visible rows group into up to four buses (row 0 starts
    bus 0; every row whose `busstart` switch is on opens the next bus).
    Each bus runs its rows serially and has its own output volume (0 =
    muted) and pan (balance); `busmode` picks the topology (see
    effectiveBusMode):
    all-parallel; bus 3 fed by the mix of buses 1+2 (bus 4 parallel);
    bus 4 fed by the mix of buses 1-3; a diamond (bus 1 into buses 2 and 3
    in parallel, their mix into bus 4); or a fan-out (bus 1 into every
    remaining bus in parallel). Parallel bus outputs are summed
    before the global dry/wet — with a single bus at default vol/pan the
    chain is bit-identical to the plain serial chain, and idle parallel
    buses each pass a copy of the dry input.

    Aux: on top of the main output the plugin has two auxiliary stereo
    outputs. A lane running the AuxSend effect adds its bus's signal into
    them (rhythmically gated) at the point in the chain where the lane
    sits, and controls how much of it carries on down the main path.

    Locking contract: one CriticalSection (`lock()`) guards the lane
    sequencers, the display order and the parsed-override map. The audio thread
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
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include "EffectBase.h"
#include "EffectTypes.h"
#include "HeldNotes.h"
#include "../ParamIDs.h"

namespace mng
{

class MangoEngine
{
public:
    MangoEngine();

    // ---- message thread ------------------------------------------------------

    /** Declares every per-lane parameter (type choice + all effect types). */
    static void addLaneParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

    void bindParameters (juce::AudioProcessorValueTreeState& apvts);
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    juce::CriticalSection& lock() const noexcept { return seqLock; }

    fxme::StringSequencer& sequencerFor (int laneIndex) { return lanes[(size_t) laneIndex].seq; }
    const fxme::StringSequencer& sequencerFor (int laneIndex) const { return lanes[(size_t) laneIndex].seq; }

    /** Display/processing order: row -> lane identity. */
    std::array<int, numLanes> laneOrder() const;
    int laneAtRow (int row) const;
    int rowOfLane (int laneIndex) const;
    void moveRow (int row, int delta);                       // swap two rows
    void setLaneOrder (const std::array<int, numLanes>&);    // state restore

    /** How many rows are currently shown/processed (the `numlanes`
        parameter). Hidden rows keep sequencing silently, like muted lanes,
        so re-showing them is seamless. */
    int visibleLaneCount() const
    {
        return numLanesParam != nullptr
             ? juce::jlimit (1, numLanes, (int) numLanesParam->load())
             : numLanes;
    }

    /** Applies the shared grid to every lane sequencer (locks internally). */
    void setGrid (fxme::SeqStepSize stepSize, int numSteps);

    /** Bus of each lane identity, from the current order + bus-start
        switches (hidden rows never open a bus; they inherit the last
        visible one). Message thread; locks briefly — the GUI colours come
        from this. */
    std::array<int, numLanes> busMapByLane() const;

    /** How many buses the visible rows currently form (1..numBuses).
        Message thread; locks briefly. */
    int busCount() const;

    /** Re-parses every block string into the override cache. Parsing happens
        outside the lock; only the map swap is locked. Call after any content
        edit or state load. */
    void rebuildOverrides();

    /** True when the block has a non-empty string that does not parse. */
    bool blockHasParseError (int laneIndex, int blockId) const;

    // ---- audio thread ----------------------------------------------------------

    /** MIDI note tracking for the mini-language. `mididur`/`midifreq` follow
        the last note-on; the voice digits follow the notes still held. */
    void midiNoteOn (int noteNumber) noexcept
    {
        midiState.last = midiNotePeriod (noteNumber);
        heldNotes.noteOn (noteNumber);
        publishMidiState();
    }

    void midiNoteOff (int noteNumber) noexcept
    {
        heldNotes.noteOff (noteNumber);
        publishMidiState();
    }

    void midiAllNotesOff() noexcept
    {
        heldNotes.allNotesOff();
        publishMidiState();
    }

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

    /** `aux1`/`aux2` are the plugin's auxiliary stereo outputs, already
        cleared by the caller and sharing `buffer`'s sample indices; lanes
        running an AuxSend effect add into them. Either may be nullptr when
        the host has that bus disabled. */
    void process (juce::AudioBuffer<float>& buffer,
                  const juce::Optional<juce::AudioPlayHead::PositionInfo>& position,
                  float dryWet,
                  juce::AudioBuffer<float>* aux1 = nullptr,
                  juce::AudioBuffer<float>* aux2 = nullptr);

    // ---- GUI polling (atomics published from the audio thread) ---------------

    double guiPlayheadStep (int laneIndex) const { return guiStep[(size_t) laneIndex].load(); }
    int    guiActiveBlock (int laneIndex) const  { return guiActive[(size_t) laneIndex].load(); }
    MidiNoteState guiMidiNotes() const
    {
        MidiNoteState s;
        s.last      = pubLast.load();
        s.heldCount = juce::jlimit (0, MidiNoteState::kMaxVoices, pubHeldCount.load());
        for (int i = 0; i < s.heldCount; ++i)
            s.held[i] = pubHeld[(size_t) i].load();
        return s;
    }

    double guiBpm() const                        { return publishedBpm.load(); }

    /** Counters bumped every time the timeline crosses a bar / pattern
        boundary. The message thread polls them to land a quantised config
        recall on a musical boundary (see MangoAudioProcessor). */
    uint32_t barWrapCount() const     { return barWraps.load(); }
    uint32_t patternWrapCount() const { return patternWraps.load(); }

private:
    static constexpr int kChunk = 32;

    struct Lane
    {
        int laneIndex = 0;
        fxme::StringSequencer seq;
        std::unique_ptr<fxme::SequencerEngine> engine;
        std::array<std::unique_ptr<EffectBase>, kNumEffectTypes> effects;
        std::atomic<float>* typeParam     = nullptr;
        std::atomic<float>* muteParam     = nullptr;
        std::atomic<float>* soloParam     = nullptr;
        std::atomic<float>* busStartParam = nullptr;
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

    std::atomic<float>* seedParam     = nullptr;
    std::atomic<float>* numLanesParam = nullptr;
    std::atomic<float>* busModeParam  = nullptr;
    std::array<std::atomic<float>*, numBuses> busVolParam {};
    std::array<std::atomic<float>*, numBuses> busPanParam {};

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> busBuffer;    // per-bus working buffer
    juce::AudioBuffer<float> feedBuffer;   // mixed feed for a post bus (serial modes)
    juce::AudioBuffer<float> stageBuffer;  // a head bus's output, held as the
                                           // source for the buses it feeds
                                           // (diamond / fan-out modes)

    double sampleRate = 44100.0;
    double currentBpm = 120.0;
    bool   pendingStart = true;   // engines start on the first process() (bpm known)
    double absoluteBeats = 0.0;        // audio thread; host ppq when synced

    // MIDI note state for the mini-language's mididur / midifreq (§5). Audio
    // thread only: written from the MIDI loop, read at block enter, both in
    // the same processBlock call.
    HeldNotes     heldNotes;
    MidiNoteState midiState;

    void publishMidiState() noexcept
    {
        heldNotes.fill (midiState);
        pubLast.store (midiState.last);
        for (int i = 0; i < midiState.heldCount; ++i)
            pubHeld[(size_t) i].store (midiState.held[i]);
        pubHeldCount.store (midiState.heldCount);
    }

    // ...republished for the GUI, which reads it to resolve the override
    // shown on a block. Display only, so a read torn across the voices costs
    // one stale label frame and needs no more than plain atomics.
    std::atomic<float> pubLast { 1.0f / 440.0f };
    std::array<std::atomic<float>, MidiNoteState::kMaxVoices> pubHeld {};
    std::atomic<int>   pubHeldCount { 0 };

    std::array<std::atomic<double>, numLanes> guiStep {};
    std::array<std::atomic<int>, numLanes>    guiActive {};
    std::array<std::atomic<uint32_t>, numLanes> laneParamVersion {};
    std::atomic<double> publishedBpm { 120.0 };

    std::atomic<uint32_t> barWraps { 0 }, patternWraps { 0 };
    int64_t lastBarIndex = 0, lastPatternIndex = 0;
    double  beatsPerBar = 4.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MangoEngine)
};

} // namespace mng
