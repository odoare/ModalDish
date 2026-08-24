/*
  ------------------------------------------------------------------------------
    CascadeMeasure.cpp

    Offline measurement of the mode cascade's drive, rendered through the real
    PlateSynth with no JUCE and no host (see Tests/juce_stub/JuceHeader.h).

    Why this exists
    ---------------
    The cascade used to read its source bands from the *audible* signal,
    phi_out(k) * c_k * y_k, where c_k = sqrt(zetaRef / zeta_k) is the loudness
    compensation. A band-pass ring has peak 2 zeta omega and that compensation
    removes only sqrt(zeta) of it, so a mode's audible peak grows as
    sqrt(zeta_k): raising a damping knob made every mode hit *harder*, and the
    ladder's cubic cubed the difference. Measured on the default plate, the
    4-20 kHz shimmer rose by 38.7 dB at 300 ms across the Viscous knob's range
    while the plate itself was only losing energy faster — a real plate cannot
    ring louder because it is damped more.

    The source is now the band-mean modal *velocity* (y_k * velScale_k,
    normalised per band by a constant fixed by the mode indices alone), which
    has no damping in it at all. This test renders one strike over both damping
    knobs and checks that the shimmer no longer tracks them, so the artefact
    cannot come back unnoticed. Nor is the source read at the pickup any more:
    weighted by phi_k(x_o), the drive moved by 18.2 dB as the output point was
    moved around the plate, which the cubic coupling of a real plate has no way
    of knowing about. It also checks that the Cascade knob still does something
    and that the effect stays amplitude-gated.

    Run: CascadeMeasure   (exit code 0 when every check passes)

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PlateSynth.h"

#include <cstdio>
#include <cstdint>
#include <vector>

using namespace fxme::acoustics;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double renderSeconds = 2.0;

    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (! ok)
            ++failures;
    }

    //==========================================================================
    // The plugin's default plate: slightly elliptic, four simply supported
    // segments, grid 16 (PluginProcessor::makeDefaultShape).
    double tailRand (std::uint64_t& s)
    {
        s += 0x9e3779b97f4a7c15ull;
        std::uint64_t k = s;
        k = (k ^ (k >> 30)) * 0xbf58476d1ce4e5b9ull;
        k = (k ^ (k >> 27)) * 0x94d049bb133111ebull;
        k ^= k >> 31;
        return (double) (k & 0xffffffffull) / 4294967296.0;
    }

    /** Mirrors PluginProcessor::appendStatisticalTail. */
    void appendStatisticalTail (fem::ModalModel& m, int targetTotal)
    {
        auto& lam = m.modes.lambda;
        auto& g = m.modes.tensionG;
        const int nFem = m.numFemModes();
        if (m.mesh == nullptr || nFem < 8 || (int) lam.size() >= targetTotal)
            return;

        const size_t iA = lam.size() / 2, iB = lam.size() - 1;
        const double dOmega = (std::sqrt (lam[iB]) - std::sqrt (lam[iA]))
                              / (double) juce::jmax ((size_t) 1, iB - iA);
        if (! (dOmega > 0.0))
            return;

        double area = 0.0;
        for (const auto& t : m.mesh->triangles)
        {
            const auto& a = m.mesh->vertices[(size_t) t[0]];
            const auto& b = m.mesh->vertices[(size_t) t[1]];
            const auto& c = m.mesh->vertices[(size_t) t[2]];
            area += 0.5 * std::abs ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
        }
        const float amp = (float) std::sqrt (2.0 / juce::jmax (area, 1.0e-6));

        std::uint64_t seed = 0x5eedULL + (std::uint64_t) nFem;
        double omega = std::sqrt (lam[iB]);
        while ((int) lam.size() < targetTotal)
        {
            omega += dOmega * (0.6 + 0.8 * tailRand (seed));
            lam.push_back (omega * omega);
            g.push_back (omega);

            fem::ModalModel::TailWave w;
            const double kappa = std::sqrt (omega);
            const double theta = juce::MathConstants<double>::twoPi * tailRand (seed);
            w.kx = (float) (kappa * std::cos (theta));
            w.ky = (float) (kappa * std::sin (theta));
            w.phase = (float) (juce::MathConstants<double>::twoPi * tailRand (seed));
            w.amp = amp;
            m.tailWaves.push_back (w);
        }
    }

    fem::ModalModel buildDefaultPlate()
    {
        std::vector<Point2> outline;
        for (int i = 0; i < 96; ++i)
        {
            const double a = juce::MathConstants<double>::twoPi * i / 96;
            outline.push_back ({ 0.5 + 0.42 * std::cos (a), 0.5 + 0.35 * std::sin (a) });
        }

        auto mesh = std::make_shared<const FemMesh> (generateMesh (outline, 1.0 / 16.0));

        BoundarySpec bc;
        bc.segmentStart = { 0.0, 0.25, 0.5, 0.75 };
        bc.segmentBc.assign (4, BoundaryCondition::SimplySupported);

        ModalOptions opt;
        opt.numModes = juce::jmin (fem::maxFemModes,
                                   juce::jmax (8, (mesh->numVertices() + mesh->numEdges()) / 6));

        fem::ModalModel model;
        model.mesh = mesh;
        model.modes = computePlateModes (*mesh, bc, opt);
        appendStatisticalTail (model, fem::maxModes);
        return model;
    }

    //==========================================================================
    struct Patch
    {
        float viscDamp = 1.0e-4f;
        float matDamp  = 7.0e-6f;
        float cascade  = 1.0f;
        float force    = 6.0f;
        float outX     = 0.5f;
        float outY     = 0.47f;
    };

    /** Renders one strike. When `modalProbe` is given it receives the modal
        velocity snapshot taken at `probeSeconds` — a picture of what the
        plate is doing, with no pickup in it. */
    std::vector<float> render (const fem::ModalModel& model, const Patch& patch,
                               std::vector<float>* modalProbe = nullptr,
                               double probeSeconds = 0.3)
    {
        fem::PlateSynth synth;
        synth.prepare (sampleRate);

        fem::PlateSynth::Params p;
        p.f1 = 110.0f;
        p.viscDamp = patch.viscDamp;
        p.matDamp = patch.matDamp;
        p.cascade = patch.cascade;
        p.force = patch.force;
        p.numModes = 192;
        p.outX = patch.outX;
        p.outY = patch.outY;
        synth.update (&model, p);
        synth.strike (0.38f, 0.45f, 1.0f);

        const int n = (int) (renderSeconds * sampleRate);
        const int probeAt = (int) (probeSeconds * sampleRate);
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            out[(size_t) i] = synth.processSample (0.0f);
            if (modalProbe != nullptr && i == probeAt)
            {
                modalProbe->assign ((size_t) fem::maxModes, 0.0f);
                const int got = synth.copyModalField (fem::PlateSynth::Field::velocity,
                                                      modalProbe->data(), fem::maxModes);
                modalProbe->resize ((size_t) got);
            }
        }
        return out;
    }

    /** RMS of the upper half of the bank: how much high-mode motion there is
        in the plate, whatever the pickup happens to see of it. */
    double highModeMotionDb (const std::vector<float>& modal)
    {
        const int n = (int) modal.size();
        double sum = 0.0;
        int count = 0;
        for (int k = n / 2; k < n; ++k)
        {
            sum += (double) modal[(size_t) k] * modal[(size_t) k];
            ++count;
        }
        return 20.0 * std::log10 (std::sqrt (sum / juce::jmax (1, count)) + 1.0e-30);
    }

    /** Level in dB of the signal above (or below) `cutoff`, in a 10 ms window
        centred on `atSeconds`. Two cascaded Butterworth-Q sections. */
    double bandLevelDb (const std::vector<float>& x, float cutoff, bool high, double atSeconds)
    {
        fxme::Biquad a, b;
        a.c = high ? fxme::BiquadCoeffs::highpass (sampleRate, cutoff, 0.707f)
                   : fxme::BiquadCoeffs::lowpass  (sampleRate, cutoff, 0.707f);
        b.c = a.c;

        const int centre = (int) (atSeconds * sampleRate);
        const int half = (int) (0.005 * sampleRate);
        const int from = juce::jmax (0, centre - half);
        const int to = juce::jmin ((int) x.size(), centre + half);

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < to; ++i)
        {
            const float y = b.processSample (a.processSample (x[(size_t) i]));
            if (i >= from)
            {
                sum += (double) y * y;
                ++count;
            }
        }
        return 20.0 * std::log10 (std::sqrt (sum / juce::jmax (1, count)) + 1.0e-30);
    }

    double shimmerDb (const std::vector<float>& x, double atSeconds)
    {
        return bandLevelDb (x, 4000.0f, true, atSeconds);
    }

    double spread (const std::vector<double>& v)
    {
        double lo = v[0], hi = v[0];
        for (double x : v) { lo = std::min (lo, x); hi = std::max (hi, x); }
        return hi - lo;
    }
}

