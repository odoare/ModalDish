/*
  ------------------------------------------------------------------------------
    PlateSynth.cpp — see PlateSynth.h (including the nonlinearity notes).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "PlateSynth.h"

#include <FxmeTools/dsp/DeterministicRandom.h>

namespace fem
{

namespace
{
    // Damping compensation, and why it is exactly 1/zeta.
    //
    // The RBJ constant-0 dB-peak band-pass is, as an analog prototype,
    //
    //     H(s) = 2 zeta omega s / (s^2 + 2 zeta omega s + omega^2)
    //
    // that is, 2 zeta_k omega_k times the velocity transfer function of the
    // modal oscillator. The bank output is therefore not the plate's motion
    // but its motion scaled by each mode's own bandwidth, and an impulse
    // response that ought to peak at a level set by the strike alone peaks
    // proportionally to zeta instead. More damping, louder attack: the
    // opposite of the physics, where damping governs how fast a mode gives
    // its energy back and not how much the strike put in.
    //
    // Dividing that factor straight back out is the whole fix. With
    //
    //     c_k = zetaRef / zeta_k
    //
    // the chain becomes c_k H(s) = 2 zetaRef omega_k s / (s^2 + ...), an
    // exact model of modal *acceleration* (velocity times omega, up to the
    // constant 2 zetaRef) — which is the right output quantity anyway, since
    // a plate radiates pressure proportional to its acceleration. The strike
    // then sets the peak, the damping sets the decay, and the two stop
    // interfering:
    //
    //     peak    2 zetaRef omega_k J_k     no zeta at all
    //     decay   exp(-zeta_k omega_k t)    zeta alone
    //
    // The previous rule was sqrt(zetaRef / zeta), which removes half of the
    // 2 zeta omega in the exponent and leaves peak proportional to sqrt(zeta)
    // — measured at +22 dB across the Viscous knob and +14 dB across the
    // Material knob, which is what this replaces.
    //
    // compZetaRef is not a free parameter of the physics, only of the gain
    // staging: it is picked so that the default patch comes out at the level
    // it always did (measured 0.18 dB apart), so presets keep their balance.
    //
    // It is deliberately NOT zetaRef below. That one normalises the modal
    // reconstruction q_k = y_k / (2 zeta_k omega_k^2) that the Berger driver,
    // the cascade source and the display field are all calibrated against —
    // nlGain in particular is a measured number that assumes it. Sharing one
    // constant between the two roles means every future adjustment of the
    // output level silently rescales the nonlinearity by its square, which is
    // exactly the trap this comment exists to keep the next reader out of.
    constexpr float compZetaRef = 1.0e-3f;

    // The bounds never bind over the parameter ranges the plugin offers
    // (zeta is itself clamped to [1e-6, 0.5], giving c in [2e-3, 1e3]); they
    // are here to keep a pathological model from producing a pathological
    // gain, not to shape the response. A clamp that bit would reintroduce
    // exactly the damping-dependent level this rule exists to remove.
    constexpr float minCompensation = 1.0e-3f;
    constexpr float maxCompensation = 1.0e3f;

    // Injection reference of the cascade ladder, and why it is NOT the output
    // gain above.
    //
    // A rung of the ladder is driven continuously by the rung below, not
    // struck, and a continuously driven mode settles at a velocity
    // proportional to 1/zeta. The ladder's rung-to-rung gain therefore
    // carries a 1/zeta whatever weight is used, and the cubic then cubes
    // whatever is left of it. Weighting the injection by sqrt(zeta) cancels
    // half of that, which is the calibration the Drive and gate settings were
    // all tuned against.
    //
    // Measured, sweeping the exponent of zeta in this weight (with the
    // cascade amount that then existed swept over its range):
    //
    //     exponent   Material spread   shimmer over that range
    //       0.5        24.8 dB           -71 -> -60 dB  (works)
    //       0.75       11.9 dB           -71 -> -74 dB  (inverted)
    //       1.0         9.3 dB           -71 -> -77 dB  (inverted)
    //
    // Cancelling the ladder's 1/zeta outright does flatten the shimmer
    // against the damping knobs, and starves the ladder doing it: past about
    // 0.5 the injection falls faster than the cubic can make up, so turning
    // the cascade up made the plate quieter at the top. 0.5 is the
    // calibration the Drive setting and the gate thresholds were tuned
    // against, and it stays.
    //
    // How flat the shimmer should be against damping is a playability
    // question, not a physical one — a real plate cascades far more freely
    // when it is lightly damped — and it was answered earlier by ear. This
    // rule is where that answer lives.
    float cascadeInjectionGain (float zeta) noexcept
    {
        return juce::jlimit (1.0f / 32.0f, 2.0f,
                             std::sqrt (juce::jmax (zeta, 1.0e-7f) / 0.01f));
    }

    float gainCompensation (float zeta) noexcept
    {
        const float g = compZetaRef / juce::jmax (zeta, 1.0e-7f);
        return juce::jlimit (minCompensation, maxCompensation, g);
    }

    // Reference damping of the modal reconstruction, unrelated to the output
    // gain above: q_k and qdot_k are expressed relative to this, and the
    // remaining constant factors are folded into nlGain and the cascade
    // source normalisation. Changing it rescales the Berger driver by its
    // square and the cascade source linearly, so it is calibration, not a
    // tuning knob.
    constexpr float zetaRef = 0.01f;

    /** Equal-power pan normalised so that a centred position is unity in both
        channels rather than the usual -3 dB. Shared by the pickup pan and by
        the tail spread, which needs the same "centre changes nothing" property
        for a different reason: see updateTailSpread. */
    inline void equalPowerPan (float pan, float level, float& gainL, float& gainR) noexcept
    {
        constexpr float centreUnity = 1.41421356f;   // sqrt(2)
        const float theta = 0.25f * juce::MathConstants<float>::pi
                            * (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f);
        const float g = centreUnity * level;
        gainL = g * std::cos (theta);
        gainR = g * std::sin (theta);
    }

    // Hammer forces are impulsive and benefit from a little headroom. Note
    // that a velocity-1, force-1 hit peaks well below unity (~0.012 on the
    // default plate): a short pulse only deposits a little energy in each
    // narrow resonance, and the Out Gain stage downstream makes up for it.
    constexpr float hammerGain = 0.4f;

    // Berger calibration: gamma = nlGain * nonlin * <sum_k nlWeight_k y_k^2>,
    // the (scaled) stretching integral int |grad w|^2 dA reconstructed from
    // the modal coordinates — see the header. Measured on the default plate
    // (elliptic, simply supported, grid 16, f1 = 110), the driver peaks at
    // 2.9e-4 for a velocity-1 hit at Force 1 and grows as force^2, so with
    // nlGain = 500 and nonlin = 1:
    //     Force 1 -> gamma 0.15 (~1.2 semitones), Force 4 -> 2.3,
    //     Force >~5 -> the gammaCap; in effect mode, a unity-peak input
    //     sits around gamma 2.
    // The driver is damping-independent by construction (within 12% over
    // zetaV, zetaM in [1e-5, 1e-3]), so this one calibration holds across
    // the damping range, and being an integral over the plate it holds
    // wherever the pickup sits.
    constexpr double nlGain = 500.0;
    constexpr double gammaCap = 4.0;

    // Windowed cascade ladder: band b is driven by
    // fb_b = cascade * cascadeInjectionAmp * gate_b * tanh(cascDrive*src_b)^3,
    // src_b the output of the cascWindow bands directly below b (a wide
    // source window lets the loud strike band drive the top bands almost
    // directly and the whole spectrum lights up at once). A cubic only
    // reaches 3x its source band, so the windowed ladder makes energy climb
    // rung by rung; the transfer graph stays a DAG, so there is no
    // stability constraint. gate_b is an attack/release envelope on the
    // source presence: attack grows with the band's height on the ladder
    // (progressive glow), release lets the pumping tail off smoothly.
    //
    // The modal-overlap floor (cascOverlap, in retuneBank) widens the
    // cascade targets to catch the broadband cubic products (at plate
    // dampings the resonances are needles spaced ~0.64*f1 apart, overlap
    // ~0.1, so most injected energy would fall between modes); high values
    // audibly damp the high end, low values keep the pumped modes ringing
    // naturally after the pumping stops.
    //
    // The cascade source gain, applied to the band-mean modal velocity that
    // drives the ladder (see the header). Calibrated so the audible cascade
    // on the default plate matches the level the previous, audio-driven
    // source produced at the reference damping — the voiced Amp/Drive
    // settings therefore keep their meaning.
    constexpr float cascadeSourceGain = 3.0f;

    // Injection gain of the ladder, formerly the Casc Amp parameter. It is
    // pinned because a gain and an amount in series are one control between
    // them: the cascade amount now spans this whole range on its own, so
    // amount 1 is what Amp 10 used to be and amount 0.11 what its voiced 1.1
    // was. Drive keeps its own knob because it changes the *shape* of the
    // carrier rather than only its size.
    constexpr float cascadeInjectionAmp = 10.0f;

    // Depletion couples the two ends of the ladder: a source band's filter
    // states decay by an extra factor (1 - deplete * cascade * depletePerSample
    // * activity) per sample, activity being the mean gate level of the bands
    // it feeds. At full depletion/activity the extra decay time is this tau:
    // the lows audibly hand their energy to the shimmer instead of ringing on
    // untouched.
    //
    // Stated in seconds, not per sample. It used to be a bare per-sample
    // constant of 3e-4, which made the time it stood for halve every time the
    // sample rate doubled - a control calibrated by ear at 48 kHz decayed
    // twice as fast at 96 kHz. The value below is exactly what that constant
    // meant at 48 kHz, so the change is a no-op there and a fix everywhere
    // else. PlateSynth::prepare turns it into the per-sample factor.
    constexpr double depleteTauSec = 1.0 / (3.0e-4 * 48000.0);   // 69.4 ms

    // Slew of the Berger stiffening towards its target, as a time constant
    // rather than a fixed step per decimated update - same reason: the update
    // period is a fixed number of *samples*, so a fixed step per update was a
    // slew whose duration depended on the sample rate. This value reproduces
    // the old 0.2 per update exactly at 48 kHz.
    constexpr double gammaSlewTauSec = 0.002987613;              // 2.99 ms

    // All of these are exposed as plugin parameters (CASCADE panel) while
    // the effect is being voiced; Params holds the defaults.
}

