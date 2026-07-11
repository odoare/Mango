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

            const auto accent = theme::laneColour (i);
            row.rubber = std::make_unique<LockedRubber> (
                processor.engine.sequencerFor (i), processor.engine.lock(),
                makeBlockPainter (accent));
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
            typeBox.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
        }

    private:
        LaneRackComponent& rack;
        const int laneIndex;
        int row = 0;

        juce::ArrowButton upButton, downButton;
        juce::ComboBox typeBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
    };

    //==========================================================================
    fxme::SequencerRubber::BlockPainter makeBlockPainter (juce::Colour accent)
    {
        return [accent] (juce::Graphics& g, juce::Rectangle<int> r,
                         const fxme::SeqBlock& b, bool selected, bool playing)
        {
            auto fill = accent.withAlpha (playing ? 0.95f : selected ? 0.8f : 0.55f);
            g.setColour (fill);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);

            if (! b.content.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f, 1.0f);
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText (juce::String (b.content), r.reduced (4, 1),
                            juce::Justification::centredLeft);
            }
        };
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
    }

    struct Row
    {
        std::unique_ptr<LaneHeader>   header;
        std::unique_ptr<LockedRubber> rubber;
    };

    MangoAudioProcessor& processor;
    std::array<Row, numLanes> rows;   // indexed by lane identity

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneRackComponent)
};

} // namespace mng
