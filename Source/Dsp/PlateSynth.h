/*
  ------------------------------------------------------------------------------
    PlateSynth.h

    Modal synthesis of the FEM plate: one resonant band-pass biquad per mode
    (the MechanOdd resonator approach). Given the modal model (frequencies,
    tension coefficients, mass-normalised shapes) the signal fed to filter k
    for a hit at point P is  phi_k(P) * force * hammer(t), and the output is
    read at the output point O through  phi_k(O) * filter_k(...). An external
    input signal is injected the same way at the last hit point, which makes
    the plugin usable as an effect.

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

    Geometric nonlinearity (Berger / von Karman, simplified)
    --------------------------------------------------------
    * Dynamic tension ("nonlin" knob): Berger's approximation replaces the
      von Karman membrane coupling by a spatially uniform tension
      proportional to the integrated stretching  ~ integral |grad w|^2
      = sum_kl q_k q_l phi_k' G phi_l  ~=  sum_k g_k q_k^2  (diagonal
      approximation) — the same g_k the linear tension law already uses.
      Per-mode energies are envelope-followed on the audio thread and the
      resulting T_dyn drives the ordinary retune law at a decimated rate,
      throttled and slewed so coefficient rewrites stay inaudible. Large
      hits therefore glide the whole spectrum up and relax as the plate
      rings out (gong / tom pitch bend).

    * Mode cascade ("cascade" knob): a memoryless cubic of the output,
      tanh-bounded, is re-injected at the last hit point like the external
      input. At high amplitude its intermodulation products excite high
      modes (cymbal-like crash brightness); at low amplitude it vanishes.

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
        float force     = 1.0f;     // hammer force amplitude
        float nonlin    = 0.0f;     // 0..1 dynamic-tension (Berger) amount
        float cascade   = 0.0f;     // 0..1 cubic-feedback amount
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
        normalised velocity 0..1 (scaled by the force parameter). Also moves
        the external-input injection point there. */
    void strike (float x, float y, float velocity);

    /** Audio thread: one sample of external input in, one plate sample out. */
    float processSample (float input) noexcept;

    /** Current dynamic (Berger) tension, for display/debugging. */
    float getDynamicTension() const noexcept { return (float) tensionDyn; }

private:
    void retune();                    // full: mode count, weights, then bank
    void retuneBank();                // coefficients/amps at tension + tensionDyn
    void computeOutputWeights();      // phi_k(out) -> phiOut
    void computeInputWeights (float x, float y);
    void updateDynamicTension() noexcept;   // decimated Berger feedback step

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
        float amplitude = 0.0f;   // velocity * force at strike time
        bool active = false;
    };

    double fs = 44100.0;
    const ModalModel* model = nullptr;
    Params current;
    bool dirty = true;

    int activeModes = 0;
    Resonator filters[fem::maxModes];
    float phiOut[fem::maxModes] {};      // phi_k(out)
    float outAmp[fem::maxModes] {};      // phi_k(out) * bandwidth compensation
    float inWeights[fem::maxModes] {};   // phi_k(last hit) for input + cascade
    float lastHitX = 0.5f, lastHitY = 0.5f;
    Hammer hammers[maxStrikes];
    int nextHammer = 0;

    // Nonlinear state (Berger dynamic tension + cubic cascade).
    static constexpr int nlUpdatePeriod = 32;   // samples between T_dyn steps
    float env[fem::maxModes] {};         // smoothed squared modal responses
    float nlWeight[fem::maxModes] {};    // g_k / omega_k^2 at the applied tension
    float maxNlWeight = 0.0f;
    float envCoef = 0.001f;
    double tensionDyn = 0.0;             // slewed dynamic tension
    double appliedTensionDyn = 0.0;      // value the bank is currently tuned at
    int nlCountdown = nlUpdatePeriod;
    float prevOut = 0.0f;
};

} // namespace fem