void PlateSynth::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    // ~20 ms energy follower: fast enough for the attack glide, slow enough
    // not to track individual cycles of the low modes.
    envCoef = 1.0f - std::exp ((float) (-1.0 / (0.02 * fs)));

    // Everything below is a duration, so every one of them is turned into a
    // per-step coefficient here, where the sample rate is known. Nothing in
    // the per-sample path may hold a bare rate constant: see depleteTauSec.
    depletePerSample = (float) (1.0 - std::exp (-1.0 / (depleteTauSec * fs)));
    gammaSlew = 1.0 - std::exp (-(double) nlUpdatePeriod / (gammaSlewTauSec * fs));

    updateCascadeEnvelopes();
    reset();
    dirty = true;
}

void PlateSynth::updateCascadeEnvelopes()
{
    // Cascade transfer inertia: band b's injection gate attacks over
    // b * cascAttackMs and releases over cascReleaseMs.
    for (int b = 0; b < numCascadeBands; ++b)
    {
        const double tauSec = 1.0e-3 * juce::jmax (0.1f, current.cascAttackMs)
                              * juce::jmax (1, b);
        attackCoef[b] = 1.0f - (float) std::exp (-1.0 / (tauSec * fs));
    }
    releaseCoef = 1.0f - (float) std::exp (
        -1.0 / (1.0e-3 * juce::jmax (1.0f, current.cascReleaseMs) * fs));
}

void PlateSynth::reset()
{
    for (auto& f : filters)
        f.reset();
    for (auto& h : hammers)
        h.active = false;
    envStretch = 0.0f;
    for (auto& q : fieldQ)
        q.store (0.0f, std::memory_order_relaxed);
    for (auto& v : fieldV)
        v.store (0.0f, std::memory_order_relaxed);
    fieldCount.store (0, std::memory_order_release);
    gliding = false;
    notePlayed = false;
    gamma = appliedGamma = 0.0;
    nlCountdown = nlUpdatePeriod;
    for (float& b : prevBandOut)
        b = 0.0f;
    for (float& g : gateEnv)
        g = 0.0f;
}

