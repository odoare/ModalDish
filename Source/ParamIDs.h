/*
  ------------------------------------------------------------------------------
    ParamIDs.h

    FemPlate parameter identifiers and a few shared constants.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

namespace fem::id
{
    inline constexpr const char* f1        = "f1";        // first eigenfrequency (Hz)
    inline constexpr const char* tension   = "tension";   // tension / flexural stiffness
    inline constexpr const char* viscDamp  = "viscdamp";  // viscous damping (zeta of mode 1)
    inline constexpr const char* matDamp   = "matdamp";   // material damping (zeta of mode 1)
    inline constexpr const char* hammerMs  = "hammer";    // half-sine shock duration (ms)
    inline constexpr const char* numModes  = "nmodes";    // active modes
    inline constexpr const char* outX      = "outx";      // output position on the plate
    inline constexpr const char* outY      = "outy";
    inline constexpr const char* inGain    = "ingain";    // external-signal drive (effect mode)
    inline constexpr const char* outGain   = "outgain";   // output level (dB)
}

namespace fem
{
    inline constexpr int maxModes = 64;
}
