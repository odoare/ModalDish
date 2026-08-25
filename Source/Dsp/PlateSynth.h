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
      with the *global* stretching of the plate, parameterised as a
      *relative* stiffening gamma of mode 1:

          T_dyn = gamma * omega_1^2(T_knob) / g_1 ,
          gamma = nlGain * nonlin * < int |grad w|^2 dA >

      so mode 1 glides up by sqrt(1 + gamma) on a hard hit and relaxes as
      the ring decays (hardening), and every other mode follows its own
      tension sensitivity g_k. gamma is dimensionless and plate-independent.
      T_dyn drives the ordinary first-order retune law at a decimated rate,
      slewed and throttled (only rewrite coefficients above ~2 cents).

      The stretching integral needs no field reconstruction: the modes are
      mass-orthonormal, so it collapses onto the modal coordinates,

          int |grad w|^2 dA = sum_kl q_k q_l phi_k^T G phi_l ~ sum_k g_k q_k^2

      (the cross terms oscillate at omega_k +- omega_l and average out in
      the envelope) — one multiply-add per mode and per sample. What the
      band-pass bank holds is not q_k though: a 0 dB-peak band-pass fed the
      true modal force is exactly y_k = 2 zeta_k omega_k qdot_k, i.e. a
      *velocity*, normalised. Hence q_k = y_k / (2 zeta_k omega_k^2) and

          nlWeight_k = (g_k / g_1) (zetaRef / zeta_k)^2 / nu_k^4 ,
          driver     = < sum_k nlWeight_k y_k^2 >

      with the leftover constant folded into nlGain. The missing
      1/(4 zeta^2 omega^4) spans ten decades across the bank: leave it out
      and a sum of the filter outputs underestimates the stretching by
      orders of magnitude. Being an integral over the plate, the driver is
      the same wherever one listens — the glide belongs to the plate, not
      to the pickup — and it weights the modes as the stretching really
      does (contributions scale as 1/nu_k, so the lows dominate and the
      cascade shimmer does not drag the glide up). Peak gamma is damping-
      independent by construction too: after an impulse y_k ~ 2 zeta_k
      omega_k, so zeta_k cancels in nlWeight_k y_k^2 and only the *decay*
      of the glide follows the damping knobs.

    * Mode cascade ("cascade" knob): a windowed multi-band ladder with
      transfer inertia. The bank is split into numCascadeBands frequency
      bands; band b is pumped by a tanh-bounded cubic of the output of the
      *cascWindow bands directly below it only* — summing all lower bands
      would let the loud strike band drive the top almost directly, making
      the whole spectrum light up at once. With the window, energy genuinely
      climbs rung by rung (a cubic only reaches 3x its source band), like
      the real von Karman cascade. Each band's injection gain additionally
      passes through an attack/release envelope whose attack time grows
      with the band's height on the ladder (~cascadeAttackMsPerBand x b),
      so the brightening is a progressive glow rather than an instant
      flash, and releases over ~cascadeReleaseMs so the pumping tails off
      smoothly. The transfer graph remains a strict DAG (band b only ever
      receives from below), so stability is unconditional with no gain
      restriction, unlike a full-output feedback (which needs a small-gain
      bound that renders it inaudible). Each target mode's injection is
      weighted by sqrt(zeta_k / zetaRef) — the ladder's own calibration,
      deliberately not the output gain rule, see cascadeInjectionGain in
      the .cpp — and target bandwidths are floored —
      proportionally to the cascade knob and the Overlap parameter — at a
      fraction of the local mode spacing: enough overlap for the receiving
      comb to catch the broadband products, low enough that the pumped
      modes keep a natural ring once the pumping stops.

      The knob acts directly: the drive below carries no damping term, so
      there is nothing for a damping compensation to correct.

      The source signal is the plate's *motion* over the whole plate, and
      deliberately not its audible output: two things sit between the two,
      and the ladder's cubic would raise both to the third power.

      The first is the output gain. The bank models plate *acceleration*:
      c_k = compZetaRef / zeta_k exactly undoes the band-pass's own factor
      2 zeta omega, leaving 2 compZetaRef omega_k times the modal velocity,
      so the audible signal carries a frequency tilt omega_k that the
      plate's motion does not. Weighting the source by velScale_k removes
      both that tilt and the zeta (y_k is proportional to zeta_k omega_k,
      velScale_k to its inverse), leaving the modal velocity, which is what
      the plate actually does. The
      *frequency* balance the ladder needs then has to be supplied on
      purpose: physical modal amplitudes fall steeply with frequency, so a
      raw velocity sum starves the upper rungs (band 0 outweighs band 1 by
      ~900x). Each band's source is therefore normalised by sum(1/nu_k)
      over its own modes — a constant fixed by the mode indices alone, with
      no damping in it. Every rung is then driven by a band-mean modal
      velocity.

      The second is the pickup. A mode nodal at x_o contributes nothing to
      the audible signal however hard the plate is moving there, and the
      cubic coupling of a real plate does not know where you put the
      microphone. The weight is the constant 1/sqrt(A), what a typical
      phi_k is worth on a mass-normalised plate (int phi^2 dA = 1), so the
      drive keeps its level on any geometry while depending on no listening
      position. The *injection* does carry phi_l(x_h): the hit point is
      where the plate deflects most, so it is the one position the
      nonlinearity has a physical claim to, and it is what keeps the
      cascade's character tied to how the plate is played.

      Depletion ("Deplete" parameter): the transfer is otherwise purely
      additive, so the low modes would ring on untouched while the shimmer
      floats on top — an audible disconnect. While band b's gates draw
      from a source band, that band's filter states receive an extra
      per-sample exponential decay proportional to the mean gate activity
      of the bands it feeds: energy audibly *leaves* the lows as the
      shimmer blooms, gluing the two. Purely dissipative, so it cannot
      affect stability.

    Played notes
    ------------
    noteOn() aims the base frequency f1 at the note (mode 1 lands on it, the
    rest of the spectrum follows the ratios of the plate) and strikes at the
    last hit point with velocity/127 scaled by Force. The pitch travels in
    log2, so a glide takes the Glide time whatever the interval, and it is
    stepped on the same 32-sample grid as the Berger tension, sharing its
    ~2-cent retune throttle. The Freq knob is not glided: it sets the pitch
    directly, and so does the first note of the session.

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

    /** One listening point. `level` is a linear gain and `pan` runs -1 (left)
        to +1 (right), equal-power with the centre at unity. A pickup that is
        off contributes nothing and costs nothing — not even the mode-shape
        evaluation at its position.

        Every pickup defaults to *off*: the caller says which listening points
        exist, and a Params with none on is silent rather than guessing. The
        position default is the plate's long-standing output point, so
        switching the first one on reproduces the mono output this replaced. */
    struct Pickup
    {
        float x = 0.5f, y = 0.47f;
        float level = 1.0f;
        float pan = 0.0f;
        bool  on = false;
    };

    struct Params
    {
        float f1        = 110.0f;   // Hz
        float tension   = 0.0f;     // T (scaled units)
        float viscDamp  = 1.0e-4f;  // zeta of mode 1, viscous term
        float matDamp   = 7.0e-6f;  // zeta of mode 1, material term
        float hammerMs  = 3.0f;     // half-sine shock duration
        float force     = 1.0f;     // hammer force amplitude
        float nonlin    = 0.0f;     // 0..1 dynamic-tension (Berger) amount
        float cascade   = 0.0f;     // 0..1 upward-cascade amount
        float glideMs   = 0.0f;     // portamento time between played notes

        // Cascade tuning set (see the class comment; defaults = voiced values).
        float cascAmp       = 1.1f;    // injection gain A
        float cascDrive     = 16.0f;   // tanh knee B
        float cascAttackMs  = 30.0f;   // gate attack per band rung
        float cascReleaseMs = 2000.0f; // gate release
        float cascOverlap   = 0.1f;    // target bandwidth floor, x local spacing
        int   cascWindow    = 4;       // source window, bands below each rung
        float cascDeplete   = 0.07f;   // source-band energy loss while pumping

        Pickup pickups[fem::maxPickups];
        int   numModes  = 32;       // active modes (<= model modes, <= fem::maxModes)
    };

    void prepare (double sampleRate);
    void reset();

    /** Number of modes the bank is actually sounding: the bank is ordered by
        frequency and everything above the audible band is muted, so the
        per-sample loop stops there. At a high Freq setting that can be a
        small fraction of the Modes knob — the knob buys spectrum, and the
        spectrum runs out at Nyquist. */
    int getLiveModeCount() const noexcept { return liveModes; }

    /** Audio thread, once per block: applies a possibly-new model and the
        current parameters, retuning the filter bank only when something
        relevant changed. */
    void update (const ModalModel* model, const Params& params);

    /** Audio thread: hit the plate at (x, y) (plate coordinates) with
        normalised velocity 0..1 (scaled by the force parameter). Also moves
        the external-input injection point there. */
    void strike (float x, float y, float velocity);

    /** Audio thread: a MIDI note strikes the plate at the last hit point with
        `velocity` (0..1, i.e. MIDI velocity / 127, which the Force parameter
        then scales).

        A note no longer sets the pitch. It used to retune the plate so mode 1
        landed on the note, gliding there over the Glide time; notes are
        becoming per-source triggers instead, and a trigger that also moved
        the whole plate's tuning would make eight independently mapped sources
        fight over it. The glide machinery below is left in place and still
        governs Freq knob moves, so restoring the behaviour is a one-line
        change rather than a rewrite. */
    void noteOn (float velocity) noexcept;

    /** Base frequency the bank is currently sounding at (Hz), for display. */
    float getBaseFrequency() const noexcept { return (float) std::exp2 (f1Log); }

    /** Audio thread: one stereo input sample in, one stereo plate sample out.

        The output is a stereo pair because the pickups pan; the input is a
        pair because a source takes its own L/R balance of it (see Params).
        Both collapse into per-mode weight vectors, so the loop cost does not
        grow with the number of pickups or sources. */
    void processSample (float inL, float inR, float& outL, float& outR) noexcept;

    /** Current relative stiffening of mode 1 (0 = linear), for display. */
    float getNonlinearGamma() const noexcept { return (float) gamma; }

    /** What a published modal snapshot holds — one power of the frequency
        ratio apart, which is exactly how much weight the picture gives the
        high modes. */
    enum class Field
    {
        displacement,   ///< q_k     ~ y_k / (zeta_k nu_k^2)
        velocity        ///< qdot_k  ~ y_k / (zeta_k nu_k)
    };

    /** Any thread: copies the latest modal snapshot into dest[0..n-1] and
        returns n (<= maxCount). Summed against the mode shapes this is the
        plate's field, w = sum_k q_k phi_k (or its time derivative).

        The 0 dB-peak band-pass output is y_k = 2 zeta_k omega_k qdot_k, so
        the bank holds *velocities*: Field::velocity is that, exactly, scaled
        per mode (velScale below) and phase-true. Field::displacement divides
        by omega_k once more (dispScale), which also turns each mode a
        quarter period ahead of its true displacement — not observable at
        frame rates, where the low modes are aliased many times over anyway.
        Both drop the constant common to every mode, which a display
        normalises away.

        Sampled every nlUpdatePeriod samples and published per mode with
        relaxed atomics: readers can straddle two sampling instants, which
        is invisible in a picture and keeps the audio thread lock-free. */
    int copyModalField (Field which, float* dest, int maxCount) const noexcept;