void PlateSynth::update (const ModalModel* newModel, const Params& params)
{
    if (newModel != model)
    {
        model = newModel;
        reset();
        // The plate geometry changed: re-anchor the input injection point.
        dirty = true;
    }

    // f1 is not in the list: it reaches the bank through the glide state
    // below, so that a played note can own the pitch without the knob
    // stealing it back on the next block.
    const bool f1Changed = ! juce::approximatelyEqual (params.f1, current.f1);
    const bool tuningChanged =
           ! juce::approximatelyEqual (params.tension,  current.tension)
        || ! juce::approximatelyEqual (params.viscDamp, current.viscDamp)
        || ! juce::approximatelyEqual (params.matDamp,  current.matDamp)
        || ! juce::approximatelyEqual (params.cascOverlap, current.cascOverlap)
        || ! juce::approximatelyEqual (params.cascade,  current.cascade)  // overlap floor
        || params.numModes != current.numModes;

    // Pickups are split in two, because moving one is not the same kind of
    // change as re-levelling one. A new position needs the mode shapes
    // resampled there; a new level, pan or on/off only needs the collapsed
    // mix rebuilt, which is a few thousand multiply-adds rather than a full
    // retune of the bank.
    bool pickupMoved = false, pickupMixChanged = false;
    for (int p = 0; p < fem::maxPickups; ++p)
    {
        const auto& a = params.pickups[p];
        const auto& b = current.pickups[p];
        // The switch counts as a move, as it does for the sources: a pickup
        // that is off is never sampled, so its phiPickup row is zeros, and
        // rebuilding the collapsed mix on top of those would switch on a
        // pickup that stays silent until something else forces a resample.
        if (! juce::approximatelyEqual (a.x, b.x) || ! juce::approximatelyEqual (a.y, b.y)
            || a.on != b.on)
            pickupMoved = true;
        if (! juce::approximatelyEqual (a.level, b.level)
            || ! juce::approximatelyEqual (a.pan, b.pan))
            pickupMixChanged = true;
    }

    // Which pickup the popup meter follows is not a plugin parameter, but it
    // changes the weights the mix builds, so it rides in with them.
    if (params.meteredPickup != current.meteredPickup)
        pickupMixChanged = true;

    // Sources split the same way: a move needs the shapes resampled, a send
    // or balance change only needs the collapsed mix rebuilt. Hammer, force,
    // spread and note are read at strike time and need neither.
    bool sourceMoved = false, sourceMixChanged = false;
    for (int i = 0; i < fem::maxSources; ++i)
    {
        const auto& a = params.sources[i];
        const auto& b = current.sources[i];
        if (! juce::approximatelyEqual (a.x, b.x) || ! juce::approximatelyEqual (a.y, b.y)
            || a.on != b.on)
            sourceMoved = true;
        if (! juce::approximatelyEqual (a.send, b.send)
            || ! juce::approximatelyEqual (a.pan, b.pan))
            sourceMixChanged = true;
    }

    const bool envChanged =
           ! juce::approximatelyEqual (params.cascAttackMs,  current.cascAttackMs)
        || ! juce::approximatelyEqual (params.cascReleaseMs, current.cascReleaseMs);

    const bool injectChanged = params.cascModalInject != current.cascModalInject;

    current = params;
    if (envChanged)
        updateCascadeEnvelopes();
    // Cheap enough here: the weights are one multiply per mode, where a
    // retune (which also rebuilds them) is far more than this needs.
    if (injectChanged)
        updateCascadeWeights();

    // The knob sets the pitch outright (glide belongs to played notes), and
    // so does a model swap.
    if (f1Changed || dirty)
    {
        f1Log = f1LogTarget = std::log2 ((double) juce::jmax (1.0f, current.f1));
        gliding = false;
    }
    if (dirty || tuningChanged || f1Changed)
    {
        retune();
        dirty = false;
    }
    else
    {
        if (pickupMoved)
            computeOutputWeights();     // rebuilds the pickup mix itself
        else if (pickupMixChanged)
            updatePickupMix();

        if (sourceMoved)
            computeSourceShapes();      // likewise the source mix
        else if (sourceMixChanged)
            updateSourceMix();
    }
}

void PlateSynth::retune()
{
    activeModes = 0;
    liveModes = 0;
    if (model == nullptr || model->numModes() < 1)
        return;

    activeModes = juce::jlimit (0, juce::jmin (model->numModes(), fem::maxModes),
                                current.numModes);

    // Cascade bands: equal index ranges (constant plate modal density makes
    // them equal frequency ranges too). Band 0 is source-only; each higher
    // band receives from all bands below it.
    for (int b = 0; b <= numCascadeBands; ++b)
        bandStart[b] = juce::jmin (activeModes,
                                   juce::jmax (b, activeModes * b / numCascadeBands));
    cascadeSplit = juce::jmax (1, bandStart[1]);
    cascEff = current.cascade;

    // Mass-normalised shapes satisfy int phi^2 dA = 1, so a typical value of
    // phi_k is 1/sqrt(A): that is the constant the cascade source uses in
    // place of a point evaluation, which keeps its level the same on any
    // plate (see updateCascadeWeights and the header).
    double area = 0.0;
    if (model->mesh != nullptr)
        for (const auto& t : model->mesh->triangles)
        {
            const auto& a = model->mesh->vertices[(size_t) t[0]];
            const auto& b = model->mesh->vertices[(size_t) t[1]];
            const auto& c = model->mesh->vertices[(size_t) t[2]];
            area += 0.5 * std::abs ((b.x - a.x) * (c.y - a.y)
                                  - (c.x - a.x) * (b.y - a.y));
        }
    srcAmp = (float) (1.0 / std::sqrt (juce::jmax (1.0e-6, area)));

    // Fixed injection weights for the modal option, drawn from the same
    // distribution the hit point produces: a tail mode's shape is
    // sqrt(2/A) cos(kappa.x + psi), so at any one point it is an arcsine
    // variable of rms 1/sqrt(A). Drawing psi per mode instead of reading it
    // off the hammer's position is the whole change. Matching the rms is
    // what lets Drive, the gate thresholds and the voiced defaults survive
    // the switch, and keeping the values incoherent between modes is what
    // stops a band's worth of injection arriving as one in-phase push.
    //
    // The seed is a constant, so a plate sounds the same every time it is
    // loaded, and every plate gets the same phase sequence -- the modes it
    // multiplies are what differ.
    constexpr std::uint64_t cascInjectSeed = 0x5eedca5cade1ull;
    for (int k = 0; k < fem::maxModes; ++k)
    {
        const float psi = juce::MathConstants<float>::twoPi
                            * fxme::detrand::u01 (cascInjectSeed, (std::uint64_t) k);
        // sqrt(2) as a literal: PlateSynth builds against the JUCE stub for
        // the offline tests, which carries only the constants it needs to.
        cascInjRand[k] = 1.41421356f * srcAmp * std::cos (psi);
    }

    computeOutputWeights();
    computeSourceShapes();
    computeHitWeights (lastHitX, lastHitY);
    retuneBank();
}

