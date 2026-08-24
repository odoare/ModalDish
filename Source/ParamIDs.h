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

#include <algorithm>
#include <cmath>

namespace fem::id
{
    inline constexpr const char* f1        = "f1";        // first eigenfrequency (Hz)
    inline constexpr const char* tension   = "tension";   // tension / flexural stiffness
    inline constexpr const char* viscDamp  = "viscdamp";  // viscous damping (zeta of mode 1)
    inline constexpr const char* matDamp   = "matdamp";   // material damping (zeta of mode 1)
    inline constexpr const char* hammerMs  = "hammer";    // half-sine shock duration (ms)
    inline constexpr const char* force     = "force";     // hammer force amplitude
    inline constexpr const char* glide     = "glide";     // portamento time between notes (ms)
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
    inline constexpr int maxModes = 512;
    // Modes actually solved by the finite-element eigensolver. This is now the
    // binding limit rather than the Grid knob: the solver's working set and
    // its Rayleigh-Ritz projections both grow with the mode count, and only
    // linearly with the mesh. Measured on a square plate, i7-10810U, dense
    // storage against the current sparse-plus-profile solver:
    //     n =  2149,  64 modes:  13.1 s / 125 MB  ->  0.45 s /   7 MB
    //     n =  3841, 128 modes: 114.8 s / 419 MB  ->   3.6 s /  25 MB
    //     n =  6029, 128 modes: 335.5 s / 1.0 GB  ->   5.5 s /  39 MB
    //     n =  8577, 256 modes:  (2.8 GB, would not run)  35.3 s / 107 MB
    //     n = 15381, 256 modes:  (7.6 GB, would not run)  48.4 s / 196 MB
    // The Grid knob still stops at 48 (n = 4913 on the default plate), which
    // the old dense solver set because it needed 737 MB there. That reason is
    // gone — n = 15381 now solves 256 modes in under a minute — so the cap is
    // worth revisiting as a product decision rather than a numerical one.
    inline constexpr int maxFemModes = 256;

    /** Modes the eigensolver is asked for on a mesh with n = vertices + edges
        free degrees of freedom. Never more than the mesh can resolve: the top
        discrete modes need several DOFs per half-wave to be physical, about
        six in practice. The Modes knob then selects among them without
        recomputing, and the statistical tail fills the bank above them. */
    inline int femModeCount (int n) noexcept
    {
        return std::min (maxFemModes, std::max (8, n / 6));
    }

    /** Peak working set of one solve at that mesh size, in bytes. Estimated
        the way the editor needs it — before pressing Compute — from the three
        terms the solver actually allocates:

            32 p n bytes    the subspace iteration block, 4 vectors of p x n,
                            for a block of p = modes + max(8, modes/2)
            ~8.9 n^1.5      the profile Cholesky factor of A + sigma M. The
                            mean row bandwidth of these meshes after the
                            reverse Cuthill-McKee renumbering measures
                            1.11 sqrt(n), within a few percent from n = 554 to
                            n = 19700, which is where the exponent comes from
            ~400 n bytes    the three sparse operators, their shared pattern
                            and the shifted copy

        Note which term leads: the iteration block, which grows with the number
        of modes asked for and only linearly with the mesh. Nothing here is
        quadratic in n any more, which is why a mesh that used to be measured
        in gigabytes is now measured in tens of megabytes.

        ModalResult::solverBytes reports the measured figure afterwards. These
        two agree to about 10%, this one being the higher — the right direction
        for a warning shown before the fact. */
    inline double solverBytesEstimate (int n) noexcept
    {
        const double dn = (double) n;
        const int modes = femModeCount (n);
        const double p = (double) (modes + std::max (8, modes / 2));
        return 32.0 * p * dn + 8.9 * dn * std::sqrt (dn) + 400.0 * dn;
    }
}
