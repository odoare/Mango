/*
  ------------------------------------------------------------------------------
    ConfigPanel.h

    The sequencer-configuration bank, shown in the right column in place of
    the lane's effect panel. Two rows of eight numbered slots — "load config
    from" and "store config to" — plus the store options (include effect
    parameters, undo) and the recall timing.

    Slot appearance carries the state: filled = the active config, outlined =
    stored, dim = empty; a dot marks the active config as edited since it was
    loaded/stored, and an armed slot (waiting for a bar/pattern boundary)
    pulses in the accent colour. Alt-click a store slot to clear it — the same
    gesture that deletes a sequencer block.

    All bank logic lives in the processor (state tree, not parameters); this
    panel only drives it and polls for the indicators.

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

class ConfigPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit ConfigPanel (MangoAudioProcessor& p) : processor (p)
    {
        for (int i = 0; i < numConfigs; ++i)
        {
            auto& load = loadSlots[(size_t) i];
            load.setButtonText (juce::String (i + 1));
            load.setMouseClickGrabsKeyboardFocus (false);
            load.onClick = [this, i] { processor.requestConfigRecall (i); refresh(); };
            addAndMakeVisible (load);

            auto& store = storeSlots[(size_t) i];
            store.setButtonText (juce::String (i + 1));
            store.setMouseClickGrabsKeyboardFocus (false);
            store.isStoreSlot = true;
            store.onClick = [this, i]
            {
                if (juce::ModifierKeys::getCurrentModifiers().isAltDown())
                    processor.clearConfig (i);
                else
                    processor.storeConfig (i, processor.configIncludeParams());
                refresh();
            };
            addAndMakeVisible (store);
        }

        includeParamsButton.setButtonText ("Include effect parameters");
        includeParamsButton.setAccent (theme::globalAccent, theme::text);
        includeParamsButton.onClick = [this]
        {
            processor.setConfigIncludeParams (includeParamsButton.getToggleState());
        };
        addAndMakeVisible (includeParamsButton);

        undoButton.setButtonText ("Undo store");
        undoButton.setMouseClickGrabsKeyboardFocus (false);
        undoButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2b2b));
        undoButton.setColour (juce::TextButton::textColourOffId, theme::text);
        undoButton.onClick = [this] { processor.undoStore(); refresh(); };
        addAndMakeVisible (undoButton);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (pid::configsync)))
            syncBox.addItemList (choice->choices, 1);
        theme::styleCombo (syncBox, theme::globalAccent);
        addAndMakeVisible (syncBox);
        syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.apvts, pid::configsync, syncBox);

        syncLabel.setText ("Recall", juce::dontSendNotification);
        syncLabel.setColour (juce::Label::textColourId, theme::dimText);
        syncLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (syncLabel);

        refresh();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (theme::globalAccent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setColour (theme::text);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("Sequencer configs", r.removeFromTop (22).reduced (8, 0),
                    juce::Justification::centredLeft);

        g.setColour (theme::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText (loadCaption, loadCaptionArea, juce::Justification::centredLeft);
        g.drawText ("Store config to", storeCaptionArea, juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawFittedText ("Configs hold the blocks, grid, lane setup and seed."
                          "  Alt-click a store slot to clear it.",
                          hintArea, juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromTop (22);   // title

        loadCaptionArea = r.removeFromTop (16);
        layoutSlots (loadSlots, r.removeFromTop (26));
        r.removeFromTop (8);

        storeCaptionArea = r.removeFromTop (16);
        layoutSlots (storeSlots, r.removeFromTop (26));
        r.removeFromTop (10);

        includeParamsButton.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);

        auto syncRow = r.removeFromTop (24);
        syncLabel.setBounds (syncRow.removeFromLeft (48));
        syncBox.setBounds (syncRow.removeFromLeft (120));
        syncRow.removeFromLeft (6);
        undoButton.setBounds (syncRow);

        r.removeFromTop (8);
        hintArea = r.removeFromTop (40);
    }

private:
    /** Numbered bank slot: empty / stored / active / armed, painted so the
        two rows read at a glance. */
    struct SlotButton : juce::TextButton
    {
        bool stored = false, active = false, armed = false, modified = false;
        bool isStoreSlot = false;

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            const auto accent = theme::globalAccent;

            if (active && ! isStoreSlot)
            {
                g.setColour (highlighted ? accent.brighter (0.2f) : accent);
                g.fillRoundedRectangle (b, 3.0f);
            }
            else
            {
                g.setColour (juce::Colour (0xff2b2b2b).withAlpha (highlighted ? 1.0f : 0.85f));
                g.fillRoundedRectangle (b, 3.0f);
                if (stored)
                {
                    g.setColour (accent.withAlpha (armed ? 1.0f : 0.7f));
                    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, armed ? 2.0f : 1.0f);
                }
            }

            g.setColour (active && ! isStoreSlot ? juce::Colours::black
                       : stored                  ? theme::text
                                                 : theme::dimText.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);

            // Edited-since-load dot on the active load slot.
            if (modified && active && ! isStoreSlot)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.fillEllipse (b.getRight() - 6.0f, b.getY() + 2.0f, 4.0f, 4.0f);
            }
        }
    };

    void timerCallback() override
    {
        if (isVisible())   // the signature walk is cheap, but not free
            refresh();
    }

    void refresh()
    {
        const int  active   = processor.activeConfig();
        const int  armed    = processor.armedConfig();
        const bool modified = processor.configIsModified();

        for (int i = 0; i < numConfigs; ++i)
        {
            const bool stored = processor.configIsStored (i);
            for (auto* row : { &loadSlots, &storeSlots })
            {
                auto& s = (*row)[(size_t) i];
                if (s.stored != stored || s.active != (i == active)
                    || s.armed != (i == armed) || s.modified != modified)
                {
                    s.stored   = stored;
                    s.active   = (i == active);
                    s.armed    = (i == armed);
                    s.modified = modified;
                    s.repaint();
                }
            }
            loadSlots[(size_t) i].setEnabled (stored);
        }

        undoButton.setEnabled (processor.canUndoStore());
        includeParamsButton.setToggleState (processor.configIncludeParams(),
                                            juce::dontSendNotification);

        const auto caption = armed >= 0
            ? "Load config from   (" + juce::String (armed + 1) + " armed...)"
            : juce::String ("Load config from");
        if (caption != loadCaption)
        {
            loadCaption = caption;
            repaint (loadCaptionArea);
        }
    }

    static void layoutSlots (std::array<SlotButton, numConfigs>& slots, juce::Rectangle<int> row)
    {
        const int w = row.getWidth() / numConfigs;
        for (auto& s : slots)
            s.setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    MangoAudioProcessor& processor;

    std::array<SlotButton, numConfigs> loadSlots, storeSlots;
    fxme::AccentToggle includeParamsButton;
    juce::TextButton undoButton;          // momentary, not a latching toggle
    juce::ComboBox syncBox;
    juce::Label syncLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAtt;

    juce::String loadCaption { "Load config from" };
    juce::Rectangle<int> loadCaptionArea, storeCaptionArea, hintArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfigPanel)
};

} // namespace mng