void PlateSynth::retuneBank()
{
    if (model == nullptr || activeModes < 1)
        return;

    const auto& lambda = model->modes.lambda;
    const auto& g = model->modes.tensionG;

    // Static part: the knob tension. The fundamental at this tension is the
    // reference the f1 mapping is pinned to (the knob reshapes ratios only).
    const double dTknob = (double) current.tension - model->modes.tensionRef;
    const double base1 = juce::jmax (1.0e-12, lambda[0] + dTknob * g[0]);

    // Dynamic part: T_dyn = gamma * base1 / g_1, so mode 1's stiffness is
    // base1 * (1 + gamma) — the whole spectrum (fundamental included)
    // glides up with gamma. That absolute shift is the hardening glide;
    // normalising by the instantaneous fundamental instead would pin mode 1
    // and leave only the overtone-ratio compression, which is heard as a
    // *softening* — the wrong direction.
    const double tDyn = gamma * base1 / juce::jmax (g[0], 1.0e-12);
    const double dT = dTknob + tDyn;

    // Instantaneous fundamental, used for the damping law's ratios.
    const double omega1SqEff = juce::jmax (1.0e-12, lambda[0] + dT * g[0]);
    const double f1Hz = std::exp2 (f1Log);
    f1LogTuned = f1Log;
    const double maxFreq = 0.47 * fs;

    // First pass: frequencies (needed for the local mode spacing below).
    double freq[fem::maxModes];
    double nu[fem::maxModes];
    for (int k = 0; k < activeModes; ++k)
    {
        const double omegaSq = juce::jmax (0.0, lambda[(size_t) k] + dT * g[(size_t) k]);
        freq[k] = f1Hz * std::sqrt (omegaSq / base1);
        nu[k] = std::sqrt (omegaSq / omega1SqEff);
    }

    liveModes = 0;
    for (int k = 0; k < activeModes; ++k)
    {
        if (nu[k] <= 0.0 || freq[k] < 20.0 || freq[k] > maxFreq)
        {
            // Keep the bank index aligned with the mode index so the shape
            // weights stay matched; the mode is silent and its filter is
            // muted (b0 = 0) so it cannot feed the feedback paths.
            // Zero rather than one: compensation doubles as the mode's
            // liveness marker, so the pickup mix and the cascade injection
            // both mute a dead mode without a second test.
            compensation[k] = 0.0f;
            cascInjW[k] = 0.0f;
            nlWeight[k] = 0.0f;
            dispScale[k] = 0.0f;
            velScale[k] = 0.0f;
            filters[k].c = fxme::BiquadCoeffs();
            filters[k].c.b0 = 0.0f;
            continue;
        }

        float zeta = juce::jlimit (1.0e-6f, 0.5f,
                                   (float) ((double) current.viscDamp / nu[k]
                                          + (double) current.matDamp * nu[k]));

        // Cascade targets: floor the bandwidth (2 zeta f) at cascOverlap
        // times the local mode spacing, scaled by the cascade amount, so the
        // receiving comb overlaps into a quasi-continuum when driven -- and
        // only when driven. The amount has to be in here: without it a plate
        // with the cascade at zero would still have every target mode widened
        // (and so damped) by a control that is doing nothing else.
        if (k >= cascadeSplit && cascEff > 0.0f)
        {
            const double spAbove = k + 1 < activeModes ? freq[k + 1] - freq[k]
                                 : k > 0               ? freq[k] - freq[k - 1] : 0.0;
            const double spBelow = k > 0 ? freq[k] - freq[k - 1] : spAbove;
            const double spacing = juce::jmax (0.0, 0.5 * (spAbove + spBelow));
            const double zetaFloor = (double) cascEff
                                     * (double) current.cascOverlap
                                     * spacing / (2.0 * freq[k]);
            zeta = juce::jmax (zeta, (float) juce::jlimit (0.0, 0.5, zetaFloor));
        }

        liveModes = k + 1;      // freq ascends, so this ends up past the last live mode

        const float q = juce::jlimit (0.5f, 1.0e5f, 1.0f / (2.0f * zeta));
        filters[k].c = fxme::BiquadCoeffs::bandpass (fs, (float) freq[k], q);
        compensation[k] = gainCompensation (zeta);
        cascInjW[k] = cascadeInjectionGain (zeta);

        // Berger driver: this mode's contribution g_k q_k^2 to the
        // stretching integral, per unit y_k^2 (header: q_k is the filter
        // output divided by 2 zeta_k omega_k^2). Frequencies relative to
        // mode 1 and damping relative to zetaRef; the remaining constant
        // factor is folded into nlGain.
        const double gRatio = g[(size_t) k] / juce::jmax (g[0], 1.0e-12);
        const double zRatio = (double) zetaRef / (double) zeta;
        const double nuSq = nu[k] * nu[k];
        nlWeight[k] = (float) juce::jlimit (0.0, 1.0e12,
                          gRatio * zRatio * zRatio / juce::jmax (1.0e-12, nuSq * nuSq));

        // Modal displacement and velocity per unit filter output, for the
        // GUI field (copyModalField). Same normalisation choices as above,
        // so the field keeps a consistent scale across parameter changes.
        // Velocity is the displacement times nu_k: the same picture with the
        // high modes lifted one power of frequency, which is what makes it
        // look like the bright end of the spectrum rather than the low modes.
        velScale[k]  = (float) juce::jlimit (0.0, 1.0e12,
                           zRatio / juce::jmax (1.0e-12, nu[k]));
        dispScale[k] = (float) juce::jlimit (0.0, 1.0e12,
                           zRatio / juce::jmax (1.0e-12, nuSq));
    }

    // Cascade source weights: the modal velocity of each mode, normalised
    // per band by a constant that depends on the mode indices only (no
    // damping), and by the plate's own scale 1/sqrt(A) rather than by a
    // mode shape sampled at the pickup. Every rung of the ladder is then
    // driven by a comparable band-mean velocity of the whole plate. The
    // header explains why the drive must not be the audible signal.
    for (int b = 0; b < numCascadeBands; ++b)
    {
        double norm = 0.0;
        for (int k = bandStart[b]; k < bandStart[b + 1] && k < activeModes; ++k)
            norm += 1.0 / juce::jmax (1.0e-6, nu[k]);

        const float scale = (float) (norm > 0.0 ? cascadeSourceGain * srcAmp / norm : 0.0);
        for (int k = bandStart[b]; k < bandStart[b + 1] && k < activeModes; ++k)
            cascSrcW[k] = velScale[k] * scale;
    }

    appliedGamma = gamma;
    updateCascadeWeights();
    // compensation[] has just been rewritten, and the pickup mix is built on
    // top of it, so it has to follow every retune.
    updatePickupMix();
}

