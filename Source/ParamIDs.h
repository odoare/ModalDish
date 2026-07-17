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
    inline constexpr const char* force     = "force";     // hammer force amplitude
    inline constexpr const char* nonlin    = "nonlin";    // Berger tension feedback amount
    inline constexpr const char* cascade   = "cascade";   // cubic feedback (mode cascade) amount

    // Cascade tuning set (exposed at least while voicing the effect).
    inline constexpr const char* cascAmp     = "cascamp";   // injection gain
    inline constexpr const char* cascDrive   = "cascdrive"; // tanh knee (B)
    inline constexpr const char* cascAttack  = "cascatt";   // gate attack, ms per band rung
    inline constexpr const char* cascRelease = "cascrel";   // gate release, ms
    inline constexpr const char* cascOverlap = "cascover";  // target bandwidth floor
    inline constexpr const char* cascWindow  = "cascwin";   // source window, bands
    inline constexpr const char* cascDeplete = "cascdepl";  // source-band energy loss
    inline constexpr const char* numModes  = "nmodes";    // active modes
    inline constexpr const char* outX      = "outx";      // output position on the plate
    inline constexpr const char* outY      = "outy";
    inline constexpr const char* inGain    = "ingain";    // external-signal drive (effect mode)
    inline constexpr const char* outGain   = "outgain";   // output level (dB)
}

namespace fem
{
    // Synthesis bank capacity: FEM-computed modes plus the statistical
    // (Berry random-wave) tail appended above them — see ModalModel.h.
    inline constexpr int maxModes = 256;
    // Modes actually solved by the finite-element eigensolver (the dense
    // solver stops being reasonable much beyond this).
    inline constexpr int maxFemModes = 128;
}
