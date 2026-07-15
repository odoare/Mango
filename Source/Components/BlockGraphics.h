/*
  ------------------------------------------------------------------------------
    BlockGraphics.h

    The per-effect visuals painted inside sequencer blocks (via
    fxme::SequencerRubber's BlockPainter):

      Gater      exact gate envelope over the block: open/closed cycles with
                 the shaped attack/release edges, at the pass-0 drawn rate
      Grain      the per-repetition envelope, mirrored around the block's
                 horizontal centre (an amplitude-envelope look)
      Delay      vertical lines spaced by the delay time, decaying by the
                 feedback amount
      Distortion a stylized tanh-squashed waveform
      Quantizer  a stylized staircase waveform
      Filter     the repeating ramp, same family as the gate curves but in
                 its own colour
      RingMod    a repeating chirp at the pass-0 drawn glide rate, flattened
                 by the modulation amount
      Reverser   falling ramps, one per drawn slice (time running backwards)
      Freeze     horizontal dashed lines (a spectrum holding still)

    Everything is drawn in *beats*: durations drawn from the probability
    weights are in beats, and the block's width maps linearly onto its
    length in beats, so the picture is tempo-invariant (only the delay
    spacing needs the current bpm).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace mng::blockgfx
{

/** One gate/grain repetition cycle: `openBeats` of shaped envelope inside a
    `cycleBeats` period (gater: cycle = 2*open; grain: cycle = open). */
struct EnvShape
{
    double cycleBeats = 2.0, openBeats = 1.0;
    float  attFrac = 0.0f, relFrac = 0.0f;      // fractions of openBeats
    float  attGamma = 1.0f, relGamma = 1.0f;
};

inline float envValue (double tBeats, const EnvShape& s)
{
    if (s.cycleBeats <= 0.0 || s.openBeats <= 0.0)
        return 0.0f;
    const double pos = std::fmod (tBeats, s.cycleBeats);
    if (pos >= s.openBeats)
        return 0.0f;

    const auto frac = (float) (pos / s.openBeats);
    float env = 1.0f;
    if (s.attFrac > 0.0f && frac < s.attFrac)
        env = std::pow (frac / s.attFrac, s.attGamma);
    if (s.relFrac > 0.0f && 1.0f - frac < s.relFrac)
        env = juce::jmin (env, std::pow ((1.0f - frac) / s.relFrac, s.relGamma));
    return env;
}