//==============================================================================
int main()
{
    std::printf ("Building the default plate...\n");
    const auto model = buildDefaultPlate();
    std::printf ("  %d FEM modes + statistical tail = %d modes\n\n",
                 model.numFemModes(), model.numModes());
    if (model.numFemModes() < 8)
    {
        std::printf ("  [FAIL] the modal solve produced nothing to render\n");
        return 1;
    }

    const double probes[] = { 0.1, 0.3, 0.6 };

    // ---- 1. The shimmer must not track the damping knobs -------------------
    // Both sweeps stop short of the settings where the plate is genuinely dead
    // within the render (zetaV 1e-2, zetaM 1e-3): there the shimmer *should*
    // collapse, and does.
    std::printf ("== Cascade drive vs damping (4 kHz and up, dB) ==\n");
    std::printf ("  %-14s %8s %8s %8s\n", "", "100ms", "300ms", "600ms");

    std::vector<double> viscAt300;
    for (float zv : { 1.0e-5f, 1.0e-4f, 1.0e-3f })
    {
        Patch p; p.viscDamp = zv;
        const auto x = render (model, p);
        std::printf ("  Viscous %-6g", (double) zv);
        for (double t : probes)
            std::printf (" %8.1f", shimmerDb (x, t));
        std::printf ("\n");
        viscAt300.push_back (shimmerDb (x, 0.3));
    }

    std::vector<double> matAt300;
    for (float zm : { 7.0e-6f, 3.0e-5f, 1.0e-4f, 3.0e-4f })
    {
        Patch p; p.matDamp = zm;
        const auto x = render (model, p);
        std::printf ("  Material %-5g", (double) zm);
        for (double t : probes)
            std::printf (" %8.1f", shimmerDb (x, t));
        std::printf ("\n");
        matAt300.push_back (shimmerDb (x, 0.3));
    }

    std::printf ("\n  viscous spread %.1f dB, material spread %.1f dB\n",
                 spread (viscAt300), spread (matAt300));
    // Audio-driven, these spreads were 29.6 dB and 39.3 dB. What is left is
    // not drive: the ladder's own targets ring for longer or shorter with the
    // damping, and the middle rungs are sources for the upper ones, so a few
    // dB of variation is the physics doing its job. The bar is set to catch a
    // return of the artefact (which was four to five times larger), not to
    // pin the sound down.
    check (spread (viscAt300) < 10.0, "shimmer barely tracks the Viscous knob (< 10 dB)");
    check (spread (matAt300) < 10.0, "shimmer barely tracks the Material knob (< 10 dB)");

    // ---- 2. The drive must not depend on where the plate is listened to ----
    // Measured on the plate's own motion, not on the output: moving the
    // pickup legitimately changes what is *heard* (a microphone does that),
    // but it must not change what the cascade does to the plate. When the
    // source was summed with phi_k(x_o) this spread was 18.2 dB.
    std::printf ("\n== Pickup independence of the drive (high-mode motion, dB) ==\n");
    const float pickX[] = { 0.50f, 0.35f, 0.62f, 0.50f, 0.44f };
    const float pickY[] = { 0.47f, 0.40f, 0.55f, 0.30f, 0.62f };
    std::vector<double> byPickup;
    for (int i = 0; i < 5; ++i)
    {
        Patch p; p.outX = pickX[i]; p.outY = pickY[i];
        std::vector<float> modal;
        render (model, p, &modal);
        byPickup.push_back (highModeMotionDb (modal));
        std::printf ("  pickup (%.2f, %.2f)  %8.1f\n",
                     (double) pickX[i], (double) pickY[i], byPickup.back());
    }
    std::printf ("  spread %.2f dB\n", spread (byPickup));
    check (spread (byPickup) < 0.5, "the cascade drive ignores the output point");

    // ---- 3. ...but must still follow the Cascade knob ----------------------
    std::printf ("\n== Cascade knob (4 kHz and up, dB at 300 ms) ==\n");
    std::vector<double> byKnob;
    for (float c : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Patch p; p.cascade = c;
        const auto x = render (model, p);
        const double db = shimmerDb (x, 0.3);
        byKnob.push_back (db);
        std::printf ("  cascade %.2f  %8.1f\n", (double) c, db);
    }
    check (byKnob.back() - byKnob.front() > 10.0,
           "the Cascade knob raises the shimmer by more than 10 dB");
    check (byKnob[2] > byKnob[0] && byKnob.back() >= byKnob[2],
           "the Cascade knob is monotonic across its range");

    // ---- 4. ...and stay amplitude-gated -----------------------------------
    std::printf ("\n== Amplitude gating (4 kHz and up, dB at 300 ms) ==\n");
    std::vector<double> byForce;
    for (float f : { 1.0f, 4.0f, 10.0f })
    {
        Patch p; p.force = f;
        const auto x = render (model, p);
        const double db = shimmerDb (x, 0.3);
        byForce.push_back (db);
        std::printf ("  force %5.1f  %8.1f\n", (double) f, db);
    }
    check (byForce[2] - byForce[0] > 20.0,
           "a hard hit shimmers far more than a soft one (> 20 dB)");

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "All checks passed." : "FAILURES",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
