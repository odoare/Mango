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

    /** Bus routing modes (`busmode`): 0 = all buses parallel; 1 = bus 3
        processes the mixed outputs of buses 1+2 (bus 4, if present, stays
        parallel); 2 = bus 4 processes the mixed outputs of buses 1–3.
        A mode needing more buses than exist falls back to parallel — this
        helper applies that rule (used by the engine and the GUI). */
    inline int effectiveBusMode (int mode, int busCount) noexcept
    {
        if (mode == 1 && busCount >= 3) return 1;
        if (mode == 2 && busCount >= 4) return 2;
        return 0;
    }

    namespace pid
    {
        // Globals
        inline constexpr const char* drywet   = "drywet";
        inline constexpr const char* seed     = "seed";
        inline constexpr const char* stepsize = "stepsize";
        inline constexpr const char* numsteps = "numsteps";
        inline constexpr const char* numlanes = "numlanes";
        inline constexpr const char* busmode  = "busmode";

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
}
