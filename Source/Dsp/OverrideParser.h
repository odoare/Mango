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

/** One override value: a plain constant, `constant * mididur`, or
    `constant * midifreq` = constant/mididur (division by N is folded into
    the constant at parse time). */
struct Expr
{
    enum Kind { Const, MididurScaled, MidifreqScaled };
    Kind  kind  = Const;
    float value = 0.0f;

    float eval (float mididurSeconds) const
    {
        if (kind == MididurScaled)
            return value * mididurSeconds;
        if (kind == MidifreqScaled)
            return mididurSeconds > 1.0e-6f ? value / mididurSeconds : 0.0f;
        return value;
    }
};

enum class OvKey
{
    Dur = 0, Fb, Damp, Porta, Att, Rel, AttCurve, RelCurve, Q, F0, F1, V0, V1,
    Bits, Down, Drive, Bias, Sag, Mix, Model, Mode, Fade, Amp, Width, Gain,
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

    inline std::optional<Expr> parseExpr (const std::string& s, bool vowelKey)
    {
        if (vowelKey)
            if (const auto v = parseVowel (s))
                return Expr { Expr::Const, *v };

        static const std::pair<std::string, Expr::Kind> kMagic[] = {
            { "mididur",  Expr::MididurScaled  },
            { "midifreq", Expr::MidifreqScaled },
        };

        for (const auto& [word, kind] : kMagic)
        {
            if (s == word)
                return Expr { kind, 1.0f };

            // word*N / word/N
            if (s.rfind (word, 0) == 0 && s.size() > word.size() + 1)
            {
                const char op = s[word.size()];
                const auto n  = parseNumber (s.substr (word.size() + 1));
                if (! n)
                    return std::nullopt;
                if (op == '*')
                    return Expr { kind, *n };
                if (op == '/' && *n != 0.0f)
                    return Expr { kind, 1.0f / *n };
                return std::nullopt;
            }

            // N*word
            const auto star = s.find ('*');
            if (star != std::string::npos && s.substr (star + 1) == word)
            {
                const auto n = parseNumber (s.substr (0, star));
                if (! n)
                    return std::nullopt;
                return Expr { kind, *n };
            }
        }

        if (const auto n = parseNumber (s))
            return Expr { Expr::Const, *n };
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