void PlateSynth::computeOutputWeights()
{
    for (int p = 0; p < fem::maxPickups; ++p)
    {
        for (float& w : phiPickup[p])
            w = 0.0f;
        // A pickup that is off is not sampled at all: evalShapes is a point
        // location plus one interpolation per mode, and there is no reason to
        // pay it for a listening point nobody is listening through.
        if (model != nullptr && current.pickups[p].on)
            model->evalShapes (current.pickups[p].x, current.pickups[p].y,
                               phiPickup[p], activeModes);
    }
    updatePickupMix();
}

void PlateSynth::updatePickupMix() noexcept
{
    // Equal-power pan, normalised so that a centred pickup is unity in both
    // channels rather than the usual -3 dB. Sine/cosine alone would make the
    // default single centred pickup 3 dB quieter than the mono output it
    // replaces, for no reason the player would recognise; the sqrt(2) puts
    // the centre back at unity and hard-panned at +3 dB in its own channel,
    // which is the same total power either way.
    for (int p = 0; p < fem::maxPickups; ++p)
    {
        const auto& pk = current.pickups[p];
        if (! pk.on)
        {
            pickupGainL[p] = pickupGainR[p] = 0.0f;
            continue;
        }
        equalPowerPan (pk.pan, pk.level, pickupGainL[p], pickupGainR[p]);
    }

    // The collapse: however many pickups are on, the audio loop sees two
    // numbers per mode. compensation[k] is zero for a mode outside the
    // audible band, which mutes it here without a second test.
    for (int k = 0; k < fem::maxModes; ++k)
    {
        float l = 0.0f, r = 0.0f;
        for (int p = 0; p < fem::maxPickups; ++p)
        {
            l += pickupGainL[p] * phiPickup[p][k];
            r += pickupGainR[p] * phiPickup[p][k];
        }
        outAmpL[k] = compensation[k] * l;
        outAmpR[k] = compensation[k] * r;
    }

    updateTailSpread();
    updateMeterWeights();
}

void PlateSynth::updateMeterWeights() noexcept
{
    // One pickup at a time, because only one point panel is open at a time.
    // Metering all eight would cost eight multiply-adds per mode per sample,
    // four times what the stereo mix itself costs; metering the one being
    // looked at costs one, and nothing at all when no panel is open.
    const int p = current.meteredPickup;
    metering = p >= 0 && p < fem::maxPickups && current.pickups[p].on;
    if (! metering)
    {
        meterOut = 0.0f;
        return;
    }

    // Mono, and deliberately so: the panel's meter answers "how much is this
    // point hearing", which its Level scales and its Pan does not. Pan only
    // decides where that signal goes in the image. compensation[k] is zero
    // outside the audible band, which mutes those modes here as it does in
    // the stereo mix.
    const float level = current.pickups[p].level;
    for (int k = 0; k < fem::maxModes; ++k)
        meterAmp[k] = compensation[k] * level * phiPickup[p][k];
}

void PlateSynth::updateTailSpread() noexcept
{
    // Why the tail needs help, and only the tail.
    //
    // A pickup's gain on one mode is phi_k at its position, which differs
    // wildly from pickup to pickup: a single mode read at two points differs
    // by ~15 dB rms, and more if one of them sits near a node. That is what
    // makes the low end wide - each partial gets its own place in the image.
    //
    // Sum over many modes, though, and the law of large numbers erases it.
    // Each channel's gain tends to sum_k phi_k^2, which converges to the same
    // spatial average wherever the pickup is: measured on the default plate,
    // two pickups differ by 2.1 dB over the first four modes and by 0.29 dB
    // over all 512. So everything the cascade regenerates - hundreds of modes
    // at once - lands in the same place, the middle. Neither a decorrelator
    // nor a delay can move it: the fine structure up there is already diffuse
    // (interchannel correlation about -0.3), and the two channels' envelopes
    // match at 0.96 to 0.98 for *every* lag within +/-1 ms, so a delay could
    // only shift the shimmer off centre, never widen it. The levels are equal
    // and the envelopes are equal, and that is the whole of it.
    //
    // The cure is to restore a level difference that does not average away.
    // Take it per band from the *signed* sum of the readout weights rather
    // than their sum of squares: squares all carry the same sign and so
    // converge, while the signed sum is a random walk that keeps a magnitude
    // and a sign specific to where the pickups actually are. Fold the result
    // straight into outAmpL/R, so the audio loop is untouched and this costs
    // nothing per sample.
    //
    // Only above the last FEM mode, where the mode "shapes" are the synthetic
    // plane waves of the statistical tail. Below that they are computed
    // eigenvectors that already carry a real position dependence, and band
    // resolution would be throwing away physics we went to the trouble of
    // solving for.
    if (model == nullptr)
        return;

    const int tailStart = juce::jlimit (0, liveModes, model->numFemModes());
    if (tailStart < 1 || liveModes - tailStart < 8)
        return;   // too few tail modes to make a band sum mean anything

    // Band edges geometric in mode index, which for a plate is geometric in
    // frequency too (constant modal density; the cascade bands rely on
    // the same fact). Roughly three bands to the octave, because the width
    // has to survive the ear as well as the arithmetic: bands much narrower
    // than an auditory filter get summed back together by the listener, which
    // re-centres them and undoes the whole exercise. A third of an octave is
    // comfortably wider than an ERB everywhere the tail lives.
    const double span = (double) liveModes / (double) tailStart;
    const int numBands = juce::jlimit (2, numTailBands,
                                       (int) std::lround (3.0 * std::log2 (span)));

    int edge[numTailBands + 1] {};
    for (int b = 0; b <= numBands; ++b)
    {
        const double f = std::pow (span, (double) b / (double) numBands);
        edge[b] = juce::jlimit (tailStart, liveModes,
                                (int) std::lround ((double) tailStart * f));
        if (b > 0)
            edge[b] = juce::jmax (edge[b], juce::jmin (liveModes, edge[b - 1] + 1));
    }
    edge[numBands] = liveModes;

    float pan[numTailBands] {};
    float mean = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        float sumL = 0.0f, sumR = 0.0f;
        for (int k = edge[b]; k < edge[b + 1]; ++k)
        {
            sumL += outAmpL[k];
            sumR += outAmpR[k];
        }

        // Magnitudes only: the sign of a random walk says nothing about which
        // side the band belongs on, its size does.
        const float aL = std::abs (sumL), aR = std::abs (sumR);
        const float total = aL + aR;
        pan[b] = total > 1.0e-12f
                   ? juce::jlimit (-tailPanLimit, tailPanLimit, (aR - aL) / total)
                   : 0.0f;
        mean += pan[b];
    }
    mean /= (float) numBands;

    for (int b = 0; b < numBands; ++b)
    {
        // Scatter without shifting. Summed over enough modes the two channels
        // really do receive the same total, whatever the pickup positions
        // (0.29 dB apart over 512 modes on the default plate), so the tail's
        // overall balance is not the random walk's to set: only the placement
        // of one band relative to the others is. Taking the mean out keeps the
        // measured fact and restores the scatter the averaging destroyed.
        const float centred = juce::jlimit (-tailPanLimit, tailPanLimit, pan[b] - mean);

        // Equal power, and unity both sides when a band has no preference.
        // One centred pickup weights both channels alike, so every band lands
        // there and a single pickup still cannot manufacture a stereo image
        // out of nothing (to within the last bit of cos and sin).
        float gainL = 1.0f, gainR = 1.0f;
        equalPowerPan (centred, 1.0f, gainL, gainR);
        for (int k = edge[b]; k < edge[b + 1]; ++k)
        {
            outAmpL[k] *= gainL;
            outAmpR[k] *= gainR;
        }
    }
}

