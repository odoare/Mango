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

/** Everything an effect may need when its block starts sounding. */
struct BlockContext
{
    int      laneIndex  = 0;       // stable lane identity (not display position)
    int      blockId    = -1;
    int64_t  loopIndex  = 0;       // pattern pass number on the host timeline
    uint64_t seed       = 0;       // global seed parameter
    double   sampleRate = 44100.0;
    double   bpm        = 120.0;
    float    mididurSeconds = 1.0f / 440.0f;
    const ParsedOverrides* overrides = nullptr;   // nullptr: none / parse error
};

/** The override for `key`, or `fallback` when the block doesn't set it. */
inline float overrideOr (const BlockContext& ctx, OvKey key, float fallback)
{
    if (ctx.overrides != nullptr)
        if (const auto* e = ctx.overrides->find (key))
            return e->eval (ctx.mididurSeconds);
    return fallback;
}

/** Resolve a `dur`-style override to seconds: a plain number is a fraction
    of a whole note (0.125 = eighth) at the context tempo; an expression
    containing mididur is already a time in seconds. Returns `fallbackSeconds`
    when the block doesn't override the key. */
inline float overrideDurSeconds (const BlockContext& ctx, OvKey key, float fallbackSeconds)
{
    if (ctx.overrides != nullptr)
        if (const auto* e = ctx.overrides->find (key))
        {
            if (e->kind == Expr::MididurScaled)
                return e->eval (ctx.mididurSeconds);
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
};

} // namespace mng
