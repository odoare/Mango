/*
  ------------------------------------------------------------------------------
    ParamIDs.h

    All Mango parameter IDs in one place. Global parameters have plain IDs;
    per-lane parameters are prefixed "l<i>_" where i is the lane *identity*
    (0..7), which never changes when lanes are visually reordered.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace mng
{
    /** The fixed maximum: parameters, engines and state always cover all
        eight lanes; the `numlanes` parameter (1..8) sets how many are shown
        and processed. */
    inline constexpr int numLanes = 8;
    inline constexpr int defaultNumLanes = 4;

    /** Maximum parallel effect buses. Rows group into contiguous buses in
        display order: row 0 always starts bus 0, and every visible row
        whose "bus start" switch is on opens the next bus (extra switches
        beyond the fourth bus are ignored). Each bus has a dry/wet and a
        pan; the `busmode` parameter picks the routing (see
        effectiveBusMode). */
    inline constexpr int numBuses = 4;

    /** Bus routing modes (`busmode`):

          0  all buses parallel from the input, outputs summed.
          1  bus 3 processes the mixed outputs of buses 1+2; bus 4, if
             present, stays parallel from the input.        (needs >= 3)
          2  bus 4 processes the mixed outputs of buses 1-3. (needs 4)
          3  diamond: bus 1 feeds buses 2 and 3 in parallel, and their mix
             feeds bus 4.                                    (needs 4)
          4  fan-out: bus 1 feeds every remaining bus in parallel, their
             outputs summed.                                 (needs >= 2)

        A mode needing more buses than exist falls back to parallel — this
        helper applies that rule (used by the engine and the GUI). Mode 4 is
        the exception that degrades rather than falls back: with three buses
        it fans out to two, with two it is a plain 1 -> 2 series. */
    inline constexpr int numBusModes = 5;

    inline int effectiveBusMode (int mode, int busCount) noexcept
    {
        if (mode == 1 && busCount >= 3) return 1;
        if (mode == 2 && busCount >= 4) return 2;
        if (mode == 3 && busCount >= 4) return 3;
        if (mode == 4 && busCount >= 2) return 4;
        return 0;
    }

    /** Slots in the sequencer-configuration bank. A config is a snapshot of
        the sequencer setup (blocks + structure), optionally including the
        effect parameters; it lives in the state tree, NOT in parameters —
        only the `config` selector is automatable. */
    inline constexpr int numConfigs = 8;

    namespace pid
    {
        // Globals
        inline constexpr const char* drywet   = "drywet";
        inline constexpr const char* seed     = "seed";
        inline constexpr const char* stepsize = "stepsize";
        inline constexpr const char* numsteps = "numsteps";
        inline constexpr const char* numlanes = "numlanes";
        inline constexpr const char* busmode  = "busmode";
        inline constexpr const char* config     = "config";      // 1..numConfigs selector
        inline constexpr const char* configsync = "configsync";  // recall timing

        /** Per-bus dry/wet, e.g. "bus2_wet" (busIndex 0-based). */
        inline juce::String busWet (int busIndex)
        {
            return "bus" + juce::String (busIndex + 1) + "_wet";
        }

        /** Per-bus pan (-1..1 balance), e.g. "bus2_pan". */
        inline juce::String busPan (int busIndex)
        {
            return "bus" + juce::String (busIndex + 1) + "_pan";
        }

        /** Prefix for one lane's parameters, e.g. lane 3 -> "l3_". */
        inline juce::String lanePrefix (int laneIndex)
        {
            return "l" + juce::String (laneIndex) + "_";
        }

        /** The lane's effect-type choice parameter, e.g. "l3_type". */
        inline juce::String laneType (int laneIndex)
        {
            return lanePrefix (laneIndex) + "type";
        }

        /** The lane's "starts a new bus" switch, e.g. "l3_busstart". */
        inline juce::String laneBusStart (int laneIndex)
        {
            return lanePrefix (laneIndex) + "busstart";
        }
    }

    /** How a parameter relates to a stored sequencer config. The split is
        by what a parameter *belongs to*, not by whether it is a "sound"
        control:

          Always   — **structure**: the shape of the rack. Grid, lane
                     count/order/types, bus grouping and topology, the
                     per-bus wet/pan, and the seed. A bus's identity is
                     defined by the config (`busstart` decides which lanes
                     are in it), so its level and pan have to travel with
                     it — exactly like the blocks travelling with the grid
                     they were drawn on.
          Optional — **voicing**: how each effect sounds, i.e. the per-lane
                     effect parameter sets. Stored only when "include
                     effect parameters" is on; per-block override strings
                     cover the same ground the other way round (typed, and
                     already carried inside the blocks).
          Never    — **performance**: what you touch during a take —
                     mute, solo, the global dry/wet — plus the config
                     selector itself. A recall must never clobber these. */
    enum class ConfigParamKind { Never, Always, Optional };

    inline ConfigParamKind configParamKind (const juce::String& id)
    {
        if (id == pid::drywet || id == pid::config || id == pid::configsync
            || id.endsWith ("_mute") || id.endsWith ("_solo"))
            return ConfigParamKind::Never;

        const bool isBusMix = id.startsWith ("bus")
                              && (id.endsWith ("_wet") || id.endsWith ("_pan"));

        if (isBusMix
            || id == pid::numlanes || id == pid::busmode || id == pid::seed
            || id == pid::stepsize || id == pid::numsteps
            || id.endsWith ("_type") || id.endsWith ("_busstart"))
            return ConfigParamKind::Always;

        return ConfigParamKind::Optional;   // per-lane effect parameters
    }
}
