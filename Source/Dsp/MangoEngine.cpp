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

        GaterEffect::addParameters      (params, prefix, nameP);
        GrainDupEffect::addParameters   (params, prefix, nameP);
        DelayEffect::addParameters      (params, prefix, nameP);
        DistortionEffect::addParameters (params, prefix, nameP);
        FilterEnvEffect::addParameters  (params, prefix, nameP);
        QuantizerEffect::addParameters  (params, prefix, nameP);
    }
}

void MangoEngine::bindParameters (juce::AudioProcessorValueTreeState& apvts)
{
    seedParam = apvts.getRawParameterValue (pid::seed);

    for (auto& lane : lanes)
    {
        const auto prefix = pid::lanePrefix (lane.laneIndex);
        lane.typeParam   = apvts.getRawParameterValue (pid::laneType (lane.laneIndex));
        lane.currentType = (int) lane.typeParam->load();

        for (int t = 0; t < kNumEffectTypes; ++t)
            bindEffect (*lane.effects[(size_t) t], (EffectType) t, apvts, prefix);
    }
}

void MangoEngine::prepare (double sr, int maxBlockSize, int numChannels)
{
    sampleRate = sr;
    dryBuffer.setSize (juce::jmax (1, numChannels), maxBlockSize);
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
void MangoEngine::handleBlockEnter (Lane& lane, int blockId)
{
    BlockContext ctx;
    ctx.laneIndex      = lane.laneIndex;
    ctx.blockId        = blockId;
    ctx.seed           = (uint64_t) (int64_t) seedParam->load();
    ctx.sampleRate     = sampleRate;
    ctx.bpm            = currentBpm;
    ctx.mididurSeconds = mididurSeconds.load();

    // The pattern pass this entry belongs to, exact regardless of buffer
    // chunking: the absolute enter position is k * patternLen + blockStart.
    const double patLen = lane.seq.getPatternLengthBeats();
    if (const auto* b = lane.seq.blockById (blockId); b != nullptr && patLen > 0.0)
    {
        const double blockStartBeats = b->startStep * lane.seq.getStepSizeBeats();
        ctx.loopIndex = (int64_t) std::llround ((chunkStartBeats - blockStartBeats) / patLen);
    }

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
                           float dryWet)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    const juce::ScopedLock sl (seqLock);   // see the locking contract in the header

    if (position && position->getBpm())
        currentBpm = juce::jmax (1.0, *position->getBpm());

    if (pendingStart)
    {
        pendingStart = false;
        chunkStartBeats = absoluteBeats;
        for (auto& lane : lanes)
            lane.engine->start (lane.seq);
    }

    const bool hostPlaying = position && position->getIsPlaying();
    if (hostPlaying && position->getPpqPosition())
    {
        // Follow the host timeline; resync on jumps (loops, relocates).
        const double ppq = juce::jmax (0.0, *position->getPpqPosition());
        if (std::abs (ppq - absoluteBeats) > 1.0e-3)
        {
            absoluteBeats = ppq;
            chunkStartBeats = absoluteBeats;
            for (auto& lane : lanes)
                lane.engine->setPositionBeats (absoluteBeats, lane.seq);
        }
    }

    for (auto& lane : lanes)
        syncEffectType (lane);

    for (int offset = 0; offset < numSamples; offset += kChunk)
    {
        const int    n     = juce::jmin (kChunk, numSamples - offset);
        const double delta = n * currentBpm / (60.0 * sampleRate);
        chunkStartBeats = absoluteBeats;

        for (int row = 0; row < numLanes; ++row)
        {
            auto& lane = lanes[(size_t) order[(size_t) row]];
            lane.engine->advance (delta, lane.seq);
            if (lane.active)
                currentEffect (lane).process (buffer, offset, n);
        }

        absoluteBeats += delta;
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
    for (const auto& lane : lanes)
    {
        guiStep[(size_t) lane.laneIndex].store (lane.engine->playheadStep (lane.seq));
        guiActive[(size_t) lane.laneIndex].store (lane.active ? lane.activeBlockId : -1);
    }
}

} // namespace mng
