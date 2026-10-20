/*
  ------------------------------------------------------------------------------
    OverrideParser.h

    The per-block override mini-language (Neorix-style): a block's string is
    a space-separated list of key=value assignments that locally override the
    lane's parameters, e.g.

        dur=0.125 fb=0.6
        dur=mididur/2
        v0=a v1=u

    Values are numbers, the magic variables `mididur` (the period, in
    seconds, of the last MIDI note the plugin received) and `midifreq`
    (its frequency in Hz, i.e. 1/mididur), or a simple product
    `mididur*N`, `mididur/N`, `N*mididur` (same forms for midifreq).
    Vowel keys also accept the letters a e i o u.

    The note-value helpers `s4 s8 s16 s32 s64` (straight), `t4 t8 t16 t32
    t64` (triplet), `d4 d8 d16 d32 d64` (dotted) expand to the same
    fraction-of-a-whole-note a block's own duration draw would produce for
    that note value (e.g. `s8` = 0.125, `t8` = 0.125 * 2/3), and take the
    same `*N`, `/N`, `N*` forms as the magic variables - e.g. `dur=t8*2`.
    Unlike mididur/midifreq they don't depend on runtime state, so they
    fold to a plain constant at parse time.

    A number written with a trailing `s` - `dur=1.5s` - is an absolute time
    in seconds: like mididur/midifreq it skips the whole-note/tempo
    conversion a plain number gets, but without following the MIDI input.
    Every `dur`-style key already accepts mididur/midifreq (they resolve to
    seconds directly; see overrideDurSeconds in EffectBase.h) - this is the
    same mechanism, just a fixed value instead of a MIDI-derived one.

    Both magic variables take an optional voice digit 1..4 — `mididur2`,
    `midifreq3*2` — addressing the chord currently held, sorted low to high,
    so one block can tune several lanes to different notes of the same chord.
    Without a digit they keep meaning the last note played, held or not.

    Units: `dur` follows note-value convention for plain numbers — a fraction
    of a whole note (0.25 = quarter, 0.125 = eighth, ...) resolved against
    the host tempo — while any expression containing `mididur` is a time in
    seconds. `midifreq` evaluates to Hz, meant for the frequency keys
    (f0/f1). All other keys are in their parameter's native unit.

    Parsing is strict and all-or-nothing (unknown key, malformed value ->
    std::nullopt); an empty/whitespace string is a valid empty set. Parse on
    the message thread only; the resulting ParsedOverrides is a flat POD the
    audio thread can read and evaluate without locks or allocation.

    The language itself (parsing, expressions, note-value constants) lives
    in FxmeTools (core, `midi/BlockOverrides.h`), shared with other hosts:
    this file gives it Mango's keys and its two MIDI variables, and keeps
    Mango's names for the types.

    JUCE-free so the console unit tests cover it.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <array>
#include <optional>
#include <string>

#include <FxmeTools/midi/BlockOverrides.h>

namespace mng
{

/** The MIDI note state an Expr resolves against.

    `last` is the period of the most recent note-on, which is what plain
    `mididur` / `midifreq` have always meant and still mean. `held` is the
    chord sounding right now (note-on without its note-off), sorted low to
    high, addressed by the voice digit: `mididur1` is the lowest note.

    Polyphony is capped at kMaxVoices; when a further note arrives the oldest
    held one is forgotten, so the chord always follows what was played last. */
struct MidiNoteState
{
    static constexpr int kMaxVoices = 4;

    float last = 1.0f / 440.0f;
    float held[kMaxVoices] {};
    int   heldCount = 0;

    MidiNoteState() = default;

    /** Single-note shorthand: no chord, just a last-note period. */
    MidiNoteState (float lastPeriod) : last (lastPeriod) {}

