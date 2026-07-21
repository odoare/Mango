/*
  ------------------------------------------------------------------------------
    MangoEngine.cpp
    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "MangoEngine.h"
#include "Effects/GaterEffect.h"
#include "Effects/GrainDupEffect.h"
#include "Effects/DelayEffect.h"
#include "Effects/DistortionEffect.h"
#include "Effects/FilterEnvEffect.h"
#include "Effects/QuantizerEffect.h"
#include "Effects/RingModEffect.h"
#include "Effects/ReverserEffect.h"
#include "Effects/FreezeEffect.h"
#include "Effects/AuxSendEffect.h"

namespace mng
{

namespace
{
    std::unique_ptr<EffectBase> makeEffect (EffectType type)
    {
        switch (type)
        {
            case EffectType::Gater:      return std::make_unique<GaterEffect>();
            case EffectType::Grain:      return std::make_unique<GrainDupEffect>();
            case EffectType::Delay:      return std::make_unique<DelayEffect>();
            case EffectType::Distortion: return std::make_unique<DistortionEffect>();
            case EffectType::FilterEnv:  return std::make_unique<FilterEnvEffect>();
            case EffectType::Quantizer:  return std::make_unique<QuantizerEffect>();
            case EffectType::RingMod:    return std::make_unique<RingModEffect>();
            case EffectType::Reverser:   return std::make_unique<ReverserEffect>();
            case EffectType::Freeze:     return std::make_unique<FreezeEffect>();
            case EffectType::AuxSend:    return std::make_unique<AuxSendEffect>();
        }
        return nullptr;
    }

    void bindEffect (EffectBase& fx, EffectType type,
                     juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
    {
        switch (type)
        {
            case EffectType::Gater:      static_cast<GaterEffect&>      (fx).bindParameters (apvts, prefix); break;
            case EffectType::Grain:      static_cast<GrainDupEffect&>   (fx).bindParameters (apvts, prefix); break;
            case EffectType::Delay:      static_cast<DelayEffect&>      (fx).bindParameters (apvts, prefix); break;
            case EffectType::Distortion: static_cast<DistortionEffect&> (fx).bindParameters (apvts, prefix); break;
            case EffectType::FilterEnv:  static_cast<FilterEnvEffect&>  (fx).bindParameters (apvts, prefix); break;
            case EffectType::Quantizer:  static_cast<QuantizerEffect&>  (fx).bindParameters (apvts, prefix); break;
            case EffectType::RingMod:    static_cast<RingModEffect&>    (fx).bindParameters (apvts, prefix); break;
            case EffectType::Reverser:   static_cast<ReverserEffect&>   (fx).bindParameters (apvts, prefix); break;
            case EffectType::Freeze:     static_cast<FreezeEffect&>     (fx).bindParameters (apvts, prefix); break;
            case EffectType::AuxSend:    static_cast<AuxSendEffect&>    (fx).bindParameters (apvts, prefix); break;
        }
    }
}

//==============================================================================
MangoEngine::MangoEngine()
{
    for (int i = 0; i < numLanes; ++i)
    {
        auto& lane = lanes[(size_t) i];
        lane.laneIndex = i;
        order[(size_t) i] = i;

        fxme::EngineCallbacks cbs;
        cbs.onBlockEnter = [this, i] (int blockId, const std::string&)
                           { handleBlockEnter (lanes[(size_t) i], blockId); };
        cbs.onBlockExit  = [this, i] (int)
                           { handleBlockExit (lanes[(size_t) i]); };
        lane.engine = std::make_unique<fxme::SequencerEngine> (cbs);
        lane.engine->setEnterEmptyBlocks (true);

        for (int t = 0; t < kNumEffectTypes; ++t)
            lane.effects[(size_t) t] = makeEffect ((EffectType) t);

        guiStep[(size_t) i].store (0.0);
        guiActive[(size_t) i].store (-1);
    }
}

//==============================================================================
void MangoEngine::addLaneParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    for (int i = 0; i < numLanes; ++i)
    {
        const auto prefix = pid::lanePrefix (i);
        const auto nameP  = "L" + juce::String (i + 1) + " ";

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            pid::laneType (i), nameP + "Effect", effectTypeNames(), i % kNumEffectTypes));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            prefix + "mute", nameP + "Mute", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            prefix + "solo", nameP + "Solo", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            pid::laneBusStart (i), nameP + "Bus Start", false));

        GaterEffect::addParameters      (params, prefix, nameP);
        GrainDupEffect::addParameters   (params, prefix, nameP);
        DelayEffect::addParameters      (params, prefix, nameP);
        DistortionEffect::addParameters (params, prefix, nameP);
        FilterEnvEffect::addParameters  (params, prefix, nameP);
        QuantizerEffect::addParameters  (params, prefix, nameP);
        RingModEffect::addParameters    (params, prefix, nameP);
        ReverserEffect::addParameters   (params, prefix, nameP);
        FreezeEffect::addParameters     (params, prefix, nameP);
        AuxSendEffect::addParameters    (params, prefix, nameP);
    }
}

void MangoEngine::bindParameters (juce::AudioProcessorValueTreeState& apvts)
{
    seedParam     = apvts.getRawParameterValue (pid::seed);
    numLanesParam = apvts.getRawParameterValue (pid::numlanes);
    busModeParam  = apvts.getRawParameterValue (pid::busmode);
    for (int b = 0; b < numBuses; ++b)
    {
        busWetParam[(size_t) b] = apvts.getRawParameterValue (pid::busWet (b));
        busPanParam[(size_t) b] = apvts.getRawParameterValue (pid::busPan (b));
    }

    for (auto& lane : lanes)
    {
        const auto prefix = pid::lanePrefix (lane.laneIndex);
        lane.typeParam     = apvts.getRawParameterValue (pid::laneType (lane.laneIndex));
        lane.muteParam     = apvts.getRawParameterValue (prefix + "mute");
        lane.soloParam     = apvts.getRawParameterValue (prefix + "solo");
        lane.busStartParam = apvts.getRawParameterValue (pid::laneBusStart (lane.laneIndex));
        lane.currentType   = (int) lane.typeParam->load();

        for (int t = 0; t < kNumEffectTypes; ++t)
            bindEffect (*lane.effects[(size_t) t], (EffectType) t, apvts, prefix);
    }
}

void MangoEngine::prepare (double sr, int maxBlockSize, int numChannels)
{
    sampleRate = sr;
    dryBuffer.setSize (juce::jmax (1, numChannels), maxBlockSize);
    busBuffer.setSize (juce::jmax (1, numChannels), maxBlockSize);
    feedBuffer.setSize (juce::jmax (1, numChannels), maxBlockSize);
    absoluteBeats = 0.0;

    const juce::ScopedLock sl (seqLock);
    for (auto& lane : lanes)
    {
        for (auto& fx : lane.effects)
        {
            fx->prepare (sr, maxBlockSize, numChannels);
            fx->reset();
        }
        lane.engine->stop();
        lane.engine->reset();
        lane.active        = false;
        lane.activeBlockId = -1;
    }

    // The engines are started from the first process() call, once the host
    // tempo is known — an enter fired here would draw durations at a stale
    // bpm.
    pendingStart = true;
}

//==============================================================================
std::array<int, numLanes> MangoEngine::laneOrder() const
{
    const juce::ScopedLock sl (seqLock);
    return order;
}

int MangoEngine::laneAtRow (int row) const
{
    const juce::ScopedLock sl (seqLock);
    return order[(size_t) juce::jlimit (0, numLanes - 1, row)];
}

int MangoEngine::rowOfLane (int laneIndex) const
{
    const juce::ScopedLock sl (seqLock);
    for (int row = 0; row < numLanes; ++row)
        if (order[(size_t) row] == laneIndex)
            return row;
    return -1;
}

void MangoEngine::moveRow (int row, int delta)
{
    const int other = row + delta;
    if (row < 0 || row >= numLanes || other < 0 || other >= numLanes)
        return;
    const juce::ScopedLock sl (seqLock);
    std::swap (order[(size_t) row], order[(size_t) other]);
}

void MangoEngine::setLaneOrder (const std::array<int, numLanes>& newOrder)
{
    // Accept only a real permutation of 0..5.
    bool seen[numLanes] = {};
    for (int lane : newOrder)
    {
        if (lane < 0 || lane >= numLanes || seen[lane])
            return;
        seen[lane] = true;
    }
    const juce::ScopedLock sl (seqLock);
    order = newOrder;
}

std::array<int, numLanes> MangoEngine::busMapByLane() const
{
    const juce::ScopedLock sl (seqLock);

    std::array<int, numLanes> result {};
    const int visibleRows = visibleLaneCount();
    int busCount = 1;
    for (int row = 0; row < numLanes; ++row)
    {
        const auto& lane = lanes[(size_t) order[(size_t) row]];
        if (row > 0 && row < visibleRows && busCount < numBuses
            && lane.busStartParam != nullptr && lane.busStartParam->load() > 0.5f)
            ++busCount;
        result[(size_t) order[(size_t) row]] = busCount - 1;   // hidden rows inherit the last bus
    }
    return result;
}

int MangoEngine::busCount() const
{
    const auto map = busMapByLane();
    int highest = 0;
    for (int b : map)
        highest = juce::jmax (highest, b);
    return highest + 1;
}

void MangoEngine::setGrid (fxme::SeqStepSize stepSize, int numSteps)
{
    const juce::ScopedLock sl (seqLock);
    for (auto& lane : lanes)
    {
        lane.seq.setStepSize (stepSize);
        lane.seq.setNumSteps (numSteps);
    }
}

void MangoEngine::rebuildOverrides()
{
    // Snapshot the block strings under the lock (small copies), parse outside,
    // then swap the fresh map in under the lock.
    std::vector<std::pair<uint64_t, std::string>> contents;
    {
        const juce::ScopedLock sl (seqLock);
        for (const auto& lane : lanes)
            for (const auto& b : lane.seq.blocks())
                if (! b.content.empty())
                    contents.emplace_back (overrideKey (lane.laneIndex, b.id), b.content);
    }

    overridesScratch.clear();
    for (const auto& [key, text] : contents)
        if (const auto parsed = parseOverrides (text))
            overridesScratch.emplace (key, *parsed);

    {
        const juce::ScopedLock sl (seqLock);
        std::swap (overrides, overridesScratch);
    }
    overridesScratch.clear();
}

bool MangoEngine::blockHasParseError (int laneIndex, int blockId) const
{
    const juce::ScopedLock sl (seqLock);
    const auto* b = lanes[(size_t) laneIndex].seq.blockById (blockId);
    if (b == nullptr || b->content.empty())
        return false;
    return overrides.find (overrideKey (laneIndex, blockId)) == overrides.end();
}

//==============================================================================
void MangoEngine::handleBlockEnter (Lane& lane, int blockId, bool isReEnter)
{
    BlockContext ctx;
    ctx.laneIndex      = lane.laneIndex;
    ctx.blockId        = blockId;
    ctx.seed           = (uint64_t) (int64_t) seedParam->load();
    ctx.sampleRate     = sampleRate;
    ctx.bpm            = currentBpm;
    ctx.mididurSeconds = mididurSeconds.load();
    ctx.isReEnter      = isReEnter;

    const auto it = overrides.find (overrideKey (lane.laneIndex, blockId));
    ctx.overrides = it != overrides.end() ? &it->second : nullptr;

    currentEffect (lane).onBlockEnter (ctx);
    lane.active        = true;
    lane.activeBlockId = blockId;
}

void MangoEngine::handleBlockExit (Lane& lane)
{
    if (lane.active)
        currentEffect (lane).onBlockExit();
    lane.active        = false;
    lane.activeBlockId = -1;
}

void MangoEngine::syncEffectType (Lane& lane)
{
    const int wanted = (int) lane.typeParam->load();
    if (wanted == lane.currentType)
        return;

    const bool wasActive = lane.active;
    const int  blockId   = lane.activeBlockId;
    if (wasActive)
        handleBlockExit (lane);

    lane.currentType = juce::jlimit (0, kNumEffectTypes - 1, wanted);

    // Re-enter the sounding block with the new effect.
    if (wasActive && blockId >= 0)
        handleBlockEnter (lane, blockId);
}

//==============================================================================
void MangoEngine::process (juce::AudioBuffer<float>& buffer,
                           const juce::Optional<juce::AudioPlayHead::PositionInfo>& position,
                           float dryWet,
                           juce::AudioBuffer<float>* aux1,
                           juce::AudioBuffer<float>* aux2)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    const juce::ScopedLock sl (seqLock);   // see the locking contract in the header

    if (position && position->getBpm())
        currentBpm = juce::jmax (1.0, *position->getBpm());

    if (position)
        if (const auto sig = position->getTimeSignature())
            beatsPerBar = juce::jmax (1.0, (double) sig->numerator * 4.0 / (double) sig->denominator);

    if (pendingStart)
    {
        pendingStart = false;
        for (auto& lane : lanes)
            lane.engine->start (lane.seq);
    }

    const bool hostPlaying = position && position->getIsPlaying();
    if (hostPlaying && position->getPpqPosition())
    {
        // Follow the host timeline. On a jump (loop wrap, relocate) every
        // lane exits and re-enters — even into the same block — so the new
        // pass gets its own draw (relocate(), unlike setPositionBeats,
        // guarantees that).
        const double ppq = juce::jmax (0.0, *position->getPpqPosition());
        if (std::abs (ppq - absoluteBeats) > 1.0e-3)
        {
            absoluteBeats = ppq;
            for (auto& lane : lanes)
                lane.engine->relocate (absoluteBeats, lane.seq);
        }
    }

    for (auto& lane : lanes)
    {
        syncEffectType (lane);

        // Lane parameters changed (weights, attack, ...): refresh the
        // sounding block immediately instead of waiting for the next entry.
        const uint32_t version = laneParamVersion[(size_t) lane.laneIndex].load();
        if (version != lane.seenParamVersion)
        {
            lane.seenParamVersion = version;
            if (lane.active && lane.activeBlockId >= 0)
            {
                handleBlockEnter (lane, lane.activeBlockId, true);
            }
        }

        // Aux destinations for the lanes that send (types are settled by
        // now). They live for this process() call only.
        currentEffect (lane).setAuxBuffers (aux1, aux2);
    }

    // Lane count + mute/solo: hidden rows and bypassed lanes still advance
    // and fire enter/exit (sequencing stays deterministic and re-showing a
    // lane is seamless), they just don't process audio. Solo only counts
    // among the visible rows — a hidden soloed lane must not silence the
    // rest.
    const int visibleRows = visibleLaneCount();
    bool anySolo = false;
    for (int row = 0; row < visibleRows; ++row)
        anySolo = anySolo || lanes[(size_t) order[(size_t) row]].soloParam->load() > 0.5f;

    // The visible rows group into parallel buses: row 0 starts bus 0, and
    // each row whose bus-start switch is on opens the next one (switches
    // beyond the numBuses'th bus are ignored; hidden rows never open one).
    int busOfRow[numLanes];
    int busCount = 1;
    for (int row = 0; row < visibleRows; ++row)
    {
        if (row > 0 && busCount < numBuses
            && lanes[(size_t) order[(size_t) row]].busStartParam->load() > 0.5f)
            ++busCount;
        busOfRow[row] = busCount - 1;
    }

    const int busMode = effectiveBusMode (
        busModeParam != nullptr ? (int) busModeParam->load() : 0, busCount);

    // One bus, into busBuffer: copy its input, run its rows serially, then
    // the per-bus dry/wet (against the bus input) and pan (balance; only
    // touched when not neutral, so defaults stay bit-transparent).
    auto processBus = [&] (int bus, const juce::AudioBuffer<float>& input, int offset, int n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            busBuffer.copyFrom (ch, offset, input, ch, offset, n);

        for (int row = 0; row < visibleRows; ++row)
        {
            if (busOfRow[row] != bus)
                continue;

            auto& lane = lanes[(size_t) order[(size_t) row]];
            const bool audible = lane.muteParam->load() < 0.5f
                              && (! anySolo || lane.soloParam->load() > 0.5f);
            if (lane.active && audible)
                currentEffect (lane).process (busBuffer, offset, n);
        }

        const float wet = busWetParam[(size_t) bus] != nullptr
                        ? juce::jlimit (0.0f, 1.0f, busWetParam[(size_t) bus]->load()) : 1.0f;
        if (wet < 1.0f)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* in = input.getReadPointer (ch) + offset;
                float*       bb = busBuffer.getWritePointer (ch) + offset;
                for (int i = 0; i < n; ++i)
                    bb[i] = in[i] + wet * (bb[i] - in[i]);
            }

        const float pan = busPanParam[(size_t) bus] != nullptr
                        ? juce::jlimit (-1.0f, 1.0f, busPanParam[(size_t) bus]->load()) : 0.0f;
        if (pan != 0.0f && numChannels >= 2)
        {
            busBuffer.applyGain (0, offset, n, pan > 0.0f ? 1.0f - pan : 1.0f);
            busBuffer.applyGain (1, offset, n, pan < 0.0f ? 1.0f + pan : 1.0f);
        }
    };

    auto addBus = [&] (juce::AudioBuffer<float>& dest, int offset, int n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            dest.addFrom (ch, offset, busBuffer, ch, offset, n);
    };

    for (int offset = 0; offset < numSamples; offset += kChunk)
    {
        const int    n     = juce::jmin (kChunk, numSamples - offset);
        const double delta = n * currentBpm / (60.0 * sampleRate);

        // Sequencing first: every lane advances and fires enter/exit,
        // hidden and bypassed lanes included.
        for (int row = 0; row < numLanes; ++row)
        {
            auto& lane = lanes[(size_t) order[(size_t) row]];
            lane.engine->advance (delta, lane.seq);
        }

        // Then the buses, per the routing mode. Parallel buses each start
        // from the dry input and sum into the output; a "post" bus starts
        // from the mixed outputs of its feeders instead.
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.clear (ch, offset, n);

        if (busMode == 0)
        {
            for (int bus = 0; bus < busCount; ++bus)
            {
                processBus (bus, dryBuffer, offset, n);
                addBus (buffer, offset, n);
            }
        }
        else
        {
            const int numFeeders = busMode == 1 ? 2 : 3;   // feeders -> post bus
            for (int ch = 0; ch < numChannels; ++ch)
                feedBuffer.clear (ch, offset, n);
            for (int bus = 0; bus < numFeeders; ++bus)
            {
                processBus (bus, dryBuffer, offset, n);
                addBus (feedBuffer, offset, n);
            }
            processBus (numFeeders, feedBuffer, offset, n);
            addBus (buffer, offset, n);

            // Mode 1 with four buses: bus 4 stays parallel from the input.
            for (int bus = numFeeders + 1; bus < busCount; ++bus)
            {
                processBus (bus, dryBuffer, offset, n);
                addBus (buffer, offset, n);
            }
        }

        absoluteBeats += delta;

        // Publish bar / pattern boundary crossings for quantised config
        // recalls (the message thread polls these counters).
        const auto barIndex = (int64_t) std::floor (absoluteBeats / beatsPerBar);
        if (barIndex != lastBarIndex)
        {
            lastBarIndex = barIndex;
            barWraps.fetch_add (1);
        }
        const double patternBeats = lanes[0].seq.getPatternLengthBeats();
        if (patternBeats > 1.0e-6)
        {
            const auto patternIndex = (int64_t) std::floor (absoluteBeats / patternBeats);
            if (patternIndex != lastPatternIndex)
            {
                lastPatternIndex = patternIndex;
                patternWraps.fetch_add (1);
            }
        }
    }

    // Global dry/wet.
    const float wet = juce::jlimit (0.0f, 1.0f, dryWet);
    if (wet < 1.0f)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.applyGain (ch, 0, numSamples, wet);
            buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples, 1.0f - wet);
        }

    // Publish GUI state.
    publishedBpm.store (currentBpm);
    for (const auto& lane : lanes)
    {
        guiStep[(size_t) lane.laneIndex].store (lane.engine->playheadStep (lane.seq));
        guiActive[(size_t) lane.laneIndex].store (lane.active ? lane.activeBlockId : -1);
    }
}

} // namespace mng
