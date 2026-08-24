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
    constexpr float zetaRef = 0.01f;

    float gainCompensation (float zeta) noexcept
    {
        const float g = std::sqrt (zetaRef / juce::jmax (zeta, 1.0e-7f));
        return juce::jlimit (0.5f, 32.0f, g);
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
    // fb_b = cascade * cascAmp * gate_b * tanh(cascDrive * src_b)^3, with
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

    // Depletion couples the two ends of the ladder: a source band's filter
    // states decay by an extra factor (1 - deplete * cascade * depleteRate
    // * activity) per sample, activity being the mean gate level of the
    // bands it feeds. At full depletion/activity the extra decay time is
    // 1/(depleteRate * fs) ~ 70 ms at 48 kHz: the lows audibly hand their
    // energy to the shimmer instead of ringing on untouched.
    constexpr float depleteRate = 3.0e-4f;

    // All of these are exposed as plugin parameters (CASCADE panel) while
    // the effect is being voiced; Params holds the defaults.
}

void PlateSynth::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    // ~20 ms energy follower: fast enough for the attack glide, slow enough
    // not to track individual cycles of the low modes.
    envCoef = 1.0f - std::exp ((float) (-1.0 / (0.02 * fs)));
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
        || ! juce::approximatelyEqual (params.cascade,  current.cascade)      // overlap floor
        || ! juce::approximatelyEqual (params.cascOverlap, current.cascOverlap)
        || ! juce::approximatelyEqual (params.outX,     current.outX)
        || ! juce::approximatelyEqual (params.outY,     current.outY)
        || params.numModes != current.numModes;

    const bool envChanged =
           ! juce::approximatelyEqual (params.cascAttackMs,  current.cascAttackMs)
        || ! juce::approximatelyEqual (params.cascReleaseMs, current.cascReleaseMs);

    current = params;
    cascEff = current.cascade;
    if (envChanged)
        updateCascadeEnvelopes();

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
            outAmp[k] = 0.0f;
            compensation[k] = 1.0f;
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
        // times the local mode spacing, scaled by the damping-compensated
        // cascade amount, so the receiving comb overlaps into a
        // quasi-continuum when driven.
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
        outAmp[k] = phiOut[k] * compensation[k];

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

int PlateSynth::copyModalField (Field which, float* dest, int maxCount) const noexcept
{
    const int n = juce::jlimit (0, maxCount, fieldCount.load (std::memory_order_acquire));
    const auto* src = which == Field::velocity ? fieldV : fieldQ;
    for (int k = 0; k < n; ++k)
        dest[k] = src[(size_t) k].load (std::memory_order_relaxed);
    return n;
}

void PlateSynth::noteOn (float frequencyHz, float velocity) noexcept
{
    if (model == nullptr || activeModes < 1)
        return;

    // Keep the fundamental itself in the audible band; the bank mutes any
    // partial that falls outside it anyway (retuneBank).
    const double target = std::log2 (juce::jlimit (20.0, 0.45 * fs,
                                                   (double) frequencyHz));
    const double glideSec = 1.0e-3 * (double) juce::jmax (0.0f, current.glideMs);

    if (glideSec <= 0.0 || ! notePlayed)
    {
        // No glide time, or nothing to glide *from* yet: land in tune now, so
        // the strike below is already at the right pitch.
        f1Log = f1LogTarget = target;
        gliding = false;
        retuneBank();
    }
    else
    {
        f1LogTarget = target;
        const double steps = juce::jmax (1.0, glideSec * fs / (double) nlUpdatePeriod);
        glideStep = (f1LogTarget - f1Log) / steps;
        gliding = std::abs (f1LogTarget - f1Log) > 1.0e-9;   // repeated note: nothing to do
    }

    notePlayed = true;
    strike (lastHitX, lastHitY, velocity);
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

            cascadeFb[b] = cascEff * current.cascAmp * gateEnv[b] * carrier;
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
                                      * depleteRate * (act / (float) n);
        }
    }

    float out = 0.0f;
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
        float x = inWeights[k] * input + cascadeW[k] * cascadeFb[band];
        for (int p = 0; p < numPulses; ++p)
            x += pulse[p] * hammers[pulseSlot[p]].weights[k];
        const float y = filters[k].process (x);
        const float contrib = outAmp[k] * y;
        stretch += nlWeight[k] * y * y;   // Berger driver, g_k q_k^2 term
        if (snapField)
        {
            fieldQ[k].store (dispScale[k] * y, std::memory_order_relaxed);
            fieldV[k].store (velScale[k] * y, std::memory_order_relaxed);
        }
        filters[k].z1 *= bandDecay[band];
        filters[k].z2 *= bandDecay[band];
        out += contrib;
        bandOut[band] += cascSrcW[k] * y;
    }
    for (int b = 0; b < numCascadeBands; ++b)
        prevBandOut[b] = bandOut[b];
    if (snapField)
        fieldCount.store (liveModes, std::memory_order_release);

    // Global stretching, smoothed: an integral over the plate, so it does
    // not depend on where the pickup sits (see the header).
    envStretch += envCoef * (stretch - envStretch);
    return out;
}

} // namespace fem