void PlateSynth::computeHitWeights (float x, float y)
{
    for (float& w : hitWeights)
        w = 0.0f;
    if (model != nullptr)
        model->evalShapes (x, y, hitWeights, activeModes);
    updateCascadeWeights();
}

void PlateSynth::computeSourceShapes()
{
    for (int i = 0; i < fem::maxSources; ++i)
    {
        for (float& w : phiSource[i])
            w = 0.0f;
        // As with the pickups, a source that is off is never sampled: no
        // point paying a point location plus a per-mode interpolation for an
        // injection point that injects nothing.
        if (model != nullptr && current.sources[i].on)
            model->evalShapes (current.sources[i].x, current.sources[i].y,
                               phiSource[i], activeModes);
    }
    updateSourceMix();
}

void PlateSynth::updateSourceMix() noexcept
{
    // Input balance, not a pan law: a source takes a weighted mean of the two
    // incoming channels, and the weights sum to one so that a centred source
    // receives exactly the mono sum the single injection point used to get.
    for (int k = 0; k < fem::maxModes; ++k)
    {
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < fem::maxSources; ++i)
        {
            const auto& src = current.sources[i];
            if (! src.on)
                continue;
            const float p = juce::jlimit (-1.0f, 1.0f, src.pan);
            l += src.send * 0.5f * (1.0f - p) * phiSource[i][k];
            r += src.send * 0.5f * (1.0f + p) * phiSource[i][k];
        }
        inWL[k] = l;
        inWR[k] = r;
    }
}

void PlateSynth::updateCascadeWeights() noexcept
{
    // Where the ladder's output re-enters the bank. Either way the weight is
    // scaled by cascadeInjectionGain (see there for why that rule is
    // deliberately not the output gain rule), and either way the source modes
    // get zero, so the low -> high transfer graph stays acyclic and is
    // unconditionally stable with no gain restriction.
    //
    // What the switch chooses is the spatial part:
    //
    //   hit point (default)  phi_k(x_h). Injecting a_k = phi_k(x_h) g is a
    //     point force at the hit: sum_k phi_k(x_h) phi_k(x) is a band-limited
    //     delta there. It ties the shimmer's character to where the plate was
    //     struck, which is playable, but it is not what the nonlinearity
    //     does.
    //
    //   modal  a fixed per-mode weight of the same rms. The von Karman cubic
    //     coupling Gamma_kpqr is an overlap integral over the whole plate; it
    //     contains no strike position at all, and the strike enters only by
    //     setting the modal amplitudes it runs on. This is the option that
    //     matches that, and it is the same choice the cascade *source*
    //     already makes for the same reason (see the header on why the drive
    //     uses 1/sqrt(A) rather than phi_k at the pickup).
    //
    // Position sensitivity survives either way: the hit still decides which
    // modes ring, hence the band sums, hence the cubic carrier. The modal
    // option stops position being applied a second time, not the first.
    const float* const w = current.cascModalInject ? cascInjRand : hitWeights;

    for (int k = 0; k < fem::maxModes; ++k)
        cascadeW[k] = (k >= cascadeSplit && k < activeModes && compensation[k] > 0.0f)
                        ? w[k] * cascInjW[k]
                        : 0.0f;
}

void PlateSynth::strike (float x, float y, float velocity)
{
    // The global path — mouse hits, and notes no source claims — has no
    // mapping of its own, so velocity scales the global Force as it always
    // did. Only the sources go through controlAmount.
    fireHammer (x, y, juce::jlimit (0.0f, 1.0f, velocity) * current.force,
                current.hammerMs);
}

void PlateSynth::fireHammer (float x, float y, float amplitude,
                             float hammerMs) noexcept
{
    if (model == nullptr || activeModes < 1)
        return;

    auto& h = hammers[nextHammer];
    nextHammer = (nextHammer + 1) % maxStrikes;

    for (float& w : h.weights)
        w = 0.0f;
    model->evalShapes (x, y, h.weights, activeModes);

    const double lenSamples = juce::jmax (1.0, fs * (double) hammerMs * 1.0e-3);
    h.phase = 0.0f;
    h.phaseInc = (float) (1.0 / lenSamples);
    h.amplitude = juce::jmax (0.0f, amplitude);
    h.active = true;

    // The cascade injects wherever the plate was last struck — the one point
    // the nonlinearity has a physical claim to (see the header). The external
    // input no longer follows it: the sources say where that goes.
    lastHitX = x;
    lastHitY = y;
    computeHitWeights (x, y);

    // Publish the position for the display. Written before the count is
    // released, so a reader that sees the count sees the point.
    const int n = hitCount.load (std::memory_order_relaxed);
    const int slot = n % hitRingSize;
    hitX[slot].store (x, std::memory_order_relaxed);
    hitY[slot].store (y, std::memory_order_relaxed);
    hitAmp[slot].store (h.amplitude, std::memory_order_relaxed);
    hitCount.store (n + 1, std::memory_order_release);
}

