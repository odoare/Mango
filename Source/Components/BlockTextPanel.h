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
        heading.setColour (juce::Label::textColourId, theme::dimText);
        heading.setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (heading);

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
    static constexpr int kHeight = 16 + 58;

    void resized() override
    {
        auto r = getLocalBounds();
        heading.setBounds (r.removeFromTop (16));
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
    OverrideEditor edit;
    int lane = -1, block = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockTextPanel)
};

} // namespace mng
