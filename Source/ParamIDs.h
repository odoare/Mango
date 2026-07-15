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
        beyond the fourth bus are ignored). Buses each process the plugin
        input and are summed at the output. */
    inline constexpr int numBuses = 4;

    namespace pid
    {
        // Globals
        inline constexpr const char* drywet   = "drywet";
        inline constexpr const char* seed     = "seed";
        inline constexpr const char* stepsize = "stepsize";
        inline constexpr const char* numsteps = "numsteps";
        inline constexpr const char* numlanes = "numlanes";

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