/** Gater: the envelope down-up curve from the block's floor. */
inline void paintEnvCurve (juce::Graphics& g, juce::Rectangle<float> r,
                           double blockBeats, const EnvShape& shape, juce::Colour colour)
{
    if (r.getWidth() < 4.0f || blockBeats <= 0.0)
        return;

    const float top = r.getY() + 2.0f, bottom = r.getBottom() - 2.0f;
    juce::Path p;
    for (int x = 0; x <= (int) r.getWidth(); ++x)
    {
        const double t = blockBeats * x / r.getWidth();
        const float  y = bottom - envValue (t, shape) * (bottom - top);
        if (x == 0) p.startNewSubPath (r.getX(), y);
        else        p.lineTo (r.getX() + (float) x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

/** Grain: the envelope mirrored around the horizontal centre, filled. */
inline void paintMirroredEnv (juce::Graphics& g, juce::Rectangle<float> r,
                              double blockBeats, const EnvShape& shape, juce::Colour colour)
{
    if (r.getWidth() < 4.0f || blockBeats <= 0.0)
        return;

    const float mid  = r.getCentreY();
    const float half = r.getHeight() * 0.5f - 2.0f;
    const int   n    = (int) r.getWidth();

    juce::Path p;
    p.startNewSubPath (r.getX(), mid);
    for (int x = 0; x <= n; ++x)   // upper contour, left to right
        p.lineTo (r.getX() + (float) x,
                  mid - envValue (blockBeats * x / r.getWidth(), shape) * half);
    for (int x = n; x >= 0; --x)   // lower contour, back
        p.lineTo (r.getX() + (float) x,
                  mid + envValue (blockBeats * x / r.getWidth(), shape) * half);
    p.closeSubPath();

    g.setColour (colour.withMultipliedAlpha (0.35f));
    g.fillPath (p);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

/** Delay: vertical lines spaced by the delay time, heights decaying with
    the feedback. */
inline void paintDelayLines (juce::Graphics& g, juce::Rectangle<float> r,
                             double blockBeats, double delayBeats, float feedback,
                             juce::Colour colour)
{
    if (r.getWidth() < 4.0f || blockBeats <= 0.0 || delayBeats <= 1.0e-3)
        return;

    const float spacing = (float) (delayBeats / blockBeats) * r.getWidth();
    const float bottom  = r.getBottom() - 2.0f;
    const float fullH   = r.getHeight() - 4.0f;

    float h = fullH;
    for (float x = r.getX() + 2.0f; x < r.getRight() - 1.0f; x += juce::jmax (2.0f, spacing))
    {
        g.setColour (colour);
        g.drawLine (x, bottom, x, bottom - juce::jmax (2.0f, h), 1.6f);
        h *= juce::jlimit (0.05f, 0.98f, feedback);
        if (h < 1.0f)
            break;
    }
}

/** Distortion: a tanh-squashed sine, two cycles across the block. */
inline void paintDistWave (juce::Graphics& g, juce::Rectangle<float> r,
                           float driveDb, juce::Colour colour)
{
    if (r.getWidth() < 4.0f)
        return;

    const float drive = std::pow (10.0f, juce::jmax (6.0f, driveDb) * 0.05f);
    const float norm  = 1.0f / std::tanh (drive);
    const float mid   = r.getCentreY();
    const float half  = r.getHeight() * 0.5f - 2.0f;

    juce::Path p;
    for (int x = 0; x <= (int) r.getWidth(); ++x)
    {
        const float phase = juce::MathConstants<float>::twoPi * 2.0f * x / r.getWidth();
        const float y = mid - std::tanh (drive * std::sin (phase)) * norm * half;
        if (x == 0) p.startNewSubPath (r.getX(), y);
        else        p.lineTo (r.getX() + (float) x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

/** Quantizer: a staircase sine, two cycles across the block. The displayed
    level count follows the bits parameter, the horizontal sample-and-hold
    follows the downsample factor; both capped so it always reads as
    stairs. */
inline void paintStairsWave (juce::Graphics& g, juce::Rectangle<float> r,
                             float bits, float down, juce::Colour colour)
{
    if (r.getWidth() < 4.0f)
        return;

    const float levels = juce::jlimit (1.0f, 6.0f, std::pow (2.0f, bits - 1.0f));
    const float hold   = juce::jlimit (1.0f, 12.0f, down * 0.5f);
    const float mid    = r.getCentreY();
    const float half   = r.getHeight() * 0.5f - 2.0f;

    juce::Path p;
    for (int x = 0; x <= (int) r.getWidth(); ++x)
    {
        const float xs    = std::floor ((float) x / hold) * hold;
        const float phase = juce::MathConstants<float>::twoPi * 2.0f * xs / r.getWidth();
        const float y = mid - std::round (std::sin (phase) * levels) / levels * half;
        if (x == 0) p.startNewSubPath (r.getX(), y);
        else        p.lineTo (r.getX() + (float) x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

/** Ring mod: a repeating chirp — a sine whose visual density glides across
    each ramp segment (denser towards the end when the end frequency is
    above the start), with its amplitude scaled by the modulation amount
    (amp 0 = flat centre line). */
inline void paintChirpWave (juce::Graphics& g, juce::Rectangle<float> r,
                            double blockBeats, double rampBeats, bool rising,
                            float depth, juce::Colour colour)
{
    if (r.getWidth() < 4.0f || blockBeats <= 0.0 || rampBeats <= 1.0e-3)
        return;

    const float mid  = r.getCentreY();
    const float half = (r.getHeight() * 0.5f - 2.0f) * juce::jlimit (0.0f, 1.0f, depth);
    const double beatsPerPx = blockBeats / (double) r.getWidth();

    juce::Path p;
    double phase = 0.0;
    for (int x = 0; x <= (int) r.getWidth(); ++x)
    {
        double t = std::fmod ((double) x * beatsPerPx, rampBeats) / rampBeats;
        if (! rising)
            t = 1.0 - t;
        // 2 -> 9 visual cycles per ramp segment across the glide.
        const double cyclesPerBeat = 2.0 * std::pow (4.5, t) / rampBeats;
        phase += juce::MathConstants<double>::twoPi * cyclesPerBeat * beatsPerPx;

        const float y = mid - (float) std::sin (phase) * half;
        if (x == 0) p.startNewSubPath (r.getX(), y);
        else        p.lineTo (r.getX() + (float) x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

/** Freeze: horizontal dashed lines — a spectrum holding still. Line
    strength scales with the mix. */
inline void paintFreezeLines (juce::Graphics& g, juce::Rectangle<float> r,
                              float mix, juce::Colour colour)
{
    if (r.getWidth() < 4.0f)
        return;

    static constexpr float heights[] = { 0.24f, 0.42f, 0.60f, 0.78f };
    const float dashes[] = { 5.0f, 4.0f };

    g.setColour (colour.withMultipliedAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, mix)));
    for (const float h : heights)
    {
        const float y = r.getY() + h * r.getHeight();
        g.drawDashedLine (juce::Line<float> (r.getX() + 2.0f, y, r.getRight() - 2.0f, y),
                          dashes, 2, 1.2f);
    }
}

/** Filter: the repeating sweep ramp (rising when the end frequency / vowel
    is above the start), gate-curve family but drawn in its own colour. */
inline void paintRampCurve (juce::Graphics& g, juce::Rectangle<float> r,
                            double blockBeats, double rampBeats, bool rising,
                            juce::Colour colour)
{
    if (r.getWidth() < 4.0f || blockBeats <= 0.0 || rampBeats <= 1.0e-3)
        return;

    const float top = r.getY() + 2.0f, bottom = r.getBottom() - 2.0f;
    juce::Path p;
    for (int x = 0; x <= (int) r.getWidth(); ++x)
    {
        const double t    = blockBeats * x / r.getWidth();
        double       frac = std::fmod (t, rampBeats) / rampBeats;
        if (! rising)
            frac = 1.0 - frac;
        const float y = bottom - (float) frac * (bottom - top);
        if (x == 0) p.startNewSubPath (r.getX(), y);
        else        p.lineTo (r.getX() + (float) x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

} // namespace mng::blockgfx
