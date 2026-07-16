/*
  ------------------------------------------------------------------------------
    PlateSynth.cpp — see PlateSynth.h.

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
}

void PlateSynth::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
    dirty = true;
}

void PlateSynth::reset()
{
    for (auto& f : filters)
        f.reset();
    for (auto& h : hammers)
        h.active = false;
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

    const auto& lambda = model->modes.lambda;
    const auto& g = model->modes.tensionG;
    const double dT = (double) current.tension - model->modes.tensionRef;

    const double omega1Sq = juce::jmax (1.0e-12, lambda[0] + dT * g[0]);
    const int count = juce::jlimit (0, juce::jmin (model->numModes(), fem::maxModes),
                                    current.numModes);
    const double maxFreq = 0.47 * fs;

    for (int k = 0; k < count; ++k)
    {
        const double omegaSq = juce::jmax (0.0, lambda[(size_t) k] + dT * g[(size_t) k]);
        const double nu = std::sqrt (omegaSq / omega1Sq);
        const double freq = (double) current.f1 * nu;

        if (nu <= 0.0 || freq < 20.0 || freq > maxFreq)
        {
            // Keep the bank index aligned with the mode index so the shape
            // weights stay matched; the mode is simply silent.
            outAmp[k] = 0.0f;
            ++activeModes;
            continue;
        }

        const float zeta = juce::jlimit (1.0e-6f, 0.5f,
                                         (float) ((double) current.viscDamp / nu
                                                + (double) current.matDamp * nu));
        const float q = juce::jlimit (0.5f, 1.0e5f, 1.0f / (2.0f * zeta));
        filters[k].c = fxme::BiquadCoeffs::bandpass (fs, (float) freq, q);
        outAmp[k] = gainCompensation (zeta);
        ++activeModes;
    }

    // phi_k(out) folded into outAmp, phi_k(in) refreshed for the (possibly
    // new) model at the last hit point.
    computeOutputWeights();
    computeInputWeights (lastHitX, lastHitY);
}

void PlateSynth::computeOutputWeights()
{
    if (model == nullptr)
        return;
    float phi[fem::maxModes] {};
    model->evalShapes (current.outX, current.outY, phi, activeModes);
    for (int k = 0; k < activeModes; ++k)
        outAmp[k] *= phi[k];
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
    h.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    h.active = true;

    // The external input follows the last hit point.
    lastHitX = x;
    lastHitY = y;
    computeInputWeights (x, y);
}

float PlateSynth::processSample (float input) noexcept
{
    if (activeModes < 1)
        return 0.0f;

    // Per-strike half-sine force pulses, shared by all modes this sample.
    float pulse[maxStrikes];
    int numPulses = 0;
    int pulseSlot[maxStrikes];
    for (int s = 0; s < maxStrikes; ++s)
    {
        auto& h = hammers[s];
        if (! h.active)
            continue;
        pulse[numPulses] = h.velocity
                           * std::sin (juce::MathConstants<float>::pi * h.phase);
        pulseSlot[numPulses] = s;
        ++numPulses;
        h.phase += h.phaseInc;
        if (h.phase >= 1.0f)
            h.active = false;
    }

    // Hammer forces are impulsive and benefit from a little headroom; the
    // constant keeps a full-velocity hit around unity peak output.
    constexpr float hammerGain = 0.4f;

    float out = 0.0f;
    for (int k = 0; k < activeModes; ++k)
    {
        float x = inWeights[k] * input;
        for (int p = 0; p < numPulses; ++p)
            x += hammerGain * pulse[p] * hammers[pulseSlot[p]].weights[k];
        out += outAmp[k] * filters[k].process (x);
    }
    return out;
}

} // namespace fem
