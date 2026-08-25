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
    // Pickups. Up to four listening points, each with its own level and pan;
    // the plate sums into a stereo pair through them. Indexed 0..3 for
    // pickups labelled 1..4 in the interface, which is the only place the
    // one-based labels appear.
    inline constexpr const char* pickupX[]     = { "pickup1x",     "pickup2x",     "pickup3x",     "pickup4x"     };
    inline constexpr const char* pickupY[]     = { "pickup1y",     "pickup2y",     "pickup3y",     "pickup4y"     };
    inline constexpr const char* pickupLevel[] = { "pickup1level", "pickup2level", "pickup3level", "pickup4level" };
    inline constexpr const char* pickupPan[]   = { "pickup1pan",   "pickup2pan",   "pickup3pan",   "pickup4pan"   };
    inline constexpr const char* pickupOn[]    = { "pickup1on",    "pickup2on",    "pickup3on",    "pickup4on"    };

    // Sources. Up to eight striking/injection points, labelled a..h in the
    // interface and indexed 0..7 here. Each carries its own hammer, its own
    // MIDI note, and its own send of the plugin input.
    inline constexpr const char* sourceX[]    = { "sourceax", "sourcebx", "sourcecx", "sourcedx", "sourceex", "sourcefx", "sourcegx", "sourcehx" };
    inline constexpr const char* sourceY[]    = { "sourceay", "sourceby", "sourcecy", "sourcedy", "sourceey", "sourcefy", "sourcegy", "sourcehy" };
    inline constexpr const char* sourceHammer[] = { "sourceahammer", "sourcebhammer", "sourcechammer", "sourcedhammer", "sourceehammer", "sourcefhammer", "sourceghammer", "sourcehhammer" };
    inline constexpr const char* sourceForce[] = { "sourceaforce", "sourcebforce", "sourcecforce", "sourcedforce", "sourceeforce", "sourcefforce", "sourcegforce", "sourcehforce" };
    inline constexpr const char* sourceNote[] = { "sourceanote", "sourcebnote", "sourcecnote", "sourcednote", "sourceenote", "sourcefnote", "sourcegnote", "sourcehnote" };
    inline constexpr const char* sourceSpread[] = { "sourceaspread", "sourcebspread", "sourcecspread", "sourcedspread", "sourceespread", "sourcefspread", "sourcegspread", "sourcehspread" };
    inline constexpr const char* sourceSend[] = { "sourceasend", "sourcebsend", "sourcecsend", "sourcedsend", "sourceesend", "sourcefsend", "sourcegsend", "sourcehsend" };
    inline constexpr const char* sourcePan[]  = { "sourceapan", "sourcebpan", "sourcecpan", "sourcedpan", "sourceepan", "sourcefpan", "sourcegpan", "sourcehpan" };
    inline constexpr const char* sourceOn[]   = { "sourceaon", "sourcebon", "sourcecon", "sourcedon", "sourceeon", "sourcefon", "sourcegon", "sourcehon" };
    inline constexpr const char* inGain    = "ingain";    // external-signal drive (effect mode)
    inline constexpr const char* outGain   = "outgain";   // output level (dB)
}

namespace fem
{
    // Listening points on the plate. Four is a deliberate cap rather than a
    // technical one: the mix into the stereo pair is linear, so any number of
    // pickups collapses into two per-mode weight vectors and costs the audio
    // loop exactly the same (see PlateSynth::updatePickupMix). What four buys
    // is a plate you can place across the image; more would be knobs without
    // a use.
    inline constexpr int maxPickups = 4;

    // Striking and injection points. Eight, for the same reason as four
    // pickups: the input sends collapse into two per-mode vectors whatever
    // the count, so the audio loop does not care. What does scale with the
    // count is the mode-shape evaluation when a source moves, and the number
    // of hammers that can be in flight at once (maxStrikes).
    inline constexpr int maxSources = 8;

    /** Interface label of source `i`: 'a'..'h'. The one place the letters
        are defined, so nothing else has to know the convention. */
    inline constexpr char sourceLabel (int i) noexcept { return (char) ('a' + i); }

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
