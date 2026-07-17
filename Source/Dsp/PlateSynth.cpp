/*
  ------------------------------------------------------------------------------
    PlateSynth.cpp — see PlateSynth.h (including the nonlinearity notes).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PlateSynth.h"

namespace fem
{

namespace
{
    // A 0 dB-peak band-pass passes energy proportional to its bandwidth
    // (~ zeta), so lightly damped (narrow) modes would ring much quieter
    // than wide ones. Scale each mode by sqrt(zetaRef / zeta) to keep the
    // perceived level roughly constant across the damping range (the
    // MechanOdd modal-resonator compensation).
    float gainCompensation (float zeta) noexcept
    {
        constexpr float zetaRef = 0.01f;
        const float g = std::sqrt (zetaRef / juce::jmax (zeta, 1.0e-7f));
        return juce::jlimit (0.5f, 32.0f, g);
    }

    // Hammer forces are impulsive and benefit from a little headroom; this
    // keeps a velocity-1, force-1 hit around unity peak output.
    constexpr float hammerGain = 0.4f;

    // Berger calibration, in audible terms: gamma = nlGain * nonlin * <out^2>.
    // A hit ringing around out ~ 0.5 with nonlin = 1 gives gamma ~ 2 (mode 1
    // up ~9 semitones, relaxing as the ring decays). gammaCap bounds the
    // total glide whatever the level.
    constexpr double nlGain = 8.0;
    constexpr double gammaCap = 4.0;

    // Upward cascade: fb = cascade * cascadeAmp * tanh(cascadeB * outLow)^3,
    // injected into the modes above cascadeSplit only (acyclic: no stability
    // constraint). cascadeB places the knee around outLow ~ 1/cascadeB.
    constexpr float cascadeB = 2.0f;
    constexpr float cascadeAmp = 3.0f;

    // Modal-overlap floor for the cascade targets: the cubic's products are
    // broadband, but at plate dampings the target resonances are needles a
    // few Hz wide spaced ~0.64*f1 apart (overlap ~0.1), so most of the
    // injected energy falls between modes and is rejected — heard as
    // "distortion of some partials" instead of a wash. At full cascade the
    // target bandwidths are floored to cascadeOverlap times the local mode
    // spacing, turning the receiving comb into a quasi-continuum (and,
    // physically enough, shortening the high modes' ring).
    constexpr double cascadeOverlap = 0.7;
}

void PlateSynth::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    // ~20 ms energy follower: fast enough for the attack glide, slow enough
    // not to track individual cycles of the low modes.
    envCoef = 1.0f - std::exp ((float) (-1.0 / (0.02 * fs)));
    reset();
    dirty = true;
}

void PlateSynth::reset()
{
    for (auto& f : filters)
        f.reset();
    for (auto& h : hammers)
        h.active = false;
    envOut = 0.0f;
    gamma = appliedGamma = 0.0;
    nlCountdown = nlUpdatePeriod;
    prevOutLow = 0.0f;
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

    const bool tuningChanged =
           ! juce::approximatelyEqual (params.f1,       current.f1)
        || ! juce::approximatelyEqual (params.tension,  current.tension)
        || ! juce::approximatelyEqual (params.viscDamp, current.viscDamp)
        || ! juce::approximatelyEqual (params.matDamp,  current.matDamp)
        || ! juce::approximatelyEqual (params.cascade,  current.cascade)   // overlap floor
        || ! juce::approximatelyEqual (params.outX,     current.outX)
        || ! juce::approximatelyEqual (params.outY,     current.outY)
        || params.numModes != current.numModes;

    current = params;
    if (dirty || tuningChanged)
    {
        retune();
        dirty = false;
    }
}

void PlateSynth::retune()
{
    activeModes = 0;
    if (model == nullptr || model->numModes() < 1)
        return;

    activeModes = juce::jlimit (0, juce::jmin (model->numModes(), fem::maxModes),
                                current.numModes);
    // The lowest quarter of the bank (the loud, struck modes) drives the
    // cascade; everything above it receives.
    cascadeSplit = juce::jmax (1, activeModes / 4);

    computeOutputWeights();
    computeInputWeights (lastHitX, lastHitY);
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
    const double maxFreq = 0.47 * fs;

    // First pass: frequencies (needed for the local mode spacing below).
    double freq[fem::maxModes];
    double nu[fem::maxModes];
    for (int k = 0; k < activeModes; ++k)
    {
        const double omegaSq = juce::jmax (0.0, lambda[(size_t) k] + dT * g[(size_t) k]);
        freq[k] = (double) current.f1 * std::sqrt (omegaSq / base1);
        nu[k] = std::sqrt (omegaSq / omega1SqEff);
    }

    for (int k = 0; k < activeModes; ++k)
    {
        if (nu[k] <= 0.0 || freq[k] < 20.0 || freq[k] > maxFreq)
        {
            // Keep the bank index aligned with the mode index so the shape
            // weights stay matched; the mode is silent and its filter is
            // muted (b0 = 0) so it cannot feed the feedback paths.
            outAmp[k] = 0.0f;
            compensation[k] = 1.0f;
            filters[k].c = fxme::BiquadCoeffs();
            filters[k].c.b0 = 0.0f;
            continue;
        }

        float zeta = juce::jlimit (1.0e-6f, 0.5f,
                                   (float) ((double) current.viscDamp / nu[k]
                                          + (double) current.matDamp * nu[k]));

        // Cascade targets: floor the bandwidth (2 zeta f) at cascadeOverlap
        // times the local mode spacing, scaled by the cascade knob, so the
        // receiving comb overlaps into a quasi-continuum when driven.
        if (k >= cascadeSplit && current.cascade > 0.0f)
        {
            const double spAbove = k + 1 < activeModes ? freq[k + 1] - freq[k]
                                 : k > 0               ? freq[k] - freq[k - 1] : 0.0;
            const double spBelow = k > 0 ? freq[k] - freq[k - 1] : spAbove;
            const double spacing = juce::jmax (0.0, 0.5 * (spAbove + spBelow));
            const double zetaFloor = (double) current.cascade * cascadeOverlap
                                     * spacing / (2.0 * freq[k]);
            zeta = juce::jmax (zeta, (float) juce::jlimit (0.0, 0.5, zetaFloor));
        }

        const float q = juce::jlimit (0.5f, 1.0e5f, 1.0f / (2.0f * zeta));
        filters[k].c = fxme::BiquadCoeffs::bandpass (fs, (float) freq[k], q);
        compensation[k] = gainCompensation (zeta);
        outAmp[k] = phiOut[k] * compensation[k];
    }

    appliedGamma = gamma;
    updateCascadeWeights();
}

void PlateSynth::computeOutputWeights()
{
    for (float& w : phiOut)
        w = 0.0f;
    if (model != nullptr)
        model->evalShapes (current.outX, current.outY, phiOut, activeModes);
}

void PlateSynth::computeInputWeights (float x, float y)
{
    for (float& w : inWeights)
        w = 0.0f;
    if (model != nullptr)
        model->evalShapes (x, y, inWeights, activeModes);
    updateCascadeWeights();
}

void PlateSynth::updateCascadeWeights() noexcept
{
    // Target (high) modes receive the distorted low-mode signal at the hit
    // point; dividing by each target's bandwidth compensation makes the
    // audible cascade level independent of the damping settings (the
    // compensation is re-applied on output). Source modes get zero: the
    // low -> high transfer graph stays acyclic, hence unconditionally
    // stable with no gain restriction.
    for (int k = 0; k < fem::maxModes; ++k)
        cascadeW[k] = (k >= cascadeSplit && k < activeModes)
                        ? inWeights[k] / juce::jmax (compensation[k], 0.5f)
                        : 0.0f;
}

void PlateSynth::strike (float x, float y, float velocity)
{
    if (model == nullptr || activeModes < 1)
        return;

    auto& h = hammers[nextHammer];
    nextHammer = (nextHammer + 1) % maxStrikes;

    for (float& w : h.weights)
        w = 0.0f;
    model->evalShapes (x, y, h.weights, activeModes);

    const double lenSamples = juce::jmax (1.0, fs * (double) current.hammerMs * 1.0e-3);
    h.phase = 0.0f;
    h.phaseInc = (float) (1.0 / lenSamples);
    h.amplitude = juce::jlimit (0.0f, 1.0f, velocity) * current.force;
    h.active = true;

    // The external input follows the last hit point.
    lastHitX = x;
    lastHitY = y;
    computeInputWeights (x, y);
}

void PlateSynth::updateDynamicTension() noexcept
{
    if (model == nullptr || activeModes < 1)
        return;

    const double target = juce::jlimit (0.0, gammaCap,
                                        nlGain * (double) current.nonlin * (double) envOut);

    // Slew over a few updates (~ a couple of ms) so the glide is smooth.
    gamma += 0.2 * (target - gamma);

    // Retune only when mode 1 moved by more than ~2 cents:
    // d(omega)/omega = 0.5 * d(gamma) / (1 + gamma).
    if (std::abs (gamma - appliedGamma) > 0.0024 * (1.0 + appliedGamma))
        retuneBank();
}

float PlateSynth::processSample (float input) noexcept
{
    if (activeModes < 1)
        return 0.0f;

    if (--nlCountdown <= 0)
    {
        nlCountdown = nlUpdatePeriod;
        if (current.nonlin > 0.0f || gamma > 1.0e-4)
            updateDynamicTension();
    }

    // Upward cascade: cubic of the previous sample's low-mode output,
    // injected into the high modes only (weights in cascadeW).
    float cascadeFb = 0.0f;
    if (current.cascade > 0.0f)
    {
        const float t = std::tanh (cascadeB * prevOutLow);
        cascadeFb = current.cascade * cascadeAmp * t * t * t;
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

    float out = 0.0f;
    float outLow = 0.0f;
    for (int k = 0; k < activeModes; ++k)
    {
        float x = inWeights[k] * input + cascadeW[k] * cascadeFb;
        for (int p = 0; p < numPulses; ++p)
            x += pulse[p] * hammers[pulseSlot[p]].weights[k];
        const float contrib = outAmp[k] * filters[k].process (x);
        out += contrib;
        if (k < cascadeSplit)
            outLow += contrib;
    }
    prevOutLow = outLow;
    envOut += envCoef * (out * out - envOut);
    return out;
}

} // namespace fem
