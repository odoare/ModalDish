/*
  ------------------------------------------------------------------------------
    PlateSynth.h

    Modal synthesis of the FEM plate: one resonant band-pass biquad per mode
    (the MechanOdd resonator approach). Given the modal model (frequencies,
    tension coefficients, mass-normalised shapes) the signal fed to filter k
    for a hit at point P is  phi_k(P) * hammer(t), and the output is read at
    the output point O through  phi_k(O) * filter_k(...). An external input
    signal is injected the same way at the last hit point, which makes the
    plugin usable as an effect.

    Frequency / damping laws (see doc/starting_spec.md and
    FxmeTools/acoustics/PlateModes.h):

        omega_k(T)^2 = lambda_k + (T - Tref) g_k          (scaled units)
        nu_k         = omega_k / omega_1                  (frequency ratios)
        f_k          = f1 * nu_k                          (Hz; f1 is a knob)
        zeta_k       = zetaV / nu_k + zetaM * nu_k

    zetaV is the viscous-damping knob (a term in d w/dt: constant absolute
    bandwidth, so relative damping falls with frequency) and zetaM the
    material-damping knob (a term in Delta^2 dw/dt ~ omega^2 dw/dt: damping
    grows with frequency). Both equal the damping ratio of mode 1.

    Everything here runs on the audio thread and is allocation-free; the
    model pointer is published by the processor (see PluginProcessor).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include "ModalModel.h"

namespace fem
{

class PlateSynth
{
public:
    static constexpr int maxStrikes = 8;

    struct Params
    {
        float f1        = 110.0f;   // Hz
        float tension   = 0.0f;     // T (scaled units)
        float viscDamp  = 1.0e-3f;  // zeta of mode 1, viscous term
        float matDamp   = 1.0e-4f;  // zeta of mode 1, material term
        float hammerMs  = 3.0f;     // half-sine shock duration
        float outX      = 0.5f;     // output point (plate coordinates)
        float outY      = 0.47f;
        int   numModes  = 32;       // active modes (<= model modes, <= fem::maxModes)
    };

    void prepare (double sampleRate);
    void reset();

    /** Audio thread, once per block: applies a possibly-new model and the
        current parameters, retuning the filter bank only when something
        relevant changed. */
    void update (const ModalModel* model, const Params& params);

    /** Audio thread: hit the plate at (x, y) (plate coordinates) with
        normalised velocity 0..1. Also moves the external-input injection
        point there. */
    void strike (float x, float y, float velocity);

    /** Audio thread: one sample of external input in, one plate sample out. */
    float processSample (float input) noexcept;

private:
    void retune();
    void computeOutputWeights();
    void computeInputWeights (float x, float y);

    // Allocation-free RBJ band-pass (constant 0 dB peak), one per mode.
    struct Resonator
    {
        fxme::BiquadCoeffs c;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x) noexcept
        {
            const float y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    struct Hammer
    {
        float weights[fem::maxModes] {};
        float phase = 0.0f;       // 0..1 over the shock
        float phaseInc = 0.0f;
        float velocity = 0.0f;
        bool active = false;
    };

    double fs = 44100.0;
    const ModalModel* model = nullptr;
    Params current;
    bool dirty = true;

    int activeModes = 0;
    Resonator filters[fem::maxModes];
    float outAmp[fem::maxModes] {};      // phi_k(out) * bandwidth compensation
    float inWeights[fem::maxModes] {};   // phi_k(last hit) for the external input
    float lastHitX = 0.5f, lastHitY = 0.5f;
    Hammer hammers[maxStrikes];
    int nextHammer = 0;
};

} // namespace fem