float PlateSynth::controlAmount (const Source& src, int control, float velocity) const noexcept
{
    if (control == fem::ctlVelocity)
        return fem::applyVelCurve (velocity, src.velCurve);
    if (control >= fem::ctlCcBase && control <= fem::ctlMax)
        return ccValue[(size_t) (control - fem::ctlCcBase)];
    return 0.0f;      // Off: hold the min end
}

void PlateSynth::randomSourcePoint (int s, float velocity, float& x, float& y) noexcept
{
    const auto& src = current.sources[s];

    // Where along the source's segment this strike lands: the 'a' end at 0,
    // the 'A' end at 1, whatever the position controller currently reads.
    // With the endpoints equal, which is the default, this is just the
    // source's point however the controller moves.
    const float t = controlAmount (src, src.posCtl, velocity);
    x = src.x + t * src.velX;
    y = src.y + t * src.velY;

    const float sigma = juce::jmax (0.0f, src.spread);
    if (sigma <= 0.0f || model == nullptr || model->mesh == nullptr)
        return;

    // Box-Muller, at strike rate rather than sample rate, so the transcendentals
    // are free in context. sigma is the per-axis standard deviation.
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const float u1 = juce::jmax (1.0e-7f, rng.nextFloat());
        const float u2 = rng.nextFloat();
        const float mag = sigma * std::sqrt (-2.0f * std::log (u1));
        const float ang = juce::MathConstants<float>::twoPi * u2;
        const float px = x + mag * std::cos (ang);
        const float py = y + mag * std::sin (ang);

        // A draw that lands off the plate excites nothing at all — evalShapes
        // would return silence and the hammer would be spent on nothing — so
        // it is redrawn rather than clamped. Clamping would pile hits onto the
        // boundary, which is exactly where most modes have a node.
        double bary[3];
        if (fxme::acoustics::findTriangle (*model->mesh, px, py, bary) >= 0)
        {
            x = px;
            y = py;
            return;
        }
    }
    // Eight misses means the point is close to an edge relative to the spread;
    // striking it exactly is a better answer than not striking at all.
}

void PlateSynth::strikeSource (int s, float velocity) noexcept
{
    if (s < 0 || s >= fem::maxSources)
        return;
    const auto& src = current.sources[s];
    if (! src.on)
        return;

    float x = 0.0f, y = 0.0f;
    randomSourcePoint (s, velocity, x, y);

    // min + amount * (max - min), with no ordering imposed on the pair: a min
    // above its max simply inverts the mapping, which is how a source is made
    // to hit softer the harder it is played.
    const float hamT = controlAmount (src, src.hammerCtl, velocity);
    const float frcT = controlAmount (src, src.forceCtl, velocity);
    const float hammerMs = (src.hammerMs + hamT * (src.hammerMsMax - src.hammerMs))
                             * current.srcHammerScale;
    const float force    = (src.force    + frcT * (src.forceMax    - src.force))
                             * current.srcForceScale;

    // Clamped to the range a source's own knobs span, so the trim can only
    // reach settings that could have been dialled in one at a time: a scale
    // of ten over an already-long hammer would otherwise produce a contact
    // time no control in the plugin can express.
    fireHammer (x, y, juce::jlimit (0.0f, 20.0f, force),
                juce::jlimit (0.1f, 50.0f, hammerMs));
}

int PlateSynth::strikeSourcesForNote (int note, float velocity) noexcept
{
    int fired = 0;
    for (int i = 0; i < fem::maxSources; ++i)
    {
        const auto& src = current.sources[i];
        if (src.on && src.note == note)
        {
            strikeSource (i, velocity);
            ++fired;
        }
    }
    return fired;
}

int PlateSynth::copyModalField (Field which, float* dest, int maxCount) const noexcept
{
    const int n = juce::jlimit (0, maxCount, fieldCount.load (std::memory_order_acquire));
    const auto* src = which == Field::velocity ? fieldV : fieldQ;
    for (int k = 0; k < n; ++k)
        dest[k] = src[(size_t) k].load (std::memory_order_relaxed);
    return n;
}

void PlateSynth::noteOn (float velocity) noexcept
{
    if (model == nullptr || activeModes < 1)
        return;

    // A note is a trigger here and nothing else; glideToNote does the pitch.
    // Two calls, because the processor routes them by MIDI channel: one
    // channel can play the plate while another tunes it, and eight sources
    // mapped to their own notes never fight over the tuning.
    strike (lastHitX, lastHitY, velocity);
}

void PlateSynth::glideToNote (int note) noexcept
{
    if (model == nullptr || activeModes < 1)
        return;

    const double hz = 440.0 * std::exp2 ((double) (note - 69) / 12.0);
    f1LogTarget = std::log2 (juce::jmax (1.0, hz));

    // The first note of the session lands directly: there is nothing to glide
    // from, and starting from whatever the Freq knob happened to be would put
    // a swoop on a note nobody asked to bend.
    const double distance = f1LogTarget - f1Log;
    const double ticksPerGlide = (double) current.glideMs * 1.0e-3 * fs
                                 / (double) nlUpdatePeriod;

    if (! notePlayed || ticksPerGlide < 1.0 || distance == 0.0)
    {
        f1Log = f1LogTarget;
        gliding = false;
        notePlayed = true;
        retuneBank();               // land in tune immediately
        return;
    }

    // Step per decimated tick, sized from the glide time, the sample rate and
    // the update period together. Distance is in log2, so the glide takes the
    // Glide time whatever the interval, and it takes the same wall-clock time
    // at any sample rate.
    glideStep = distance / ticksPerGlide;
    gliding = true;
    notePlayed = true;
}

