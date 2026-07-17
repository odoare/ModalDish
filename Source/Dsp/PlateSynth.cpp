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

    // Calibration of the Berger feedback: T_dyn = nlScale * nonlin * sum_k
    // nlWeight_k * env_k. With nlWeight ~ 1/omega1^2-ish and env ~ squared
    // signal level, nlScale = 1500 makes nonlin = 1 glide a hard hit by
    // roughly a fifth on a default plate.
    constexpr double nlScale = 1500.0;
    constexpr double maxDynTension = 2000.0;   // hard safety cap

    // Cascade feedback level: fb = cascade * cascadeGain * tanh(out)^3, so
    // the injected signal is bounded by cascade * cascadeGain whatever the
    // ring amplitude.
    constexpr float cascadeGain = 2.0f;
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
    for (float& e : env)
        e = 0.0f;
    tensionDyn = appliedTensionDyn = 0.0;
    nlCountdown = nlUpdatePeriod;
    prevOut = 0.0f;
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
    const double tEff = (double) current.tension + tensionDyn;
    const double dT = tEff - model->modes.tensionRef;

    const double omega1Sq = juce::jmax (1.0e-12, lambda[0] + dT * g[0]);
    const double maxFreq = 0.47 * fs;

    maxNlWeight = 0.0f;
    for (int k = 0; k < activeModes; ++k)
    {
        const double omegaSq = juce::jmax (0.0, lambda[(size_t) k] + dT * g[(size_t) k]);
        const double nu = std::sqrt (omegaSq / omega1Sq);
        const double freq = (double) current.f1 * nu;

        nlWeight[k] = (float) (g[(size_t) k] / juce::jmax (1.0e-12, omegaSq));
        maxNlWeight = juce::jmax (maxNlWeight, nlWeight[k]);

        if (nu <= 0.0 || freq < 20.0 || freq > maxFreq)
        {
            // Keep the bank index aligned with the mode index so the shape
            // weights stay matched; the mode is silent and its filter is
            // muted (b0 = 0) so it cannot feed the Berger energy tracker.
            outAmp[k] = 0.0f;
            filters[k].c = fxme::BiquadCoeffs();
            filters[k].c.b0 = 0.0f;
            continue;
        }

        const float zeta = juce::jlimit (1.0e-6f, 0.5f,
                                         (float) ((double) current.viscDamp / nu
                                                + (double) current.matDamp * nu));
        const float q = juce::jlimit (0.5f, 1.0e5f, 1.0f / (2.0f * zeta));
        filters[k].c = fxme::BiquadCoeffs::bandpass (fs, (float) freq, q);
        outAmp[k] = phiOut[k] * gainCompensation (zeta);
    }

    appliedTensionDyn = tensionDyn;
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

    // Berger tension target from the diagonal modal stretching estimate.
    double e = 0.0;
    for (int k = 0; k < activeModes; ++k)
        e += (double) nlWeight[k] * (double) env[k];
    const double target = juce::jlimit (0.0, maxDynTension,
                                        nlScale * (double) current.nonlin * e);

    // Slew over a few updates (~ a couple of ms) so the glide is smooth.
    tensionDyn += 0.2 * (target - tensionDyn);

    // Retune only when the induced pitch shift exceeds ~2 cents:
    // d(omega)/omega ~= dT * nlWeight / 2.
    const double shift = 0.5 * std::abs (tensionDyn - appliedTensionDyn) * (double) maxNlWeight;
    if (shift > 0.0012)
        retuneBank();
}

float PlateSynth::processSample (float input) noexcept
{
    if (activeModes < 1)
        return 0.0f;

    if (--nlCountdown <= 0)
    {
        nlCountdown = nlUpdatePeriod;
        if (current.nonlin > 0.0f || tensionDyn > 1.0e-6)
            updateDynamicTension();
    }

    // Cubic cascade feedback of the previous output sample, tanh-bounded so
    // the loop injection can never exceed cascade * cascadeGain.
    float feedback = 0.0f;
    if (current.cascade > 0.0f)
    {
        const float t = std::tanh (prevOut);
        feedback = current.cascade * cascadeGain * t * t * t;
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

    const float inject = input + feedback;

    float out = 0.0f;
    for (int k = 0; k < activeModes; ++k)
    {
        float x = inWeights[k] * inject;
        for (int p = 0; p < numPulses; ++p)
            x += pulse[p] * hammers[pulseSlot[p]].weights[k];
        const float y = filters[k].process (x);
        env[k] += envCoef * (y * y - env[k]);
        out += outAmp[k] * y;
    }
    prevOut = out;
    return out;
}

} // namespace fem
