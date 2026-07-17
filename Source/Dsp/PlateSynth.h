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

    Frequency / damping laws (see doc/technical.tex and
    FxmeTools/acoustics/PlateModes.h):

        omega_k(T)^2 = lambda_k + (T - Tref) g_k          (scaled units)
        f_k          = f1 * omega_k(T_eff) / omega_1(T_knob)
        zeta_k       = zetaV / nu_k + zetaM * nu_k

    The frequency map is normalised by the fundamental at the *static* knob
    tension: turning the Tension knob keeps mode 1 at f1 (it reshapes the
    overtone ratios, i.e. the timbre), while the *dynamic* Berger tension
    below shifts the whole spectrum, fundamental included — that absolute
    shift is the audible hardening glide. zetaV is the viscous-damping knob
    (a term in d w/dt: relative damping falls with frequency) and zetaM the
    material-damping knob (a term in Delta^2 dw/dt: damping grows with
    frequency); both equal the damping ratio of mode 1.

    Geometric nonlinearity (Berger / von Karman, simplified)
    --------------------------------------------------------
    * Dynamic tension ("nonlin" knob). Berger's approximation replaces the
      von Karman membrane coupling by a spatially uniform tension that grows
      with the vibration amplitude, parameterised as a *relative* stiffening
      gamma of mode 1:

          T_dyn = gamma * omega_1^2(T_knob) / g_1 ,
          gamma = nlGain * nonlin * <out^2>

      so mode 1 glides up by sqrt(1 + gamma) on a hard hit and relaxes as
      the ring decays (hardening), and every other mode follows its own
      tension sensitivity g_k. gamma is dimensionless and plate-independent.
      T_dyn drives the ordinary first-order retune law at a decimated rate,
      slewed and throttled (only rewrite coefficients above ~2 cents).

    * Mode cascade ("cascade" knob): the low-mode part of the output (the
      loud, struck modes) is passed through a tanh-bounded cubic and
      injected into the *higher* modes only, at the last hit point. The
      transfer graph low -> high is acyclic — no feedback loop exists — so
      the scheme is unconditionally stable with no gain restriction, unlike
      a full-output feedback (which needs a small-gain bound that renders it
      inaudible). Each target mode's injection is divided by its bandwidth
      compensation so the audible cascade level is damping-independent.
      This mimics the upward energy transfer of von Karman plates: loud low
      modes continuously pump the high ones, and the shimmer decays with
      the low-mode ring. Because the cubic's products are broadband while
      the target resonances are needles (modal overlap ~0.1 at plate
      dampings), the targets' bandwidths are floored — proportionally to
      the cascade knob — at ~0.7x the local mode spacing, so the receiving
      comb overlaps into a quasi-continuum instead of picking out isolated
      partials (see cascadeOverlap in the .cpp).

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
        float cascade   = 0.0f;     // 0..1 upward-cascade amount
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

    /** Current relative stiffening of mode 1 (0 = linear), for display. */
    float getNonlinearGamma() const noexcept { return (float) gamma; }

private:
    void retune();                    // full: mode count, weights, then bank
    void retuneBank();                // coefficients/amps at tension + T_dyn(gamma)
    void computeOutputWeights();      // phi_k(out) -> phiOut
    void computeInputWeights (float x, float y);
    void updateCascadeWeights() noexcept;    // injection weights of the target modes
    void updateDynamicTension() noexcept;    // decimated Berger feedback step

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
    float inWeights[fem::maxModes] {};   // phi_k(last hit) for the external input
    float compensation[fem::maxModes] {};// bandwidth gain compensation per mode
    float lastHitX = 0.5f, lastHitY = 0.5f;
    Hammer hammers[maxStrikes];
    int nextHammer = 0;

    // Nonlinear state (Berger dynamic tension + upward cubic cascade).
    static constexpr int nlUpdatePeriod = 32;   // samples between gamma steps
    float envCoef = 0.001f;              // output-energy follower coefficient
    float envOut = 0.0f;                 // smoothed out^2
    double gamma = 0.0;                  // slewed relative stiffening of mode 1
    double appliedGamma = 0.0;           // value the bank is currently tuned at
    int nlCountdown = nlUpdatePeriod;
    int cascadeSplit = 0;                // modes < split are cascade sources
    float cascadeW[fem::maxModes] {};    // injection weights (targets only)
    float prevOutLow = 0.0f;             // last sample of the low-mode output
};

} // namespace fem