private:
    void retune();                    // full: mode count, weights, then bank
    void retuneBank();                // coefficients/amps at tension + T_dyn(gamma)
    void computeOutputWeights();      // phi_k at each pickup -> phiPickup
    void updatePickupMix() noexcept;  // phiPickup + levels/pans -> outAmpL/R
    void computeInputWeights (float x, float y);
    void updateCascadeWeights() noexcept;    // injection weights of the target modes
    void updateCascadeEnvelopes();           // attack/release coefficients from params
    void updateDynamicTension() noexcept;    // decimated Berger feedback step
    void updateGlide() noexcept;             // decimated portamento step

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
    int liveModes = 0;                   // modes actually in band, always <= activeModes
    Resonator filters[fem::maxModes];
    // Mode shapes sampled at each pickup, and the two vectors they collapse
    // into once levels and pans are applied. The per-sample loop only ever
    // touches the collapsed pair.
    float phiPickup[fem::maxPickups][fem::maxModes] {};
    float pickupGainL[fem::maxPickups] {};
    float pickupGainR[fem::maxPickups] {};
    float outAmpL[fem::maxModes] {};
    float outAmpR[fem::maxModes] {};
    float inWeights[fem::maxModes] {};   // phi_k(last hit) for the external input
    float compensation[fem::maxModes] {};// output gain per mode (zetaRef / zeta)
    float cascInjW[fem::maxModes] {};    // cascade injection gain per mode
    float nlWeight[fem::maxModes] {};    // g_k q_k^2 per unit y_k^2 (Berger driver)
    float dispScale[fem::maxModes] {};   // q_k per unit y_k (display field)
    float velScale[fem::maxModes] {};    // qdot_k per unit y_k (display field)
    float cascSrcW[fem::maxModes] {};    // cascade source weight (band-normalised)
    float srcAmp = 1.0f;                 // 1/sqrt(plate area): typical |phi_k|
    float lastHitX = 0.5f, lastHitY = 0.5f;
    Hammer hammers[maxStrikes];
    int nextHammer = 0;

    // Base-frequency glide. The bank is tuned at exp2(f1Log); played notes
    // move f1Log towards f1LogTarget by glideStep per decimated update
    // (log2 domain, so a glide takes the same time whatever the interval),
    // and the bank is rewritten when it has drifted ~2 cents from the pitch
    // it currently sounds at — the same throttle the Berger glide uses.
    double f1Log = 0.0, f1LogTarget = 0.0;
    double f1LogTuned = -1.0e9;
    double glideStep = 0.0;
    bool gliding = false;
    bool notePlayed = false;

    // Field snapshots for the GUI (see copyModalField).
    std::atomic<float> fieldQ[fem::maxModes];
    std::atomic<float> fieldV[fem::maxModes];
    std::atomic<int> fieldCount { 0 };

    // Nonlinear state (Berger dynamic tension + windowed cubic cascade).
    static constexpr int nlUpdatePeriod = 32;   // samples between gamma steps
    static constexpr int numCascadeBands = 8;
    float envCoef = 0.001f;              // stretching follower coefficient
    float envStretch = 0.0f;             // smoothed sum_k nlWeight_k y_k^2
    double gamma = 0.0;                  // slewed relative stiffening of mode 1
    double appliedGamma = 0.0;           // value the bank is currently tuned at
    int nlCountdown = nlUpdatePeriod;
    int cascadeSplit = 0;                // first mode of band 1 (targets start here)
    float cascEff = 0.0f;                // cascade knob x material-damping scale
    int bandStart[numCascadeBands + 1] {};        // mode-index range of each band
    float cascadeW[fem::maxModes] {};    // injection weights (bands >= 1 only)
    float prevBandOut[numCascadeBands] {};        // per-band output, last sample
    float gateEnv[numCascadeBands] {};   // per-band injection envelope (0..1)
    float attackCoef[numCascadeBands] {};// per-band attack one-pole coefficient
    float releaseCoef = 0.001f;
};

} // namespace fem
