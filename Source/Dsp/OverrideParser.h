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

    JUCE-free so the console unit tests cover it.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

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
};

/** One override value: a plain constant, `constant * mididur`, or
    `constant * midifreq` = constant/mididur (division by N is folded into
    the constant at parse time). `voice` is the magic variable's optional
    digit: 0 = the last note played, 1..kMaxVoices = the held chord. */
struct Expr
{
    enum Kind { Const, MididurScaled, MidifreqScaled };
    Kind  kind  = Const;
    float value = 0.0f;
    int   voice = 0;

    float eval (const MidiNoteState& midi) const
    {
        if (kind == Const)
            return value;
        const float period = midi.periodFor (voice);
        if (kind == MididurScaled)
            return value * period;
        return period > 1.0e-6f ? value / period : 0.0f;
    }
};

enum class OvKey
{
    Dur = 0, Fb, Damp, Porta, Att, Rel, AttCurve, RelCurve, Q, F0, F1, V0, V1,
    Bits, Down, Drive, Bias, Sag, Mix, Model, Mode, Fade, Amp, Width, Gain,
    Aux1, Aux2, Pass,                      // aux send levels + main passthrough
    Glide,                                 // panner: move time between positions
    W4, W8, W16, W32, Wstr, Wtrip, Wdot,   // duration probability weights
    Count
};

struct ParsedOverrides
{
    static constexpr int kMaxEntries = 16;

    struct Entry { OvKey key = OvKey::Count; Expr expr; };
    Entry entries[kMaxEntries];
    int   count = 0;

    const Expr* find (OvKey k) const
    {
        for (int i = 0; i < count; ++i)
            if (entries[i].key == k)
                return &entries[i].expr;
        return nullptr;
    }

    bool add (OvKey k, Expr e)
    {
        if (count >= kMaxEntries)
            return false;
        entries[count++] = { k, e };
        return true;
    }
};

namespace detail
{
    inline const char* keyNames[(int) OvKey::Count] = {
        "dur", "fb", "damp", "porta", "att", "rel", "attcurve", "relcurve", "q", "f0", "f1", "v0", "v1",
        "bits", "down", "drive", "bias", "sag", "mix", "model", "mode", "fade", "amp", "width", "gain",
        "aux1", "aux2", "pass", "glide",
        "w4", "w8", "w16", "w32", "wstr", "wtrip", "wdot"
    };

    inline std::optional<OvKey> keyFromString (const std::string& s)
    {
        for (int i = 0; i < (int) OvKey::Count; ++i)
            if (s == keyNames[i])
                return (OvKey) i;
        return std::nullopt;
    }

    /** Strict float: the whole string must be consumed. */
    inline std::optional<float> parseNumber (const std::string& s)
    {
        if (s.empty())
            return std::nullopt;
        char* end = nullptr;
        const float v = std::strtof (s.c_str(), &end);
        if (end != s.c_str() + s.size())
            return std::nullopt;
        return v;
    }

    /** Vowel letters map to the fxme::FormantFilter::Vowel indices. */
    inline std::optional<float> parseVowel (const std::string& s)
    {
        if (s.size() != 1)
            return std::nullopt;
        switch (s[0])
        {
            case 'a': return 0.0f;
            case 'e': return 1.0f;
            case 'i': return 2.0f;
            case 'o': return 3.0f;
            case 'u': return 4.0f;
            default:  return std::nullopt;
        }
    }

    /** A magic variable reference at `pos`: the word plus its optional voice
        digit. `len` is how much of `s` it consumed, so the caller can check
        that nothing unparsed is left over. */
    struct MagicRef { Expr::Kind kind = Expr::Const; int voice = 0; size_t len = 0; };

    inline std::optional<MagicRef> parseMagic (const std::string& s, size_t pos)
    {
        static const std::pair<std::string, Expr::Kind> kMagic[] = {
            { "mididur",  Expr::MididurScaled  },
            { "midifreq", Expr::MidifreqScaled },
        };

        for (const auto& [word, kind] : kMagic)
        {
            if (s.compare (pos, word.size(), word) != 0)
                continue;

            MagicRef m { kind, 0, word.size() };

            // Optional voice digit. Out-of-range digits are left unconsumed
            // rather than clamped, so `mididur5` fails to parse (and shows
            // red) instead of quietly meaning something else.
            if (pos + m.len < s.size())
            {
                const char c = s[pos + m.len];
                if (c >= '1' && c <= (char) ('0' + MidiNoteState::kMaxVoices))
                {
                    m.voice = c - '0';
                    ++m.len;
                }
            }
            return m;
        }
        return std::nullopt;
    }

    inline std::optional<Expr> parseExpr (const std::string& s, bool vowelKey)
    {
        if (vowelKey)
            if (const auto v = parseVowel (s))
                return Expr { Expr::Const, *v, 0 };

        // word[voice] , word[voice]*N , word[voice]/N
        if (const auto m = parseMagic (s, 0))
        {
            if (m->len == s.size())
                return Expr { m->kind, 1.0f, m->voice };

            if (s.size() > m->len + 1)
            {
                const char op = s[m->len];
                const auto n  = parseNumber (s.substr (m->len + 1));
                if (! n)
                    return std::nullopt;
                if (op == '*')
                    return Expr { m->kind, *n, m->voice };
                if (op == '/' && *n != 0.0f)
                    return Expr { m->kind, 1.0f / *n, m->voice };
            }
            return std::nullopt;
        }

        // N*word[voice]
        if (const auto star = s.find ('*'); star != std::string::npos)
            if (const auto m = parseMagic (s, star + 1))
                if (m->len == s.size() - star - 1)
                {
                    const auto n = parseNumber (s.substr (0, star));
                    if (! n)
                        return std::nullopt;
                    return Expr { m->kind, *n, m->voice };
                }

        if (const auto n = parseNumber (s))
            return Expr { Expr::Const, *n, 0 };
        return std::nullopt;
    }
} // namespace detail

/** Parses a block override string. Returns std::nullopt on any error; an
    empty (or all-whitespace) string yields a valid empty set. */
inline std::optional<ParsedOverrides> parseOverrides (const std::string& text)
{
    ParsedOverrides result;

    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n'))
            ++i;
        if (i >= text.size())
            break;

        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t' && text[j] != '\n')
            ++j;
        const std::string token = text.substr (i, j - i);
        i = j;

        const auto eq = token.find ('=');
        if (eq == std::string::npos || eq == 0 || eq == token.size() - 1)
            return std::nullopt;

        const auto key = detail::keyFromString (token.substr (0, eq));
        if (! key)
            return std::nullopt;

        const bool vowelKey = (*key == OvKey::V0 || *key == OvKey::V1);
        const auto expr = detail::parseExpr (token.substr (eq + 1), vowelKey);
        if (! expr)
            return std::nullopt;

        if (! result.add (*key, *expr))
            return std::nullopt;   // too many assignments
    }

    return result;
}

} // namespace mng
