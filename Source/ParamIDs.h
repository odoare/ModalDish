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
    // Modes actually solved by the finite-element eigensolver. What limits
    // this in practice is the Grid knob rather than the number here: the time
    // grows with n^3 and with the requested count, for n = vertices + edges.
    // Measured on the default plate, single core, i7-10810U, back when the
    // solver held all four matrices densely (4 n^2 doubles):
    //     grid 16 (n =  554,   9 MB):  92 modes in  0.5 s
    //     grid 24 (n = 1243,  47 MB): 207 modes in  6.4 s
    //     grid 32 (n = 2195, 147 MB): 256 modes in 51 s
    //     grid 40 (n = 3440, 361 MB): 128 modes in 75 s
    //     grid 48 (n = 4913, 737 MB): the top of the Grid range
    // The three assembled matrices are sparse now (about eleven non-zeros per
    // row whatever the mesh density) and only the shifted operator is still
    // factorised densely, so those footprints are down to a third — see
    // solverBytesEstimate below — and the times to about half. Measured back
    // to back, same mesh both ways, identical eigenvalues to every digit:
    //     n = 2149,  64 modes:  13.1 s / 125 MB  ->   7.3 s /  36 MB
    //     n = 3841, 128 modes: 114.8 s / 419 MB  ->  59.9 s / 122 MB
    //     n = 6029, 128 modes: 335.5 s / 1.0 GB  -> 171.0 s / 287 MB
    // Grid still stops at 48: one step further (64) is n = 8742, and while
    // 688 MB is no longer out of reach, the dense factorisation and the
    // O(n p^2) projections still put it at tens of minutes, which is not a
    // setting anyone would wait for. Lifting it wants a sparse factorisation
    // (bandwidth-reducing ordering plus a profile Cholesky), not a bigger cap.
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
        the way the editor needs it — before pressing Compute — from the same
        three terms the solver actually allocates:

            n^2 doubles   the dense Cholesky factor of A + sigma M
            4 p n doubles the subspace iteration block, p = modes + max(8, modes/2)
            ~320 n bytes  the three sparse operators and their shared pattern

        ModalResult::solverBytes reports the measured figure afterwards; these
        two agree to a few percent. Before the matrices went sparse the first
        term was 4 n^2 rather than n^2, which is the whole of the difference. */
    inline double solverBytesEstimate (int n) noexcept
    {
        const double dn = (double) n;
        const int modes = femModeCount (n);
        const double p = (double) (modes + std::max (8, modes / 2));
        return dn * dn * 8.0 + 4.0 * p * dn * 8.0 + 320.0 * dn;
    }
}