    /** The period a voice digit addresses. A voice past the end of the chord
        falls back to its highest note, and an empty chord falls back to
        `last`, so `mididur2` on a single note behaves like `mididur` instead
        of collapsing to zero and silencing whatever it drives. */
    float periodFor (int voice) const
    {
        if (voice <= 0 || heldCount <= 0)
            return last;
        return held[(voice < heldCount ? voice : heldCount) - 1];
    }

    /** The language's variables (see kMididur, kMidifreq): a period in
        seconds, or its frequency in Hz (0 with no note yet). Other
        variables (a host extending the language) resolve to 0 here. */
    float variableValue (int variable, int voice) const
    {
        const float period = periodFor (voice);
        if (variable == 0)
            return period;
        if (variable == 1)
            return period > 1.0e-6f ? 1.0f / period : 0.0f;
        return 0.0f;
    }
};

/** The variables of Mango's language, in the order of overrideLanguage()'s table. */
inline constexpr int kMididur = 0, kMidifreq = 1;

/** One override value: a plain constant, an absolute-seconds constant (a
    number written with a trailing `s`), or a scaled variable (`mididur`,
    `midifreq`, with their voice digit). Evaluate with a MidiNoteState. */
using Expr = fxme::OverrideExpr;
using ParsedOverrides = fxme::ParsedOverrides;

enum class OvKey
{
    Dur = 0, Fb, Damp, Porta, Att, Rel, AttCurve, RelCurve, Q, F0, F1, V0, V1,
    Bits, Down, Drive, Bias, Sag, Mix, Model, Mode, Fade, Amp, Width, Gain,
    Aux1, Aux2, Pass,                      // aux send levels + main passthrough
    Glide,                                 // panner: move time between positions
    W4, W8, W16, W32, Wstr, Wtrip, Wdot,   // duration probability weights
    W64,                                   // ...appended, not grouped: this enum
                                           // extends at the tail only (see §10)
    Count
};

namespace detail
{
    inline constexpr const char* keyNames[(int) OvKey::Count] = {
        "dur", "fb", "damp", "porta", "att", "rel", "attcurve", "relcurve", "q", "f0", "f1", "v0", "v1",
        "bits", "down", "drive", "bias", "sag", "mix", "model", "mode", "fade", "amp", "width", "gain",
        "aux1", "aux2", "pass", "glide",
        "w4", "w8", "w16", "w32", "wstr", "wtrip", "wdot",
        "w64"
    };

    /** constexpr string equality, so tables of keys can be checked against
        tables of parameter suffixes at compile time (see DurationWeights). */
    inline constexpr bool sameKeyName (const char* a, const char* b)
    {
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
} // namespace detail

/** Mango's language: its keys (the vowel keys also take a e i o u, the
    fxme::FormantFilter::Vowel indices) and its two MIDI variables. */
inline const fxme::OverrideLanguage& overrideLanguage()
{
    static const fxme::OverrideSymbol vowels[] = { { "a", 0.0f }, { "e", 1.0f }, { "i", 2.0f }, { "o", 3.0f }, { "u", 4.0f } };
    static const fxme::OverrideVariable variables[] = { { "mididur", MidiNoteState::kMaxVoices }, { "midifreq", MidiNoteState::kMaxVoices } };
    static const auto keys = []
    {
        std::array<fxme::OverrideKey, (size_t) OvKey::Count> k {};
        for (int i = 0; i < (int) OvKey::Count; ++i)
        {
            k[(size_t) i].name = detail::keyNames[i];
            if (i == (int) OvKey::V0 || i == (int) OvKey::V1)
            {
                k[(size_t) i].symbols = vowels;
                k[(size_t) i].numSymbols = 5;
            }
        }
        return k;
    }();
    static const fxme::OverrideLanguage language { keys.data(), (int) keys.size(), variables, 2 };
    return language;
}

/** Parses a block override string. Returns std::nullopt on any error; an
    empty (or all-whitespace) string yields a valid empty set. */
inline std::optional<ParsedOverrides> parseOverrides (const std::string& text)
{
    return fxme::parseOverrides (text, overrideLanguage());
}

} // namespace mng
