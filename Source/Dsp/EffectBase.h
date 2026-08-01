/*
  ------------------------------------------------------------------------------
    EffectBase.h

    The interface every Mango lane effect implements, and the context handed
    to an effect when the playhead enters one of its lane's blocks. New
    effect types are added by subclassing EffectBase, providing the two
    parameter hooks (static addParameters + bindParameters, FxmeFX style)
    and registering the type in EffectTypes.h.

    Threading: prepare/reset/bindParameters run on the message thread;
    onBlockEnter / onBlockExit / process run on the audio thread and must
    not allocate. Live-tweakable values are read from the bound APVTS
    atomics inside process(); values that depend on the random draw or on
    a block override are resolved once, in onBlockEnter().

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "OverrideParser.h"

namespace mng
{

/** Everything an effect may need when its block starts sounding. Random
    draws are keyed on (seed, laneIndex, blockId, drawIndex) only — every
    pattern pass replays the same drawn sequence, exactly as displayed. */
struct BlockContext
{
    int      laneIndex  = 0;       // stable lane identity (not display position)
    int      blockId    = -1;
    uint64_t seed       = 0;       // global seed parameter
    double   sampleRate = 44100.0;
    double   bpm        = 120.0;
    MidiNoteState midi;            // what mididur / midifreq resolve against
    int      blockLengthSamples = 0;   // the block's span in samples at this tempo
                                       // (0 if unknown); effects that fade at
                                       // the block edge or clock inside it use it
    const ParsedOverrides* overrides = nullptr;   // nullptr: none / parse error

    /** True when this is a parameter-change refresh of an already-sounding
        block (same loop pass, same random draw input): effects should apply
        the new values but preserve their running phase where possible. */
    bool isReEnter = false;
};

/** Curve parameter (0 slow, 0.5 linear, 1 very fast) -> envelope exponent.
    Attack gain = ramp^gamma; release gain = remaining^gamma, so the release
    mapping is mirrored (fast release = big exponent = exponential-decay
    tail). Shared by the effects and the block visuals. */
inline constexpr float kCurveRange = 6.0f;
inline float attackGammaFor (float curve)  { return std::pow (kCurveRange, 1.0f - 2.0f * curve); }
inline float releaseGammaFor (float curve) { return std::pow (kCurveRange, 2.0f * curve - 1.0f); }

/** Attack/release lengths as fractions (0..1) of the shaped duration. When
    their sum exceeds 1 they share the duration proportionally to their
    values: att=1, rel=0.5 -> att acts on 2/3 and rel on 1/3. Shared by the
    gater, the grain repetitions and the block visuals. */
inline void normaliseAttackRelease (float& att, float& rel)
{
    att = att < 0.0f ? 0.0f : (att > 1.0f ? 1.0f : att);
    rel = rel < 0.0f ? 0.0f : (rel > 1.0f ? 1.0f : rel);
    const float sum = att + rel;
    if (sum > 1.0f)
    {
        att /= sum;
        rel /= sum;
    }
}

/** The override for `key`, or `fallback` when the block doesn't set it. */
inline float overrideOr (const BlockContext& ctx, OvKey key, float fallback)
{
    if (ctx.overrides != nullptr)
        if (const auto* e = ctx.overrides->find (key))
            return e->eval (ctx.midi);
    return fallback;
}

/** Resolve a `dur`-style override to seconds: a plain number is a fraction
    of a whole note (0.125 = eighth) at the context tempo; an expression
    containing mididur (or midifreq) is already a time in seconds. Returns
    `fallbackSeconds` when the block doesn't override the key. */
inline float overrideDurSeconds (const BlockContext& ctx, OvKey key, float fallbackSeconds)
{
    if (ctx.overrides != nullptr)
        if (const auto* e = ctx.overrides->find (key))
        {
            if (e->kind != Expr::Const)
                return e->eval (ctx.midi);
            return e->value * 4.0f * 60.0f / (float) ctx.bpm;   // whole-note fraction
        }
    return fallbackSeconds;
}

class EffectBase
{
public:
    virtual ~EffectBase() = default;

    /** Message thread; may allocate. */
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    /** Audio thread, when the playhead enters a block on this lane: draw the
        random choices, resolve overrides, trigger internal state. */
    virtual void onBlockEnter (const BlockContext& ctx) = 0;

    /** Audio thread, when the block ends (or playback stops). */
    virtual void onBlockExit() = 0;

    /** In-place processing of [startSample, startSample + numSamples); only
        called while this lane's block is active. */
    virtual void process (juce::AudioBuffer<float>& buffer,
                          int startSample, int numSamples) = 0;

    /** Destinations for effects that send audio *out* of the lane chain
        (AuxSend). The engine sets these on each lane's current effect once
        per processBlock; the buffers share the main buffer's sample
        indices, so process()'s startSample applies to them unchanged.
        Either may be nullptr — the host has that aux bus disabled — and an
        effect that doesn't send simply ignores them. Audio thread. */
    virtual void setAuxBuffers (juce::AudioBuffer<float>* /*aux1*/,
                                juce::AudioBuffer<float>* /*aux2*/) {}
};

} // namespace mng
