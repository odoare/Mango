/*
  ------------------------------------------------------------------------------
    HeldNotes.h

    Which MIDI notes are sounding right now, for the mini-language's voice
    digits (`mididur2`, `midifreq3`): a note-on adds, its note-off removes.

    Polyphony is capped at MidiNoteState::kMaxVoices. A further note-on drops
    the *oldest* held note rather than being ignored, so the chord always
    follows what was played last — holding a pedal chord and playing over it
    moves the voices to the new notes instead of freezing on the old ones.

    Notes are kept in arrival order and only sorted (low to high) when
    published, so "forget the oldest" and "voice 1 is the lowest note" stay
    independent of each other.

    JUCE-free so the console unit tests cover it. Audio thread only: no
    allocation, no locks.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <algorithm>
#include <cmath>
#include "OverrideParser.h"

namespace mng
{

/** Period in seconds of a MIDI note number (A4 = note 69 = 440 Hz). */
inline float midiNotePeriod (int noteNumber)
{
    return 1.0f / (440.0f * std::pow (2.0f, (float) (noteNumber - 69) / 12.0f));
}

class HeldNotes
{
public:
    static constexpr int kMaxVoices = MidiNoteState::kMaxVoices;

    void noteOn (int noteNumber)
    {
        // A note already held stays where it is: retriggering it must not
        // cost another voice, nor make it look newer than it is.
        for (int i = 0; i < count; ++i)
            if (notes[i] == noteNumber)
                return;

        if (count == kMaxVoices)
        {
            for (int i = 1; i < kMaxVoices; ++i)   // drop the oldest
                notes[i - 1] = notes[i];
            --count;
        }
        notes[count++] = noteNumber;
    }

    void noteOff (int noteNumber)
    {
        for (int i = 0; i < count; ++i)
            if (notes[i] == noteNumber)
            {
                for (int j = i + 1; j < count; ++j)
                    notes[j - 1] = notes[j];
                --count;
                return;
            }
    }

    void allNotesOff() { count = 0; }

    int size() const { return count; }

    /** Writes the chord into `s` sorted low to high, leaving `s.last` (the
        last note played, held or not) to the caller. */
    void fill (MidiNoteState& s) const
    {
        int sorted[kMaxVoices];
        std::copy (notes, notes + count, sorted);
        std::sort (sorted, sorted + count);

        s.heldCount = count;
        for (int i = 0; i < count; ++i)
            s.held[i] = midiNotePeriod (sorted[i]);
    }

private:
    int notes[kMaxVoices] {};   // arrival order, oldest first
    int count = 0;
};

} // namespace mng
