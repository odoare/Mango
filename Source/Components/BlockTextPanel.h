/*
  ------------------------------------------------------------------------------
    BlockTextPanel.h

    The override-string editor for the selected sequencer block (three
    lines). Return commits/validates and leaves the field; Ctrl/Cmd+Return
    inserts a newline (plain whitespace to the parser); focus loss commits
    too. An unparsable string keeps a red outline and stays visible for
    fixing.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Theme.h"

namespace mng
{

class BlockTextPanel : public juce::Component
{
public:
    BlockTextPanel()
    {
        heading.setText ("block overrides", juce::dontSendNotification);
        heading.setColour (juce::Label::textColourId, theme::text);
        heading.setFont (juce::Font (juce::FontOptions (kHeadingFont, juce::Font::bold)));
        addAndMakeVisible (heading);

        theme::styleInfo (info, theme::mangoYellow);
        info.setInfo ("Block overrides",
            "Every lane has one set of knobs, but each block can override them "
            "with a line of text - so one delay lane can play a different time "
            "in every block without automating anything.\n\n"
            "Select a block and type key=value pairs separated by spaces:\n\n"
            "    dur=0.125 fb=0.6\n"
            "    dur=mididur/2\n"
            "    v0=a v1=u\n\n"
            "Each effect reads the keys it knows and ignores the rest; the "
            "panel above lists the ones the selected lane understands. A key "
            "you set here overrides that knob for this block only.\n\n"
            "It is all or nothing: if any part of the line does not make "
            "sense, none of it is applied and the field turns red. Your text "
            "is kept so you can fix it.\n\n"
            "VALUES\n\n"
            "A plain dur is a fraction of a whole note - 0.25 is a quarter, "
            "0.125 an eighth - and follows the host tempo.\n\n"
            "mididur is the length of one cycle of the last MIDI note the "
            "plugin received, in seconds, and midifreq is that note's pitch in "
            "Hz. Both can be scaled: mididur*2, mididur/4, midifreq*2. Use "
            "them to make an effect track what you play - dur=mididur on a "
            "delay tunes it to the note, f0=midifreq points a filter at it.\n\n"
            "Add a digit 1 to 4 to follow a chord instead of a single note: "
            "mididur1 is the lowest note you are holding, mididur2 the next "
            "one up, and so on (midifreq1..4 likewise). Give each lane a "
            "different voice and one chord tunes the whole rack. Without a "
            "digit they keep meaning the last note played. Up to four notes "
            "are followed; play a fifth and the oldest is dropped.\n\n"
            "Any expression using mididur is a time in seconds, so it ignores "
            "the tempo. Every other key is in the same unit as its knob.\n\n"
            "Return commits and leaves the field; Ctrl+Return adds a newline "
            "(the parser treats it as a space); clicking away commits too.\n\n"
            "REUSING A LINE\n\n"
            "Ctrl+Shift-drag a block onto another one to copy just this text "
            "across. Ctrl-drag copies the whole block, text included. To "
            "empty a block's line, shift-right-click it.");
        addAndMakeVisible (info);

        // Three lines: room for several assignments; newlines are ordinary
        // whitespace to the parser. Return commits and leaves the field;
        // Ctrl/Cmd+Return inserts a newline (see OverrideEditor); focus
        // loss commits too.
        edit.setMultiLine (true, true);
        edit.setReturnKeyStartsNewLine (false);
        edit.onReturnKey = [this]
        {
            commit();
            edit.giveAwayKeyboardFocus();
        };
        edit.setFont (juce::Font (juce::FontOptions (13.0f)));
        edit.setTextToShowWhenEmpty ("e.g.  dur=0.125 fb=0.6   or   dur=mididur/2",
                                     theme::dimText.withAlpha (0.6f));
        edit.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff181e28));
        edit.setColour (juce::TextEditor::textColourId, theme::text);
        edit.onFocusLost = [this] { commit(); };
        addAndMakeVisible (edit);

        setBlock (-1, -1, {}, false);
    }

    /** Called with the committed text; returns false when it doesn't parse. */
    std::function<bool (int laneIndex, int blockId, const juce::String&)> onCommit;

    void setBlock (int laneIndex, int blockId, const juce::String& text, bool hasError)
    {
        lane  = laneIndex;
        block = blockId;
        const bool haveBlock = laneIndex >= 0 && blockId >= 0;

        edit.setEnabled (haveBlock);
        if (! edit.hasKeyboardFocus (true))   // don't stomp typing
            edit.setText (haveBlock ? text : juce::String(), false);
        showError (haveBlock && hasError);
    }

    int laneOf() const  { return lane; }
    int blockOf() const { return block; }

    /** Heading + three text lines. */
    static constexpr float kHeadingFont   = 13.5f;
    static constexpr int   kHeadingHeight = 19;
    static constexpr int   kHeight = kHeadingHeight + 58;

    void resized() override
    {
        auto r = getLocalBounds();

        auto headingRow = r.removeFromTop (kHeadingHeight);
        const auto font = juce::Font (juce::FontOptions (kHeadingFont, juce::Font::bold));
        const int textWidth = juce::GlyphArrangement::getStringWidthInt (font, heading.getText());
        heading.setBounds (headingRow.removeFromLeft (textWidth + 4));
        headingRow.removeFromLeft (theme::infoGap - 4);
        info.setBounds (headingRow.removeFromLeft (theme::infoSize)
                                  .withSizeKeepingCentre (theme::infoSize, theme::infoSize));

        edit.setBounds (r.removeFromTop (58));
    }

private:
    /** Multiline TextEditor where plain Return commits (returnKeyStartsNewLine
        is off, so it fires onReturnKey) and Ctrl/Cmd+Return inserts the
        newline instead. */
    struct OverrideEditor : juce::TextEditor
    {
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::returnKey
                && (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
            {
                insertTextAtCaret ("\n");
                return true;
            }
            return juce::TextEditor::keyPressed (key);
        }
    };

    void commit()
    {
        if (lane < 0 || block < 0 || onCommit == nullptr)
            return;
        showError (! onCommit (lane, block, edit.getText().trim()));
    }

    void showError (bool error)
    {
        if (error)
            edit.setColour (juce::TextEditor::outlineColourId, juce::Colours::red);
        else
            edit.removeColour (juce::TextEditor::outlineColourId);
        edit.repaint();
    }

    juce::Label heading;
    fxme::InfoButton info;
    OverrideEditor edit;
    int lane = -1, block = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockTextPanel)
};

} // namespace mng