void PlateSynth::updateGlide() noexcept
{
    bool arrived = false;
    if (gliding)
    {
        f1Log += glideStep;
        if ((glideStep > 0.0) == (f1Log >= f1LogTarget))
        {
            f1Log = f1LogTarget;
            gliding = false;
            arrived = true;
        }
    }

    // Same throttle as the Berger glide: rewrite the bank once the pitch has
    // drifted ~2 cents (2/1200 of an octave) from what it sounds at, and once
    // more on arrival so the note settles exactly in tune.
    if (arrived || std::abs (f1Log - f1LogTuned) > 0.00167)
        retuneBank();
}

void PlateSynth::updateDynamicTension() noexcept
{
    if (model == nullptr || activeModes < 1)
        return;

    const double target = juce::jlimit (0.0, gammaCap,
                                        nlGain * (double) current.nonlin * (double) envStretch);

    // Slew over gammaSlewTauSec so the glide is smooth. The coefficient
    // carries nlUpdatePeriod, because this runs on the decimated tick.
    gamma += gammaSlew * (target - gamma);

    // Retune only when mode 1 moved by more than ~2 cents:
    // d(omega)/omega = 0.5 * d(gamma) / (1 + gamma).
    if (std::abs (gamma - appliedGamma) > 0.0024 * (1.0 + appliedGamma))
        retuneBank();
}

void PlateSynth::processSample (float inL, float inR, float& outL, float& outR) noexcept
{
    outL = outR = 0.0f;
    if (activeModes < 1)
        return;


    const bool snapField = (--nlCountdown <= 0);
    if (snapField)
    {
        nlCountdown = nlUpdatePeriod;
        updateGlide();
        if (current.nonlin > 0.0f || gamma > 1.0e-4)
            updateDynamicTension();
    }

    // Cascade ladder: band b is pumped by the cubic of the two bands
    // directly below it (previous sample's band sums), through its own
    // attack/release gate. Band 0 receives nothing.
    float cascadeFb[numCascadeBands];
    cascadeFb[0] = 0.0f;
    if (cascEff > 0.0f)
    {
        const int window = juce::jlimit (1, numCascadeBands, current.cascWindow);
        for (int b = 1; b < numCascadeBands; ++b)
        {
            float src = 0.0f;
            for (int c = juce::jmax (0, b - window); c < b; ++c)
                src += prevBandOut[c];
            const float t = std::tanh (current.cascDrive * src);
            const float carrier = t * t * t;

            // Gate: rises towards 1 while the source is hot (faster near
            // the bottom of the ladder), releases slowly.
            const float target = juce::jmin (1.0f, 4.0f * std::abs (carrier));
            gateEnv[b] += (target > gateEnv[b] ? attackCoef[b] : releaseCoef)
                          * (target - gateEnv[b]);

            cascadeFb[b] = cascEff * cascadeInjectionAmp * gateEnv[b] * carrier;
        }
    }
    else
    {
        for (int b = 1; b < numCascadeBands; ++b)
        {
            cascadeFb[b] = 0.0f;
            gateEnv[b] = 0.0f;
        }
    }

    // Per-strike half-sine force pulses, shared by all modes this sample.
    float pulse[maxStrikes];
    int numPulses = 0;
    int pulseSlot[maxStrikes];
    for (int s = 0; s < maxStrikes; ++s)
    {
        auto& h = hammers[s];
        if (! h.active)
            continue;
        pulse[numPulses] = hammerGain * h.amplitude
                           * std::sin (juce::MathConstants<float>::pi * h.phase);
        pulseSlot[numPulses] = s;
        ++numPulses;
        h.phase += h.phaseInc;
        if (h.phase >= 1.0f)
            h.active = false;
    }

    // Depletion: extra per-sample state decay of each source band,
    // proportional to the mean gate activity of the bands it feeds.
    float bandDecay[numCascadeBands];
    for (int b = 0; b < numCascadeBands; ++b)
        bandDecay[b] = 1.0f;
    if (cascEff > 0.0f && current.cascDeplete > 0.0f)
    {
        const int window = juce::jlimit (1, numCascadeBands, current.cascWindow);
        for (int b = 0; b < numCascadeBands; ++b)
        {
            float act = 0.0f;
            int n = 0;
            for (int t = b + 1; t < numCascadeBands && t <= b + window; ++t)
            {
                act += gateEnv[t];
                ++n;
            }
            if (n > 0)
                bandDecay[b] = 1.0f - current.cascDeplete * cascEff
                                      * depletePerSample * (act / (float) n);
        }
    }

    float sumL = 0.0f, sumR = 0.0f;
    float meterSum = 0.0f;
    float stretch = 0.0f;
    float bandOut[numCascadeBands] = {};   // cascade source: band-mean velocity
    int band = 0;
    // Only up to the last mode still inside the audible band: the bank is
    // ordered by frequency, and everything above Nyquist is muted (b0 = 0),
    // so processing it would cost exactly as much as a sounding mode and
    // return silence. At a high Freq setting most of a large bank is up
    // there.
    for (int k = 0; k < liveModes; ++k)
    {
        while (band + 1 < numCascadeBands && k >= bandStart[band + 1])
            ++band;
        float x = inWL[k] * inL + inWR[k] * inR + cascadeW[k] * cascadeFb[band];
        for (int p = 0; p < numPulses; ++p)
            x += pulse[p] * hammers[pulseSlot[p]].weights[k];
        const float y = filters[k].processSample (x);
        stretch += nlWeight[k] * y * y;   // Berger driver, g_k q_k^2 term
        if (snapField)
        {
            fieldQ[k].store (dispScale[k] * y, std::memory_order_relaxed);
            fieldV[k].store (velScale[k] * y, std::memory_order_relaxed);
        }
        filters[k].z1 *= bandDecay[band];
        filters[k].z2 *= bandDecay[band];
        sumL += outAmpL[k] * y;
        sumR += outAmpR[k] * y;
        if (metering)
            meterSum += meterAmp[k] * y;
        bandOut[band] += cascSrcW[k] * y;
    }
    for (int b = 0; b < numCascadeBands; ++b)
        prevBandOut[b] = bandOut[b];
    if (snapField)
        fieldCount.store (liveModes, std::memory_order_release);

    // Global stretching, smoothed: an integral over the plate, so it does
    // not depend on where the pickup sits (see the header).
    envStretch += envCoef * (stretch - envStretch);
    meterOut = meterSum;
    outL = sumL;
    outR = sumR;
}

} // namespace fem
