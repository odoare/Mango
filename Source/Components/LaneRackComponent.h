/*
  ------------------------------------------------------------------------------
    LaneRackComponent.h

    The centre of the Mango editor: a step ruler on top and six rows below,
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
        for (int i = 0; i < numLanes; ++i)
        {
            auto& row = rows[(size_t) i];
            row.header = std::make_unique<LaneHeader> (*this, i);
            addAndMakeVisible (*row.header);

            row.rubber = std::make_unique<LockedRubber> (
                processor.engine.sequencerFor (i), processor.engine.lock(),
                makeBlockPainter (i));
            // Always fit the whole pattern: the six lanes, the ruler and the
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
        }
        startTimerHz (30);
    }

    /** (laneIndex identity, blockId or -1) */
    std::function<void (int, int)> onBlockSelected;
    /** A block's string was cleared from the rubber (right-click). */
    std::function<void (int, int)> onBlockContentChanged;

    void refreshOrder() { resized(); repaint(); }

    void deselectAllExcept (int laneIndex)
    {
        for (int i = 0; i < numLanes; ++i)
            if (i != laneIndex)
                rows[(size_t) i].rubber->selectBlock (-1);
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

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (kRulerHeight);

        const auto laneOrder = processor.engine.laneOrder();
        const int rowH = r.getHeight() / numLanes;

        for (int rowIdx = 0; rowIdx < numLanes; ++rowIdx)
        {
            auto rowArea = r.removeFromTop (rowH).reduced (0, 2);
            auto& row = rows[(size_t) laneOrder[(size_t) rowIdx]];
            row.header->setRow (rowIdx);
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
            const auto accent = theme::laneColour (laneIndex);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                    rack.processor.apvts.getParameter (pid::laneType (laneIndex))))
                typeBox.addItemList (choice->choices, 1);
            theme::styleCombo (typeBox, accent);
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
            muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                rack.processor.apvts, prefix + "mute", muteButton);
            soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                rack.processor.apvts, prefix + "solo", soloButton);
        }

        void setRow (int newRow)
        {
            row = newRow;
            upButton.setEnabled (row > 0);
            downButton.setEnabled (row < numLanes - 1);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::laneColour (laneIndex));
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

            auto ms = r.removeFromRight (22).withSizeKeepingCentre (20, 42);
            muteButton.setBounds (ms.removeFromTop (20));
            soloButton.setBounds (ms.removeFromBottom (20));
            r.removeFromRight (2);

            typeBox.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
        }

    private:
        /** A square letter toggle small enough for the lane header (the
            stock LookAndFeel text indents leave no room at this size). */
        struct MiniToggle : juce::TextButton
        {
            void paintButton (juce::Graphics& g, bool highlighted, bool) override
            {
                const bool on = getToggleState();
                auto bg = findColour (on ? buttonOnColourId : buttonColourId);
                g.setColour (highlighted ? bg.brighter (0.2f) : bg);
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
                g.setColour (findColour (on ? textColourOnId : textColourOffId));
                g.setFont (juce::Font (juce::FontOptions ((float) getHeight() * 0.62f,
                                                          juce::Font::bold)));
                g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
            }
        };

        void setupToggle (juce::TextButton& b, const juce::String& text, juce::Colour accent)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setMouseClickGrabsKeyboardFocus (false);
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2b2b));
            b.setColour (juce::TextButton::buttonOnColourId, accent);
            b.setColour (juce::TextButton::textColourOffId, theme::text);
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            addAndMakeVisible (b);
        }

        LaneRackComponent& rack;
        const int laneIndex;
        int row = 0;

        juce::ArrowButton upButton, downButton;
        MiniToggle muteButton, soloButton;
        juce::ComboBox typeBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
    };

    //==========================================================================
    fxme::SequencerRubber::BlockPainter makeBlockPainter (int laneIndex)
    {
        return [this, laneIndex] (juce::Graphics& g, juce::Rectangle<int> r,
                                  const fxme::SeqBlock& b, bool selected, bool playing)
        {
            const auto accent = theme::laneColour (laneIndex);
            g.setColour (accent.withAlpha (playing ? 0.95f : selected ? 0.8f : 0.55f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);

            paintEffectVisual (g, r.toFloat().reduced (2.0f), laneIndex, b);

            if (! b.content.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f, 1.0f);

                // The block's override string, with a soft dark backing so it
                // stays readable on top of the visuals.
                g.setFont (juce::Font (juce::FontOptions (12.5f)));
                const auto textArea = r.reduced (5, 2);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawText (juce::String (b.content), textArea.translated (1, 1),
                            juce::Justification::topLeft);
                g.setColour (juce::Colours::white);
                g.drawText (juce::String (b.content), textArea,
                            juce::Justification::topLeft);
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
        const float  mididur    = processor.engine.guiMididur();

        std::optional<ParsedOverrides> ov;
        if (! b.content.empty())
            ov = parseOverrides (b.content);

        auto ovOr = [&] (OvKey k, float fallback)
        {
            if (ov)
                if (const auto* e = ov->find (k))
                    return e->eval (mididur);
            return fallback;
        };
        auto ovDurBeats = [&] (double fallbackBeats)
        {
            if (ov)
                if (const auto* e = ov->find (OvKey::Dur))
                    return e->kind == Expr::MididurScaled
                         ? (double) e->eval (mididur) * bpm / 60.0   // seconds -> beats
                         : (double) e->value * 4.0;                  // whole-note fraction
            return fallbackBeats;
        };

        // The duration this block draws on pass 0 (same hash as the engine).
        auto drawnBeats = [&] (const char* weightPrefix)
        {
            static constexpr OvKey baseKeys[] = { OvKey::W4, OvKey::W8, OvKey::W16, OvKey::W32 };
            static constexpr OvKey modKeys[]  = { OvKey::Wstr, OvKey::Wtrip, OvKey::Wdot };
            fxme::WeightedDurationTable t;
            for (int i = 0; i < fxme::kNumNoteBases; ++i)
                t.baseWeights[i] = ovOr (baseKeys[i],
                    apvts.getRawParameterValue (prefix + weightPrefix
                                                + DurationWeights::baseSuffixes[i])->load());
            for (int i = 0; i < fxme::kNumNoteMods; ++i)
                t.modWeights[i] = ovOr (modKeys[i],
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
                        delayBeats = e->kind == Expr::MididurScaled
                                   ? (double) e->eval (mididur) * bpm / 60.0
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
                                           curveColour);
                break;
        }
    }

    void moveRow (int row, int delta)
    {
        processor.engine.moveRow (row, delta);
        refreshOrder();
    }

    void timerCallback() override
    {
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

    MangoAudioProcessor& processor;
    std::array<Row, numLanes> rows;   // indexed by lane identity
    int visualTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneRackComponent)
};

} // namespace mng
