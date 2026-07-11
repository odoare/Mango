/*
  ------------------------------------------------------------------------------
    ParamIDs.h

    All Mango parameter IDs in one place. Global parameters have plain IDs;
    per-lane parameters are prefixed "l<i>_" where i is the lane *identity*
    (0..5), which never changes when lanes are visually reordered.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace mng
{
    inline constexpr int numLanes = 6;

    namespace pid
    {
        // Globals
        inline constexpr const char* drywet   = "drywet";
        inline constexpr const char* seed     = "seed";
        inline constexpr const char* stepsize = "stepsize";
        inline constexpr const char* numsteps = "numsteps";

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
    }
}
