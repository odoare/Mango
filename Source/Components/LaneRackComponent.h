/*
  ------------------------------------------------------------------------------
    LaneRackComponent.h

    The centre of the Mango editor: a step ruler on top and the lane rows below,
    one per sequencer lane, laid out in the engine's display order. Each row
    is a lane header (up/down reorder arrows, accent swatch, effect-type
    box) plus a rubber sequencer strip.

    The rubbers mutate the shared StringSequencers directly, so every mouse
    or key gesture is wrapped in the engine lock (LockedRubber); block
    add/delete/clear also re-parse the override cache.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Theme.h"
#include "../Dsp/DurationWeights.h"
#include "BlockGraphics.h"
#include "../Dsp/Effects/PannerEffect.h"   // panStateAt: the visual follows the DSP

namespace mng
{

class LaneRackComponent : public juce::Component,
                          private juce::Timer
{
public:
    static constexpr int kHeaderWidth = 140;
    static constexpr int kRulerHeight = 16;

    explicit LaneRackComponent (MangoAudioProcessor& p) : processor (p)
    {
        refreshBusCache();

        for (int i = 0; i < numLanes; ++i)
        {
            auto& row = rows[(size_t) i];
            row.header = std::make_unique<LaneHeader> (*this, i);
            addAndMakeVisible (*row.header);

            row.rubber = std::make_unique<LockedRubber> (
                processor.engine.sequencerFor (i), processor.engine.lock(),
                makeBlockPainter (i));
            // Always fit the whole pattern: the lanes, the ruler and the
            // playhead must stay aligned whatever the step count.
            row.rubber->setMinPixelsPerStep (1);
            addAndMakeVisible (*row.rubber);

            row.rubber->onBlockSelected = [this, i] (int blockId)
            {
                if (blockId >= 0)
                    deselectAllExcept (i);
                if (onBlockSelected)
                    onBlockSelected (i, blockId);
            };
            row.rubber->onBlockDeleted = [this, i] (int)
            {
                processor.engine.rebuildOverrides();
                if (onBlockSelected)
                    onBlockSelected (i, -1);
            };
            row.rubber->onBlockContentCleared = [this, i] (int blockId)
            {
                processor.engine.rebuildOverrides();
                if (onBlockContentChanged)
                    onBlockContentChanged (i, blockId);
            };
            // Duplicate / content copy: same refresh, the content just came
            // from another block instead of being emptied.
            row.rubber->onBlockContentChanged = [this, i] (int blockId)
            {
                processor.engine.rebuildOverrides();
                if (onBlockContentChanged)
                    onBlockContentChanged (i, blockId);
            };
            // A copy drag that left its own strip: only the rack can see the
            // other lanes, so it owns the target hunt, the rules and the ghost.
            row.rubber->onCopyDragMoved = [this, i] (const fxme::SequencerRubber::CopyDrag& d)
            {
                updateCopyGhost (i, d);
            };
            row.rubber->onCopyDropped = [this, i] (const fxme::SequencerRubber::CopyDrag& d)
            {
                commitCrossLaneCopy (i, d);
            };
        }
        startTimerHz (30);
    }

    /** (laneIndex identity, blockId or -1) */
    std::function<void (int, int)> onBlockSelected;
    /** A block's string changed from the rubber: cleared (alt-right-click),
        or copied in from another block (ctrl-drag, ctrl-shift-drag, ctrl-D). */
    std::function<void (int, int)> onBlockContentChanged;
    /** The lane->bus assignment changed (reorder, bus switch, lane count):
        the editor re-accents the effect panels from colourOfLane(). */
    std::function<void()> onBusMapChanged;

    /** The lane's accent = its bus colour, shaded by the lane's position
        inside the bus (the bus's first lane gets the pure colour). */
    juce::Colour colourOfLane (int laneIndex) const
    {
        const auto lane = (size_t) juce::jlimit (0, numLanes - 1, laneIndex);
        return theme::busColour (busOfLane[lane], depthOfLane[lane]);
    }

    void refreshOrder() { resized(); repaint(); }

    void deselectAllExcept (int laneIndex)
    {
        for (int i = 0; i < numLanes; ++i)
            if (i != laneIndex)
                rows[(size_t) i].rubber->selectBlock (-1);
    }

    /** Shows a selection that came from somewhere other than a click (the
        restored GUI state). SequencerRubber::selectBlock is silent, so this
        does not bounce back through onBlockSelected. */
    void showSelection (int laneIndex, int blockId)
    {
        deselectAllExcept (laneIndex);
        if (laneIndex >= 0 && laneIndex < numLanes)
            rows[(size_t) laneIndex].rubber->selectBlock (blockId);
    }

    void paint (juce::Graphics& g) override
    {
        // Step ruler, aligned with the rubbers.
        auto ruler = getLocalBounds().removeFromTop (kRulerHeight);
        ruler.removeFromLeft (kHeaderWidth + 4);

        int numSteps = 16;
        {
            const juce::ScopedLock sl (processor.engine.lock());
            numSteps = processor.engine.sequencerFor (0).getNumSteps();
        }

        g.setColour (theme::dimText);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        const float stepW = (float) ruler.getWidth() / (float) juce::jmax (1, numSteps);
        for (int s = 0; s < numSteps; s += 4)
            g.drawText (juce::String (s + 1),
                        ruler.getX() + (int) (s * stepW), ruler.getY(),
                        (int) (stepW * 4.0f), ruler.getHeight(),
                        juce::Justification::bottomLeft);
    }

    /** The drop ghost of a copy drag that has left its own lane. Drawn here
        rather than in the target rubber because the drag never leaves the
        component it started in, so no rubber can paint the answer. */
    void paintOverChildren (juce::Graphics& g) override
    {
        if (! copyGhost.active)
            return;

        const auto& r = *rows[(size_t) copyGhost.laneIndex].rubber;
        auto local = copyGhost.contentOnly
                   ? r.rectForBlock (copyGhost.targetBlockId)
                   : r.rectForSteps (copyGhost.startStep,
                                     copyGhost.startStep + copyGhost.lengthSteps);
        if (local.isEmpty())
            return;   // content copy over empty space: nothing would happen

        const auto rect = local.translated (r.getX(), r.getY());
        const auto c    = copyGhost.valid ? juce::Colours::white : theme::mangoRed;
        g.setColour (c.withAlpha (0.22f));
        g.fillRect (rect);
        g.setColour (c.withAlpha (0.85f));
        g.drawRect (rect, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (kRulerHeight);

        const auto laneOrder = processor.engine.laneOrder();
        visibleRows = processor.engine.visibleLaneCount();
        const int rowH = r.getHeight() / visibleRows;

        for (int rowIdx = 0; rowIdx < numLanes; ++rowIdx)
        {
            auto& row = rows[(size_t) laneOrder[(size_t) rowIdx]];
            const bool shown = rowIdx < visibleRows;
            row.header->setVisible (shown);
            row.rubber->setVisible (shown);
            if (! shown)
                continue;

            auto rowArea = r.removeFromTop (rowH).reduced (0, 2);
            row.header->setRow (rowIdx, visibleRows);
            row.header->setBounds (rowArea.removeFromLeft (kHeaderWidth));
            rowArea.removeFromLeft (4);
            row.rubber->setBounds (rowArea);
        }
    }

private:
    //==========================================================================
    /** fxme::SequencerRubber mutates the shared StringSequencer from its
        mouse/keyboard handlers; this wrapper takes the engine lock around
        each of them (the audio thread reads under the same lock). */
    struct LockedRubber : fxme::SequencerRubber
    {
        LockedRubber (fxme::StringSequencer& s, juce::CriticalSection& cs, BlockPainter painter)
            : SequencerRubber (s, std::move (painter)), lock (cs) {}

        void mouseDown (const juce::MouseEvent& e) override { const juce::ScopedLock sl (lock); SequencerRubber::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { const juce::ScopedLock sl (lock); SequencerRubber::mouseDrag (e); }
        void mouseUp   (const juce::MouseEvent& e) override { const juce::ScopedLock sl (lock); SequencerRubber::mouseUp (e); }
        bool keyPressed (const juce::KeyPress& k) override  { const juce::ScopedLock sl (lock); return SequencerRubber::keyPressed (k); }

        juce::CriticalSection& lock;
    };

    //==========================================================================
    class LaneHeader : public juce::Component
    {
    public:
        LaneHeader (LaneRackComponent& r, int laneIdx)
            : rack (r), laneIndex (laneIdx),
              upButton ("up", 0.75f, theme::text),
              downButton ("down", 0.25f, theme::text)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                    rack.processor.apvts.getParameter (pid::laneType (laneIndex))))
                typeBox.addItemList (choice->choices, 1);
            addAndMakeVisible (typeBox);
            typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                rack.processor.apvts, pid::laneType (laneIndex), typeBox);

            for (auto* b : { &upButton, &downButton })
            {
                addAndMakeVisible (*b);
                b->setMouseClickGrabsKeyboardFocus (false);
            }
            upButton.onClick   = [this] { rack.moveRow (row, -1); };
            downButton.onClick = [this] { rack.moveRow (row, +1); };

            const auto prefix = pid::lanePrefix (laneIndex);
            setupToggle (muteButton, "M", juce::Colour (0xffd9b13a));
            setupToggle (soloButton, "S", juce::Colour (0xff9ac93c));
            setupToggle (busButton,  "B", theme::text);
            muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                rack.processor.apvts, prefix + "mute", muteButton);
            soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                rack.processor.apvts, prefix + "solo", soloButton);
            busAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                rack.processor.apvts, pid::laneBusStart (laneIndex), busButton);

            refreshAccent();
        }

        /** Re-applies the lane's current bus colour (swatch, combo, B). */
        void refreshAccent()
        {
            const auto accent = rack.colourOfLane (laneIndex);
            theme::styleCombo (typeBox, accent);
            busButton.setColour (juce::TextButton::buttonOnColourId, accent);
            repaint();
        }

        void setRow (int newRow, int visibleRowCount)
        {
            row = newRow;
            upButton.setEnabled (row > 0);
            downButton.setEnabled (row < visibleRowCount - 1);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (rack.colourOfLane (laneIndex));
            g.fillRoundedRectangle (getLocalBounds().removeFromLeft (5).toFloat(), 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromLeft (8);   // accent swatch
            auto arrows = r.removeFromLeft (18).withSizeKeepingCentre (14, 34);
            upButton.setBounds (arrows.removeFromTop (16));
            downButton.setBounds (arrows.removeFromBottom (16));
            r.removeFromLeft (2);

            auto ms = r.removeFromRight (22).withSizeKeepingCentre (20, 58);
            muteButton.setBounds (ms.removeFromTop (19).reduced (0, 1));
            soloButton.setBounds (ms.removeFromTop (20).reduced (0, 1));
            busButton.setBounds  (ms.removeFromTop (19).reduced (0, 1));
            r.removeFromRight (2);

            typeBox.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
        }

    private:
        /** The square letter toggles (M / S / B). */
        using MiniToggle = fxme::AccentToggle;

        void setupToggle (fxme::AccentToggle& b, const juce::String& text, juce::Colour accent)
        {
            b.setButtonText (text);
            b.setAccent (accent, theme::text);
            addAndMakeVisible (b);
        }

        LaneRackComponent& rack;
        const int laneIndex;
        int row = 0;

        juce::ArrowButton upButton, downButton;
        MiniToggle muteButton, soloButton, busButton;
        juce::ComboBox typeBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt, busAtt;
    };

    //==========================================================================
    fxme::SequencerRubber::BlockPainter makeBlockPainter (int laneIndex)
    {
        return [this, laneIndex] (juce::Graphics& g, juce::Rectangle<int> r,
                                  const fxme::SeqBlock& b, bool selected, bool playing)
        {
            const auto accent = colourOfLane (laneIndex);
            g.setColour (accent.withAlpha (playing ? 0.95f : selected ? 0.8f : 0.55f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);

            paintEffectVisual (g, r.toFloat().reduced (2.0f), laneIndex, b);

            if (! b.content.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f, 1.0f);

                // The block's override string (newlines flattened), with a
                // soft dark backing so it stays readable on the visuals.
                const auto text = juce::String (b.content).replace ("\n", "  ");
                g.setFont (juce::Font (juce::FontOptions (12.5f)));
                const auto textArea = r.reduced (5, 2);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawText (text, textArea.translated (1, 1), juce::Justification::topLeft);
                g.setColour (juce::Colours::white);
                g.drawText (text, textArea, juce::Justification::topLeft);
            }
        };
    }

    /** Paints the effect-specific picture inside one block, from the lane's
        parameters, the block's overrides and — for the weight-drawn
        durations — the actual pass-0 deterministic draw. */
    void paintEffectVisual (juce::Graphics& g, juce::Rectangle<float> r,
                            int laneIndex, const fxme::SeqBlock& b)
    {
        auto& apvts = processor.apvts;
        const auto prefix = pid::lanePrefix (laneIndex);
        auto param = [&] (const char* suffix)
        {
            return apvts.getRawParameterValue (prefix + suffix)->load();
        };

        // Benign unlocked reads: GUI-only, a transient stale value at worst.
        const double stepBeats  = processor.engine.sequencerFor (laneIndex).getStepSizeBeats();
        const double blockBeats = (b.endStep - b.startStep) * stepBeats;
        const double bpm        = processor.engine.guiBpm();
        const auto   midi       = processor.engine.guiMidiNotes();

        std::optional<ParsedOverrides> ov;
        if (! b.content.empty())
            ov = parseOverrides (b.content);

        auto ovOr = [&] (OvKey k, float fallback)
        {
            if (ov)
                if (const auto* e = ov->find (k))
                    return e->eval (midi);
            return fallback;
        };
        auto ovDurBeats = [&] (double fallbackBeats)
        {
            if (ov)
                if (const auto* e = ov->find (OvKey::Dur))
                    return e->kind != Expr::Const
                         ? (double) e->eval (midi) * bpm / 60.0   // seconds -> beats
                         : (double) e->value * 4.0;                  // whole-note fraction
            return fallbackBeats;
        };

        // The duration this block draws on pass 0 (same hash as the engine).
        auto drawnBeats = [&] (const char* weightPrefix)
        {
            // DurationWeights owns the key tables: a local copy here was left
            // at four entries when 1/64 was added and read out of bounds.
            fxme::WeightedDurationTable t;
            for (int i = 0; i < fxme::kNumNoteBases; ++i)
                t.baseWeights[i] = ovOr (DurationWeights::baseKeys[i],
                    apvts.getRawParameterValue (prefix + weightPrefix
                                                + DurationWeights::baseSuffixes[i])->load());
            for (int i = 0; i < fxme::kNumNoteMods; ++i)
                t.modWeights[i] = ovOr (DurationWeights::modKeys[i],
                    apvts.getRawParameterValue (prefix + weightPrefix
                                                + DurationWeights::modSuffixes[i])->load());
            const auto seed = (uint64_t) (int64_t) apvts.getRawParameterValue (pid::seed)->load();
            return t.drawBeats (fxme::detrand::u01 (seed, (uint64_t) laneIndex,
                                                    (uint64_t) b.id, 0, 0));
        };

        const auto curveColour  = juce::Colours::white.withAlpha (0.85f);
        const auto filterColour = juce::Colour (0xff14101a).withAlpha (0.85f);

        switch ((EffectType) (int) apvts.getRawParameterValue (pid::laneType (laneIndex))->load())
        {
            case EffectType::Gater:
            {
                const double dur = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("gate_")));
                blockgfx::EnvShape s;
                s.cycleBeats = 2.0 * dur;
                s.openBeats  = dur;
                s.attFrac    = ovOr (OvKey::Att, param ("gate_att"));
                s.relFrac    = ovOr (OvKey::Rel, param ("gate_rel"));
                normaliseAttackRelease (s.attFrac, s.relFrac);
                s.attGamma   = attackGammaFor (ovOr (OvKey::AttCurve, param ("gate_attcurve")));
                s.relGamma   = releaseGammaFor (ovOr (OvKey::RelCurve, param ("gate_relcurve")));
                blockgfx::paintEnvCurve (g, r, blockBeats, s, curveColour);
                break;
            }

            case EffectType::Grain:
            {
                const double dur = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("grain_")));
                blockgfx::EnvShape s;
                s.cycleBeats = dur;   // repetitions are contiguous
                s.openBeats  = dur;
                s.attFrac    = ovOr (OvKey::Att, param ("grain_att"));
                s.relFrac    = ovOr (OvKey::Rel, param ("grain_rel"));
                normaliseAttackRelease (s.attFrac, s.relFrac);
                s.attGamma   = attackGammaFor (ovOr (OvKey::AttCurve, param ("grain_attcurve")));
                s.relGamma   = releaseGammaFor (ovOr (OvKey::RelCurve, param ("grain_relcurve")));
                blockgfx::paintMirroredEnv (g, r, blockBeats, s, curveColour);
                break;
            }

            case EffectType::Delay:
            {
                double delayBeats = param ("dly_dur") * bpm / 60.0;
                if (ov)
                    if (const auto* e = ov->find (OvKey::Dur))
                        delayBeats = e->kind != Expr::Const
                                   ? (double) e->eval (midi) * bpm / 60.0
                                   : (double) e->value * 4.0;
                const float fb = ovOr (OvKey::Fb, param ("dly_fb"));
                blockgfx::paintDelayLines (g, r, blockBeats, delayBeats, fb, curveColour);
                break;
            }

            case EffectType::Distortion:
                blockgfx::paintDistWave (g, r, ovOr (OvKey::Drive, param ("dist_drive")),
                                         curveColour);
                break;

            case EffectType::FilterEnv:
            {
                const double ramp = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("flt_")));
                const bool formant = (int) ovOr (OvKey::Mode, param ("flt_mode")) == 2;
                const bool rising = formant
                    ? ovOr (OvKey::V1, param ("flt_v1")) >= ovOr (OvKey::V0, param ("flt_v0"))
                    : ovOr (OvKey::F1, param ("flt_f1")) >= ovOr (OvKey::F0, param ("flt_f0"));
                blockgfx::paintRampCurve (g, r, blockBeats, ramp, rising, filterColour);
                break;
            }

            case EffectType::Quantizer:
                blockgfx::paintStairsWave (g, r, ovOr (OvKey::Bits, param ("qnt_bits")),
                                           ovOr (OvKey::Down, param ("qnt_down")),
                                           curveColour);
                break;

            case EffectType::RingMod:
            {
                const double ramp = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("ring_")));
                const bool rising = ovOr (OvKey::F1, param ("ring_f1"))
                                 >= ovOr (OvKey::F0, param ("ring_f0"));
                blockgfx::paintChirpWave (g, r, blockBeats, ramp, rising,
                                          ovOr (OvKey::Amp, param ("ring_amp")),
                                          curveColour);
                break;
            }

            case EffectType::Reverser:
            {
                // Falling ramps, one per slice: time running backwards.
                const double slice = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("rev_")));
                blockgfx::paintRampCurve (g, r, blockBeats, slice, false, curveColour);
                break;
            }

            case EffectType::Freeze:
                blockgfx::paintFreezeLines (g, r, ovOr (OvKey::Mix, param ("frz_mix")),
                                            curveColour);
                break;

            case EffectType::AuxSend:
            {
                const double dur = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("aux_")));
                blockgfx::EnvShape s;
                s.cycleBeats = 2.0 * dur;
                s.openBeats  = dur;
                s.attFrac    = ovOr (OvKey::Att, param ("aux_att"));
                s.relFrac    = ovOr (OvKey::Rel, param ("aux_rel"));
                normaliseAttackRelease (s.attFrac, s.relFrac);
                s.attGamma   = attackGammaFor (ovOr (OvKey::AttCurve, param ("aux_attcurve")));
                s.relGamma   = releaseGammaFor (ovOr (OvKey::RelCurve, param ("aux_relcurve")));
                const float send = juce::jmax (ovOr (OvKey::Aux1, param ("aux_send1")),
                                               ovOr (OvKey::Aux2, param ("aux_send2")));
                blockgfx::paintSendEnv (g, r, blockBeats, s, send,
                                        ovOr (OvKey::Pass, param ("aux_pass")), curveColour);
                break;
            }

            case EffectType::Panner:
            {
                const double step = juce::jmax (1.0e-3, ovDurBeats (drawnBeats ("pan_")));
                const auto mode = (PanMode) juce::jlimit (0, 3,
                    (int) ovOr (OvKey::Mode, param ("pan_mode")));
                const auto seed = (uint64_t) (int64_t) apvts.getRawParameterValue (pid::seed)->load();
                blockgfx::paintPanSteps (g, r, blockBeats, step,
                                         ovOr (OvKey::Glide, param ("pan_glide")),
                                         [&] (int64_t k) { return panStateAt (mode, seed, laneIndex, b.id, k); },
                                         curveColour);
                break;
            }
        }
    }

    void moveRow (int row, int delta)
    {
        processor.engine.moveRow (row, delta);
        refreshOrder();
    }

    void timerCallback() override
    {
        // Follow the lane-count parameter (buttons, automation, state load).
        if (processor.engine.visibleLaneCount() != visibleRows)
            refreshOrder();

        // Follow the bus layout: recolour rows when it changes (including
        // reorders inside a bus, which only change the shading depths).
        if (refreshBusCache())
        {
            for (auto& row : rows)
            {
                row.header->refreshAccent();
                row.rubber->repaint();
            }
            if (onBusMapChanged)
                onBusMapChanged();
        }

        for (int i = 0; i < numLanes; ++i)
        {
            rows[(size_t) i].rubber->setPlayheadStep (processor.engine.guiPlayheadStep (i));
            rows[(size_t) i].rubber->setActiveBlockId (processor.engine.guiActiveBlock (i));
        }

        // The block visuals follow the lane parameters; refresh them at
        // ~10 Hz so knob moves show up even while the transport is stopped.
        if (++visualTick >= 3)
        {
            visualTick = 0;
            for (auto& row : rows)
                row.rubber->repaint();
        }
    }

    struct Row
    {
        std::unique_ptr<LaneHeader>   header;
        std::unique_ptr<LockedRubber> rubber;
    };

    //==========================================================================
    // Cross-lane copy drags.
    //
    // The rules differ by gesture on purpose. An override string is just
    // text, and a key an effect does not use is ignored, so it may go to any
    // lane. A whole block carries that text as its meaning, so it may only
    // land on a lane running the SAME effect: `dur=mididur fb=0.99` copied
    // onto a bit crusher would be a block that silently does nothing like the
    // one it came from.
    struct CopyGhost
    {
        bool active         = false;
        bool contentOnly    = false;
        bool valid          = false;
        int  laneIndex      = -1;    // lane identity under the cursor
        int  startStep      = 0;
        int  lengthSteps    = 1;
        int  targetBlockId  = -1;    // content copy: block under the cursor
    };

    CopyGhost copyGhost;

    int effectTypeOf (int laneIndex) const
    {
        return (int) processor.apvts.getRawParameterValue (pid::laneType (laneIndex))->load();
    }

    /** The lane whose strip is under `screenPos`, or -1. */
    int laneAtScreenPos (juce::Point<int> screenPos) const
    {
        const auto p = getLocalPoint (nullptr, screenPos);
        for (int i = 0; i < numLanes; ++i)
        {
            const auto& rubber = *rows[(size_t) i].rubber;
            if (! rubber.isVisible())
                continue;

            // The row band, not the exact bounds: rows are inset by 2 px, and
            // matching bounds alone would blink the ghost off in the gap every
            // time a drag crosses from one lane to the next.
            const auto b = rubber.getBounds();
            if (p.x >= b.getX() && p.x < b.getRight()
                && p.y >= b.getY() - 2 && p.y < b.getBottom() + 2)
                return i;
        }
        return -1;
    }

    /** Recomputes the ghost for a copy drag in progress. Called on every drag
        move, including while the cursor is still home, so the ghost clears
        the moment the drag comes back to its own lane. */
    void updateCopyGhost (int sourceLane, const fxme::SequencerRubber::CopyDrag& d)
    {
        const bool wasActive = copyGhost.active;
        copyGhost = {};

        const int target = d.insideSource ? -1 : laneAtScreenPos (d.screenPos);
        if (target >= 0 && target != sourceLane)
        {
            auto& rubber = *rows[(size_t) target].rubber;
            const auto local = rubber.getLocalPoint (nullptr, d.screenPos);

            copyGhost.active      = true;
            copyGhost.contentOnly = d.contentOnly;
            copyGhost.laneIndex   = target;
            copyGhost.lengthSteps = d.lengthSteps;
            // Resolved against the TARGET's geometry, not the source's.
            copyGhost.startStep   = rubber.stepAtX (local.x) - d.grabOffset;

            const juce::ScopedLock sl (processor.engine.lock());
            if (d.contentOnly)
            {
                copyGhost.targetBlockId = rubber.blockIdAt (local);
                copyGhost.valid         = copyGhost.targetBlockId >= 0;
            }
            else
            {
                copyGhost.valid = effectTypeOf (sourceLane) == effectTypeOf (target)
                               && processor.engine.sequencerFor (target)
                                      .canPlaceBlock (copyGhost.startStep, copyGhost.lengthSteps);
            }
        }

        if (copyGhost.active || wasActive)
            repaint();
    }

    /** Commits a copy drag dropped on another lane, if the drop is legal.
        Runs under the engine lock already taken by LockedRubber::mouseUp
        (juce::CriticalSection is recursive). */
    void commitCrossLaneCopy (int sourceLane, const fxme::SequencerRubber::CopyDrag& d)
    {
        updateCopyGhost (sourceLane, d);   // the drop point is the truth
        const auto g = copyGhost;
        copyGhost = {};
        repaint();

        if (! g.active || ! g.valid)
            return;

        auto& src = processor.engine.sequencerFor (sourceLane);
        auto& dst = processor.engine.sequencerFor (g.laneIndex);

        const auto* srcBlock = src.blockById (d.sourceBlockId);
        if (srcBlock == nullptr)
            return;
        const std::string content = srcBlock->content;   // before any mutation

        int changed = -1;
        if (g.contentOnly)
        {
            if (dst.setContent (g.targetBlockId, content))
                changed = g.targetBlockId;
        }
        else if (const int id = dst.addBlock (g.startStep, g.lengthSteps); id >= 0)
        {
            dst.setContent (id, content);
            changed = id;
        }

        if (changed < 0)
            return;

        processor.engine.rebuildOverrides();
        if (onBlockContentChanged)
            onBlockContentChanged (g.laneIndex, changed);

        // A duplicated block becomes the selection, as it does within a lane;
        // a text copy leaves the selection alone, since the target block was
        // already there and the user is not "on" it.
        if (! g.contentOnly)
        {
            deselectAllExcept (g.laneIndex);
            rows[(size_t) g.laneIndex].rubber->selectBlock (changed);
            if (onBlockSelected)
                onBlockSelected (g.laneIndex, changed);
        }
        rows[(size_t) g.laneIndex].rubber->repaint();
    }

    /** Re-derives lane -> (bus, position-in-bus) from the engine; true if
        anything changed and the rack needs recolouring. */
    bool refreshBusCache()
    {
        const auto map   = processor.engine.busMapByLane();
        const auto order = processor.engine.laneOrder();

        std::array<int, numLanes> depth {};
        int countInBus[numBuses] = {};
        for (int row = 0; row < numLanes; ++row)
        {
            const auto lane = (size_t) order[(size_t) row];
            depth[lane] = countInBus[map[lane]]++;
        }

        if (map == busOfLane && depth == depthOfLane)
            return false;
        busOfLane   = map;
        depthOfLane = depth;
        return true;
    }

    MangoAudioProcessor& processor;
    std::array<Row, numLanes> rows;   // indexed by lane identity
    std::array<int, numLanes> busOfLane {};     // cached lane -> bus map
    std::array<int, numLanes> depthOfLane {};   // cached lane -> position in its bus
    int visualTick  = 0;
    int visibleRows = numLanes;       // cached view of the numlanes parameter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneRackComponent)
};

} // namespace mng
