/*
  ------------------------------------------------------------------------------
    ConfigPanel.h

    The sequencer-configuration bank, shown in the right column in place of
    the lane's effect panel. Two rows of eight numbered slots — "load config
    from" and "store config to" — plus the store options (include effect
    parameters, undo) and the recall timing.

    Each slot carries its own point on the identity ramp (slot 1 yellow
    through to slot 8 red), so the row reads as the colour code and a slot
    number has a colour you can learn.

    Slot appearance carries the state: filled = the active config, outlined =
    stored, dim = empty; an underline marks a config stored *with* the effect
    parameters (recalling it changes the sound, not just the pattern), a dot
    marks the active config as edited since it was loaded/stored, and an armed
    slot (waiting for a bar/pattern boundary) takes a thicker outline.
    Alt-click a store slot to clear it — the same gesture that deletes a
    sequencer block.

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
            // The eight slots spread the identity ramp, so a slot number is
            // also a position on the colour code.
            const auto slotColour = theme::mangoAt ((float) i / (float) (numConfigs - 1));

            auto& load = loadSlots[(size_t) i];
            load.slotAccent = slotColour;
            load.setButtonText (juce::String (i + 1));
            load.setMouseClickGrabsKeyboardFocus (false);
            load.onClick = [this, i] { processor.requestConfigRecall (i); refresh(); };
            addAndMakeVisible (load);

            auto& store = storeSlots[(size_t) i];
            store.slotAccent = slotColour;
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
        includeParamsButton.setAccent (theme::mangoRed, theme::text);
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
        theme::styleCombo (syncBox, theme::mangoRed);
        addAndMakeVisible (syncBox);
        syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.apvts, pid::configsync, syncBox);

        syncLabel.setText ("Recall", juce::dontSendNotification);
        syncLabel.setColour (juce::Label::textColourId, theme::dimText);
        syncLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (syncLabel);

        theme::styleInfo (info, theme::mangoRed);
        info.setInfo ("Sequencer configs",
            "Eight slots holding whole sequencer setups, so you can build "
            "several arrangements and switch between them while playing.\n\n"
            "Click a slot in the bottom row to store what you have now; click "
            "one in the top row to load it back. Slots show their state: "
            "filled is the one you are on, outlined means something is stored, "
            "dim means empty, and a dot means you have edited since loading "
            "it. Alt-click a store slot to clear it, and Undo store takes back "
            "the last overwrite.\n\n"
            "WHAT A CONFIG HOLDS\n\n"
            "A config is a pattern, not a sound. It always carries the blocks "
            "and their override text, the grid, how many lanes there are and "
            "in what order, which effect each lane runs, the whole bus wiring "
            "with each bus's wet and pan, and the seed.\n\n"
            "It does not carry the effect knob values unless you tick Include "
            "effect parameters before storing. Slots holding them are "
            "underlined, so loading one is never a surprise. Leaving it off "
            "means you can recall an arrangement without losing the sound you "
            "have been dialling in.\n\n"
            "Mute, solo and the global Dry/Wet are never stored, so recalling "
            "cannot trample your live mix.\n\n"
            "TIMING\n\n"
            "Recall decides when a load takes effect: immediately, or waiting "
            "for the next bar or the next pattern so the switch lands in time. "
            "A slot waiting its turn is outlined more thickly.\n\n"
            "Loading the slot you are already on reloads it - a way to throw "
            "away edits and go back to what was stored.\n\n"
            "The slot number is also a plugin parameter, so your host can "
            "automate which config is playing.");
        addAndMakeVisible (info);

        refresh();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (theme::mangoRed.withAlpha (0.6f));
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
        g.drawFittedText ("Configs hold the blocks, grid, lane setup, routing and seed."
                          "  An underlined slot also carries the effect parameters."
                          "  Alt-click a store slot to clear it.",
                          hintArea, juce::Justification::topLeft, 4);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromTop (22);   // title

        // Laid out in the row paint() draws the title in — the *unpadded* top
        // 22 px — so the button lines up with the text instead of sitting
        // 8 px below it.
        auto titleRow = getLocalBounds().removeFromTop (22).reduced (8, 0);
        const auto titleFont = juce::Font (juce::FontOptions (14.0f, juce::Font::bold));
        titleRow.removeFromLeft (
            juce::GlyphArrangement::getStringWidthInt (titleFont, "Sequencer configs")
            + theme::infoGap);
        info.setBounds (titleRow.removeFromLeft (theme::infoSize)
                                .withSizeKeepingCentre (theme::infoSize, theme::infoSize));

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
        bool hasParams = false;   // stored with the effect parameters
        bool isStoreSlot = false;
        juce::Colour slotAccent { theme::mangoYellow };   // its point on the ramp

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            const auto accent = slotAccent;

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

            // Underline: this config also carries the effect parameters, so
            // recalling it changes the sound, not just the pattern.
            if (stored && hasParams)
            {
                g.setColour (active && ! isStoreSlot ? juce::Colours::black.withAlpha (0.55f)
                                                     : accent.withAlpha (0.9f));
                g.fillRoundedRectangle (b.getX() + 3.0f, b.getBottom() - 3.5f,
                                        b.getWidth() - 6.0f, 2.0f, 1.0f);
            }

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
            const bool stored    = processor.configIsStored (i);
            const bool hasParams = processor.configHasParams (i);
            for (auto* row : { &loadSlots, &storeSlots })
            {
                auto& s = (*row)[(size_t) i];
                if (s.stored != stored || s.active != (i == active)
                    || s.armed != (i == armed) || s.modified != modified
                    || s.hasParams != hasParams)
                {
                    s.stored    = stored;
                    s.active    = (i == active);
                    s.armed     = (i == armed);
                    s.modified  = modified;
                    s.hasParams = hasParams;
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

    fxme::InfoButton info;
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
