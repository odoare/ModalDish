/*
  ------------------------------------------------------------------------------
    IoTests.cpp

    The plate's input/output topology, rendered through the real PlateSynth
    with no JUCE and no host (see Tests/juce_stub):

      1. Pickups: the pan law, the level, and that several sum.
      2. Sources: per-source force and velocity scaling.
      3. Sources: position spread, and that a scattered hit stays on the plate.
      4. Sources: input send and left/right balance.
      5. Sources: MIDI note dispatch.
      6. Published hit positions.
      7. Stereo spread of the statistical tail.
      8. Notes: pitch, glide, and that striking and tuning stay separate.

    Both features are linear mixes that collapse into two per-mode vectors
    (PlateSynth::updatePickupMix / updateSourceMix), which is what keeps the
    audio loop's cost independent of how many are enabled. These tests exist
    because that collapse is exactly the kind of indexing work that produces
    a plausible-sounding wrong answer.

    Exit code 0 when everything passes.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include <FxmeTools/acoustics/FemMesh.h>
#include <FxmeTools/acoustics/PlateModes.h>
#include <ModalModel.h>
#include <PlateSynth.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace fxme::acoustics;
static constexpr double fs = 48000.0;
static int failures = 0;
static void check (bool ok, const char* what)
{ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", what); if(!ok) ++failures; }

static fem::ModalModel buildModel()
{
    std::vector<Point2> o;
    for (int i=0;i<96;++i){ const double a=2.0*M_PI*i/96;
        o.push_back({0.5+0.42*std::cos(a), 0.5+0.30*std::sin(a)}); }
    auto mesh = std::make_shared<FemMesh>(generateMesh(o, 1.0/16.0));
    BoundarySpec bc; bc.segmentStart={0.0}; bc.segmentBc={BoundaryCondition::SimplySupported};
    ModalOptions opt; opt.numModes = 96;
    fem::ModalModel m; m.mesh = mesh; m.modes = computePlateModes(*mesh, bc, opt);
    return m;
}

/** The plugin's statistical tail in miniature: plane-wave "shapes" bolted on
    above the last computed mode, at constant modal density. updateTailSpread
    acts only up there, so the FEM-only model above would not exercise it. */
static void appendTail (fem::ModalModel& m, int total)
{
    auto& lam = m.modes.lambda;
    auto& g = m.modes.tensionG;
    double omega = std::sqrt (lam.back());
    const double dOmega = omega / (double) m.numFemModes();
    std::uint32_t seed = 12345u;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u;
                         return (double) seed / 4294967296.0; };
    while ((int) lam.size() < total)
    {
        omega += dOmega;
        lam.push_back (omega * omega);
        g.push_back (omega);
        fem::ModalModel::TailWave w;
        const double kappa = std::sqrt (omega), th = 2.0 * M_PI * rnd();
        w.kx = (float) (kappa * std::cos (th));
        w.ky = (float) (kappa * std::sin (th));
        w.phase = (float) (2.0 * M_PI * rnd());
        w.amp = 1.5f;
        m.tailWaves.push_back (w);
    }
}

/** One band of a steep (three-section) band-pass, as an interchannel level
    difference in dB. Three sections because a single biquad's skirts let the
    louder bands below leak in and wash the difference out. */
static double bandIldDb (const std::vector<float>& L, const std::vector<float>& R,
                         double centre, double width)
{
    struct Bq
    {
        double b0=0,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void set (double f, double q)
        {
            const double w = 2.0*M_PI*f/fs, al = std::sin(w)/(2.0*q), c = std::cos(w);
            const double a0 = 1.0+al;
            b0 = al/a0; b1 = 0.0; b2 = -al/a0; a1 = (-2.0*c)/a0; a2 = (1.0-al)/a0;
        }
        double run (double x){ const double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
    };
    Bq bl[3], br[3];
    for (int i = 0; i < 3; ++i)
    {
        bl[i].set (centre, 0.5098 * centre / width);   // 0.5098 restores the
        br[i].set (centre, 0.5098 * centre / width);   // width after cascading
    }
    double sll = 0.0, srr = 0.0;
    const size_t skip = (size_t) (0.02 * fs);
    for (size_t i = 0; i < L.size(); ++i)
    {
        double a = L[i], b = R[i];
        for (int j = 0; j < 3; ++j) { a = bl[j].run (a); b = br[j].run (b); }
        if (i < skip) continue;
        sll += a*a; srr += b*b;
    }
    return 10.0 * std::log10 (std::max (sll, 1e-30) / std::max (srr, 1e-30));
}

static fem::PlateSynth::Params basePatch()
{
    fem::PlateSynth::Params p;
    // Cascade off: these tests are about the linear input/output topology,
    // and a running ladder would put its own energy into every level they
    // measure (and widen the target modes through the Overlap floor).
    p.f1 = 110.0f; p.numModes = 128; p.cascade = 0.0f; p.nonlin = 0.0f;
    p.pickups[0].on = true;
    return p;
}

// peak of a render, driving the synth with the given stereo input
template <class Fn>
static void render (fem::PlateSynth& s, double seconds, Fn&& perSample,
                    double& peakL, double& peakR)
{
    peakL = peakR = 0.0;
    const int n = (int)(seconds*fs);
    for (int i=0;i<n;++i)
    {
        float il=0.0f, ir=0.0f; perSample(i, il, ir);
        float l=0.0f, r=0.0f; s.processSample(il, ir, l, r);
        peakL = std::max(peakL,(double)std::abs(l));
        peakR = std::max(peakR,(double)std::abs(r));
    }
}

int main()
{
    const auto model = buildModel();
    std::printf("model: %d FEM modes\n", model.modes.numModes());


    // --- 0. Pickups --------------------------------------------------------
    std::printf ("\n== Pickups ==\n");
    {
        auto p = basePatch();
        double l0 = 0.0, r0 = 0.0, d = 0.0;

        // A centred pickup at unity is the mono output this replaced: equal
        // power would put 0.707 in each channel, which would have made the
        // plugin 3 dB quieter for no reason a player would recognise.
        fem::PlateSynth s1; s1.prepare (fs); s1.update (&model, p);
        s1.strike (0.42f, 0.45f, 1.0f);
        render (s1, 2.0, [] (int, float&, float&) {}, l0, r0);
        std::printf ("  centred, unity: L %.5f  R %.5f\n", l0, r0);
        check (std::abs (l0 - r0) < 1e-9, "a centred pickup is equal in both channels");

        // Hard left: everything in L, silence in R, and +3 dB for equal power.
        p.pickups[0].pan = -1.0f;
        fem::PlateSynth s2; s2.prepare (fs); s2.update (&model, p);
        s2.strike (0.42f, 0.45f, 1.0f);
        double l1 = 0.0, r1 = 0.0;
        render (s2, 2.0, [] (int, float&, float&) {}, l1, r1);
        std::printf ("  hard left:      L %.5f  R %.5f (x%.3f vs centre)\n",
                     l1, r1, l1 / l0);
        check (r1 < 1e-9, "a hard-left pickup is silent on the right");
        check (std::abs (l1 / l0 - 1.41421356) < 0.01,
               "hard-panned is +3 dB in its own channel (equal power)");

        // Level, and a pickup that is off.
        p.pickups[0].pan = 0.0f;
        p.pickups[0].level = 0.5f;
        fem::PlateSynth s3; s3.prepare (fs); s3.update (&model, p);
        s3.strike (0.42f, 0.45f, 1.0f);
        double l2 = 0.0, r2 = 0.0;
        render (s3, 2.0, [] (int, float&, float&) {}, l2, r2);
        check (std::abs (l2 / l0 - 0.5) < 0.01, "level scales the pickup linearly");

        p.pickups[0].on = false;
        fem::PlateSynth s4; s4.prepare (fs); s4.update (&model, p);
        s4.strike (0.42f, 0.45f, 1.0f);
        double l3 = 0.0, r3 = 0.0;
        render (s4, 2.0, [] (int, float&, float&) {}, l3, r3);
        check (l3 < 1e-9 && r3 < 1e-9, "a plate with no pickup on is silent");

        // Two pickups at the same point sum; at opposite pans they split.
        p = basePatch();
        p.pickups[0].pan = -1.0f;
        p.pickups[1] = p.pickups[0];
        p.pickups[1].pan = 1.0f;
        p.pickups[1].on = true;
        fem::PlateSynth s5; s5.prepare (fs); s5.update (&model, p);
        s5.strike (0.42f, 0.45f, 1.0f);
        double l4 = 0.0, r4 = 0.0;
        render (s5, 2.0, [] (int, float&, float&) {}, l4, r4);
        std::printf ("  two co-located, opposite pans: L %.5f  R %.5f\n", l4, r4);
        check (std::abs (l4 - r4) < 1e-9, "opposite pans give a symmetric pair");
        check (std::abs (l4 / l0 - 1.41421356) < 0.01,
               "each channel carries one whole pickup");

        // Switching a pickup on *while running*. Every check above builds a
        // fresh synth, so each one gets a full retune and resamples all the
        // shapes; that path was never the broken one. A pickup that is off is
        // deliberately not sampled (its phiPickup row is zeroed), so turning
        // it on has to resample it — rebuilding the collapsed mix on top of
        // the zeros it left leaves the new pickup silent.
        p = basePatch();
        p.pickups[1].x = 0.30f; p.pickups[1].y = 0.62f;   // placed, still off
        fem::PlateSynth s6; s6.prepare (fs); s6.update (&model, p);
        p.pickups[1].on = true;                 // the only change: the switch
        s6.update (&model, p);                  // second update: no retune
        s6.strike (0.42f, 0.45f, 1.0f);
        double l5 = 0.0, r5 = 0.0;
        render (s6, 2.0, [] (int, float&, float&) {}, l5, r5);

        // The same patch reached by a rebuild, which resamples everything.
        fem::PlateSynth s7; s7.prepare (fs); s7.update (&model, p);
        s7.strike (0.42f, 0.45f, 1.0f);
        double l6 = 0.0, r6 = 0.0;
        render (s7, 2.0, [] (int, float&, float&) {}, l6, r6);
        std::printf ("  switched on live: L %.5f   rebuilt: L %.5f\n", l5, l6);
        check (std::abs (l5 / l6 - 1.0) < 0.01,
               "a pickup switched on at runtime is sampled, not left at zero");
        (void) d;
    }

    // --- 0a. Velocity moves the strike along the source's segment ----------
    // (x, y) is the velocity-0 end and (x2, y2) the full-velocity one, so a
    // hit lands on the straight line between them at t = velocity. Endpoints
    // left equal — the default and what an old session migrates to — must
    // reproduce a fixed point exactly.
    std::printf ("\n== Velocity-dependent source position ==\n");
    {
        auto p = basePatch();
        p.sources[0].on = true;
        p.sources[0].note = 60;
        p.sources[0].spread = 0.0f;      // deterministic: no random draw
        // The plugin exposes two absolute endpoints, 'a' and 'A', and hands
        // the synth their difference: base (0.30, 0.40), 'A' at (0.70, 0.60).
        p.sources[0].x = 0.30f; p.sources[0].y = 0.40f;
        p.sources[0].velX = 0.40f; p.sources[0].velY = 0.20f;

        fem::PlateSynth s; s.prepare (fs); s.update (&model, p);

        auto hitAt = [&] (float vel)
        {
            s.strikeSourcesForNote (60, vel);
            return s.getHit (s.getHitCount() - 1);
        };

        const auto lo  = hitAt (0.0f);
        const auto mid = hitAt (0.5f);
        const auto hi  = hitAt (1.0f);
        std::printf ("  v=0.0 (%.3f, %.3f)  v=0.5 (%.3f, %.3f)  v=1.0 (%.3f, %.3f)\n",
                     lo.x, lo.y, mid.x, mid.y, hi.x, hi.y);

        check (std::abs (lo.x - 0.30f) < 1e-6f && std::abs (lo.y - 0.40f) < 1e-6f,
               "velocity 0 strikes the 'a' end exactly");
        check (std::abs (hi.x - 0.70f) < 1e-6f && std::abs (hi.y - 0.60f) < 1e-6f,
               "full velocity strikes the 'A' end exactly");
        check (std::abs (mid.x - 0.50f) < 1e-6f && std::abs (mid.y - 0.50f) < 1e-6f,
               "half velocity lands halfway along the segment");

        // Linear, not merely monotonic: a quarter of the way is a quarter.
        const auto q = hitAt (0.25f);
        check (std::abs (q.x - 0.40f) < 1e-6f && std::abs (q.y - 0.45f) < 1e-6f,
               "the interpolation is linear in velocity");

        // No offset: the pre-segment behaviour, exactly. This is also the
        // struct's default, so a caller that never heard of the segment —
        // every other test below — gets a fixed point.
        p.sources[0].velX = 0.0f;
        p.sources[0].velY = 0.0f;
        fem::PlateSynth f; f.prepare (fs); f.update (&model, p);
        f.strikeSourcesForNote (60, 0.1f);
        const auto soft = f.getHit (f.getHitCount() - 1);
        f.strikeSourcesForNote (60, 1.0f);
        const auto hard = f.getHit (f.getHitCount() - 1);
        check (std::abs (soft.x - hard.x) < 1e-9f && std::abs (soft.y - hard.y) < 1e-9f,
               "equal endpoints make the position velocity-independent");
        check (std::abs (soft.x - 0.30f) < 1e-6f,
               "and that fixed point is the source's own");

        // Velocity still scales the hit as it always did; the segment moves
        // where it lands, not how hard.
        check (hard.amplitude > soft.amplitude * 5.0f,
               "velocity still drives amplitude, not only position");
    }

    // --- 0c. Min/max mappings and their controllers -------------------------
    // Each of position, hammer and force is min + amount * (max - min), where
    // amount comes from the selected controller. The pair is not ordered:
    // min above max inverts the mapping, which is the whole point of letting
    // a source hit softer the harder it is played.
    std::printf ("\n== Source controller mappings ==\n");
    {
        auto base = basePatch();
        base.sources[0].on = true;
        base.sources[0].note = 60;

        auto lastHit = [] (fem::PlateSynth& s) { return s.getHit (s.getHitCount() - 1); };

        // Force: Off holds the min, Velocity sweeps the pair.
        {
            auto p = base;
            p.sources[0].force = 2.0f; p.sources[0].forceMax = 9.0f;
            p.sources[0].forceCtl = fem::ctlOff;
            fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
            s.strikeSourcesForNote (60, 1.0f);
            const float off = lastHit (s).amplitude;
            check (std::abs (off - 2.0f) < 1e-4f, "Off holds a mapping at its min");

            p.sources[0].forceCtl = fem::ctlVelocity;
            fem::PlateSynth v; v.prepare (fs); v.update (&model, p);
            v.strikeSourcesForNote (60, 0.0f);
            const float lo = lastHit (v).amplitude;
            v.strikeSourcesForNote (60, 1.0f);
            const float hi = lastHit (v).amplitude;
            v.strikeSourcesForNote (60, 0.5f);
            const float mid = lastHit (v).amplitude;
            std::printf ("  force 2..9 by velocity: %.3f / %.3f / %.3f\n", lo, mid, hi);
            check (std::abs (lo - 2.0f) < 1e-4f && std::abs (hi - 9.0f) < 1e-4f,
                   "velocity sweeps force between its ends");
            check (std::abs (mid - 5.5f) < 1e-4f, "and does so linearly");
        }

        // Inverted pair: min above max, so playing harder hits softer.
        {
            auto p = base;
            p.sources[0].force = 9.0f; p.sources[0].forceMax = 2.0f;
            p.sources[0].forceCtl = fem::ctlVelocity;
            fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
            s.strikeSourcesForNote (60, 0.0f);
            const float lo = lastHit (s).amplitude;
            s.strikeSourcesForNote (60, 1.0f);
            const float hi = lastHit (s).amplitude;
            std::printf ("  inverted 9..2: v=0 -> %.3f, v=1 -> %.3f\n", lo, hi);
            check (lo > hi && std::abs (lo - 9.0f) < 1e-4f && std::abs (hi - 2.0f) < 1e-4f,
                   "a min above its max inverts the mapping");
        }

        // A CC drives a mapping, and does so independently of velocity.
        {
            auto p = base;
            p.sources[0].force = 0.0f; p.sources[0].forceMax = 4.0f;
            p.sources[0].forceCtl = fem::ctlCcBase + 74;
            fem::PlateSynth s; s.prepare (fs); s.update (&model, p);

            s.strikeSourcesForNote (60, 1.0f);
            check (std::abs (lastHit (s).amplitude) < 1e-4f,
                   "an unmoved CC reads zero, holding the min");

            s.setControllerValue (74, 1.0f);
            s.strikeSourcesForNote (60, 0.1f);      // velocity must not matter
            const float full = lastHit (s).amplitude;
            std::printf ("  CC74 at full, velocity 0.1 -> %.3f\n", full);
            check (std::abs (full - 4.0f) < 1e-4f,
                   "a CC drives the mapping regardless of velocity");

            s.setControllerValue (73, 0.0f);        // a different CC changes nothing
            s.strikeSourcesForNote (60, 0.1f);
            check (std::abs (lastHit (s).amplitude - 4.0f) < 1e-4f,
                   "and only the selected CC is listened to");
        }

        // The velocity curve shapes velocity only.
        {
            auto p = base;
            p.sources[0].force = 0.0f; p.sources[0].forceMax = 1.0f;
            p.sources[0].forceCtl = fem::ctlVelocity;

            auto at = [&] (int curve)
            {
                p.sources[0].velCurve = curve;
                fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
                s.strikeSourcesForNote (60, 0.5f);
                return s.getHit (s.getHitCount() - 1).amplitude;
            };
            const float slow = at (fem::velCurveSlow);
            const float lin  = at (fem::velCurveLinear);
            const float fast = at (fem::velCurveFast);
            std::printf ("  curve at velocity 0.5: slow %.3f, linear %.3f, fast %.3f\n",
                         slow, lin, fast);
            check (slow < lin && lin < fast, "slow, linear and fast are ordered");
            check (std::abs (lin - 0.5f) < 1e-4f, "linear is the identity");
            check (std::abs (slow - 0.25f) < 1e-4f && std::abs (fast - 0.70711f) < 1e-4f,
                   "slow squares the velocity and fast takes its root");
        }

        // Hammer time is mapped by the same machinery, on its own controller.
        {
            auto p = base;
            p.sources[0].hammerMs = 1.0f; p.sources[0].hammerMsMax = 20.0f;
            p.sources[0].hammerCtl = fem::ctlVelocity;
            p.sources[0].force = 1.0f; p.sources[0].forceMax = 1.0f;
            p.sources[0].forceCtl = fem::ctlOff;

            // A long contact is a duller strike, so the same force puts less
            // into the high modes: compare the peak of a rendered hit.
            auto peakAt = [&] (float vel)
            {
                fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
                s.strikeSourcesForNote (60, vel);
                double l = 0.0, r = 0.0;
                render (s, 0.5, [] (int, float&, float&) {}, l, r);
                return l;
            };
            const double shortHit = peakAt (0.0f);   // 1 ms
            const double longHit  = peakAt (1.0f);   // 20 ms
            std::printf ("  hammer 1 ms -> %.5f, 20 ms -> %.5f\n", shortHit, longHit);
            check (shortHit > longHit * 1.5,
                   "the hammer mapping changes the strike, not just a number");
        }

        // Position, hammer and force each answer to their own controller.
        {
            auto p = base;
            p.sources[0].x = 0.3f; p.sources[0].y = 0.4f;
            p.sources[0].velX = 0.3f; p.sources[0].velY = 0.0f;
            p.sources[0].posCtl = fem::ctlCcBase + 1;
            p.sources[0].forceCtl = fem::ctlVelocity;
            p.sources[0].force = 0.0f; p.sources[0].forceMax = 5.0f;

            fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
            s.setControllerValue (1, 1.0f);
            s.strikeSourcesForNote (60, 0.4f);
            const auto h = lastHit (s);
            std::printf ("  pos on CC1, force on velocity: (%.3f, %.3f) amp %.3f\n",
                         h.x, h.y, h.amplitude);
            check (std::abs (h.x - 0.6f) < 1e-4f,
                   "position followed its CC, not the velocity");
            check (std::abs (h.amplitude - 2.0f) < 1e-4f,
                   "while force followed the velocity, not the CC");
        }
    }

    // --- 0b. The point panel's pickup meter --------------------------------
    // Mono, pre-pan, post-level. A centred pickup at unity is exactly the
    // mono output (that is what the sqrt(2) in equalPowerPan buys), so the
    // meter and the left channel have to agree to the last bit there, and
    // then panning must move the channel while leaving the meter alone.
    std::printf ("\n== Pickup meter ==\n");
    {
        // peaks of the metered mono tap and of the left channel, same render
        double outPk = 0.0;
        auto meterPeak = [&outPk] (fem::PlateSynth& s, double seconds)
        {
            double pk = 0.0;
            outPk = 0.0;
            const int n = (int) (seconds * fs);
            for (int i = 0; i < n; ++i)
            {
                float l = 0.0f, r = 0.0f;
                s.processSample (0.0f, 0.0f, l, r);
                pk = std::max (pk, (double) std::abs (s.getMeteredSample()));
                outPk = std::max (outPk, (double) std::abs (l));
            }
            return pk;
        };

        auto p = basePatch();
        p.meteredPickup = 0;
        fem::PlateSynth m1; m1.prepare (fs); m1.update (&model, p);
        m1.strike (0.42f, 0.45f, 1.0f);
        const double centred = meterPeak (m1, 2.0);
        std::printf ("  centred unity: meter %.5f  L %.5f\n", centred, outPk);
        check (std::abs (centred / outPk - 1.0) < 1e-6,
               "a centred unity pickup meters exactly its own output");

        // Pan is the one thing the meter must ignore.
        p.pickups[0].pan = -1.0f;
        fem::PlateSynth m2; m2.prepare (fs); m2.update (&model, p);
        m2.strike (0.42f, 0.45f, 1.0f);
        const double panned = meterPeak (m2, 2.0);
        std::printf ("  hard left:     meter %.5f\n", panned);
        check (std::abs (panned / centred - 1.0) < 1e-6,
               "panning a pickup does not move its meter");

        // Level is the one thing it must follow.
        p.pickups[0].pan = 0.0f;
        p.pickups[0].level = 0.5f;
        fem::PlateSynth m3; m3.prepare (fs); m3.update (&model, p);
        m3.strike (0.42f, 0.45f, 1.0f);
        const double halved = meterPeak (m3, 2.0);
        check (std::abs (halved / centred - 0.5) < 1e-6,
               "level scales the meter linearly");

        // No panel open, and a panel on a pickup that is switched off.
        p = basePatch();
        p.meteredPickup = -1;
        fem::PlateSynth m4; m4.prepare (fs); m4.update (&model, p);
        m4.strike (0.42f, 0.45f, 1.0f);
        check (meterPeak (m4, 0.5) == 0.0, "no panel open meters nothing");

        p.meteredPickup = 1;                 // pickup 2, which is off
        fem::PlateSynth m5; m5.prepare (fs); m5.update (&model, p);
        m5.strike (0.42f, 0.45f, 1.0f);
        check (meterPeak (m5, 0.5) == 0.0, "a pickup that is off meters silence");

        // Switching the meter between pickups mid-flight, the same live path
        // the panel uses when one window closes and another opens.
        p = basePatch();
        p.pickups[1].x = 0.30f; p.pickups[1].y = 0.62f; p.pickups[1].on = true;
        p.meteredPickup = 0;
        fem::PlateSynth m6; m6.prepare (fs); m6.update (&model, p);
        m6.strike (0.42f, 0.45f, 1.0f);
        const double first = meterPeak (m6, 1.0);
        p.meteredPickup = 1;
        m6.update (&model, p);
        const double second = meterPeak (m6, 1.0);
        std::printf ("  pickup 1 %.5f -> pickup 2 %.5f\n", first, second);
        check (second > 0.0 && std::abs (second / first - 1.0) > 1e-3,
               "re-pointing the meter follows the other pickup");
    }

    // --- 1. A source strike scales with its own Force, not the global -----
    std::printf("\n== Per-source force ==\n");
    {
        double a=0,b=0,lo=0,hi=0;
        auto p = basePatch();
        p.force = 1.0f;                       // global, deliberately different
        p.sources[2].on = true; p.sources[2].x = 0.42f; p.sources[2].y = 0.45f;
        // Force is a min/max pair driven by a controller now; "Force N" as it
        // used to mean is min 0, max N, velocity.
        p.sources[2].force = 0.0f; p.sources[2].forceCtl = fem::ctlVelocity;
        p.sources[2].forceMax = 1.0f;
        fem::PlateSynth s1; s1.prepare(fs); s1.update(&model,p);
        s1.strikeSource(2, 1.0f);
        render(s1, 2.0, [](int,float&,float&){}, lo, a);

        p.sources[2].forceMax = 4.0f;
        fem::PlateSynth s2; s2.prepare(fs); s2.update(&model,p);
        s2.strikeSource(2, 1.0f);
        render(s2, 2.0, [](int,float&,float&){}, hi, b);
        const double ratio = hi/std::max(lo,1e-12);
        std::printf("  force 1 -> %.5f, force 4 -> %.5f (x%.2f)\n", lo, hi, ratio);
        check(std::abs(ratio-4.0) < 0.05, "source peak is linear in its own Force");
    }

    // --- 2. Velocity scales toward that maximum ---------------------------
    std::printf("\n== Velocity maps 0..Force ==\n");
    {
        auto p = basePatch();
        p.sources[0].on=true;
        p.sources[0].force = 0.0f; p.sources[0].forceMax = 4.0f;
        p.sources[0].forceCtl = fem::ctlVelocity;
        double half=0, full=0, d=0;
        fem::PlateSynth s1; s1.prepare(fs); s1.update(&model,p);
        s1.strikeSource(0, 0.5f); render(s1,2.0,[](int,float&,float&){},half,d);
        fem::PlateSynth s2; s2.prepare(fs); s2.update(&model,p);
        s2.strikeSource(0, 1.0f); render(s2,2.0,[](int,float&,float&){},full,d);
        std::printf("  velocity 0.5 -> %.5f, 1.0 -> %.5f (x%.2f)\n", half, full, full/half);
        check(std::abs(full/half - 2.0) < 0.05, "velocity scales the hit linearly");
    }

    // --- 3. Spread actually moves the hit, and stays on the plate ---------
    std::printf("\n== Position spread ==\n");
    {
        auto p = basePatch();
        p.sources[0].on=true; p.sources[0].x=0.5f; p.sources[0].y=0.47f;

        auto spreadOf = [&](float sigma, int hits)
        {
            p.sources[0].spread = sigma;
            fem::PlateSynth s; s.prepare(fs); s.update(&model,p);
            double lo=1e30, hi=-1e30; bool anySilent=false;
            for (int t=0;t<hits;++t)
            {
                fem::PlateSynth one; one.prepare(fs); one.update(&model,p);
                for (int w=0; w<t; ++w) one.strikeSource(0, 0.0f);  // advance the rng
                one.strikeSource(0, 1.0f);
                double pk=0,d=0; render(one, 0.5, [](int,float&,float&){}, pk, d);
                lo = std::min(lo,pk); hi = std::max(hi,pk);
                if (pk < 1e-9) anySilent = true;
            }
            return std::make_tuple(lo,hi,anySilent);
        };

        auto [lo0,hi0,sil0] = spreadOf(0.0f, 12);
        auto [lo1,hi1,sil1] = spreadOf(0.12f, 12);
        std::printf("  spread 0.00: peaks %.5f..%.5f\n", lo0, hi0);
        std::printf("  spread 0.12: peaks %.5f..%.5f\n", lo1, hi1);
        check(hi0 - lo0 < 1e-9, "spread 0 always strikes the same point");
        check(hi1 - lo1 > 1e-4, "spread > 0 moves the hit between strikes");
        check(! sil1, "every scattered hit still landed on the plate");
    }

    // --- 4. Input send and L/R balance ------------------------------------
    std::printf("\n== Input send and balance ==\n");
    {
        auto p = basePatch();
        p.sources[0].on=true; p.sources[0].send=1.0f; p.sources[0].pan=0.0f;
        auto tone = [](int i, float& l, float& r)
        { l = r = 0.2f*std::sin(2.0*M_PI*110.0*i/fs); };

        double pk=0,d=0;
        fem::PlateSynth s; s.prepare(fs); s.update(&model,p);
        render(s, 1.0, tone, pk, d);
        check(pk > 1e-4, "a source with a send passes the input to the plate");

        // Hard left source, right-only input -> nothing.
        p.sources[0].pan = -1.0f;
        fem::PlateSynth s2; s2.prepare(fs); s2.update(&model,p);
        double pkR=0; 
        render(s2, 1.0, [](int i, float& l, float& r)
               { l = 0.0f; r = 0.2f*std::sin(2.0*M_PI*110.0*i/fs); }, pkR, d);
        std::printf("  left-balanced source fed right channel only: peak %.3e\n", pkR);
        check(pkR < 1e-9, "balance -1 ignores the right channel");

        // ...and the same source fed the left channel does sound.
        double pkL=0;
        fem::PlateSynth s3; s3.prepare(fs); s3.update(&model,p);
        render(s3, 1.0, [](int i, float& l, float& r)
               { l = 0.2f*std::sin(2.0*M_PI*110.0*i/fs); r = 0.0f; }, pkL, d);
        check(pkL > 1e-4, "balance -1 still takes the left channel");

        // A source that is off injects nothing.
        p.sources[0].on = false;
        fem::PlateSynth s4; s4.prepare(fs); s4.update(&model,p);
        double pkOff=0;
        render(s4, 1.0, tone, pkOff, d);
        check(pkOff < 1e-9, "a source that is off passes no input");
    }

    // --- 5. Note dispatch --------------------------------------------------
    std::printf("\n== Note dispatch ==\n");
    {
        auto p = basePatch();
        p.sources[1].on=true; p.sources[1].note=60;
        p.sources[3].on=true; p.sources[3].note=60;   // two sources, same note
        p.sources[4].on=true; p.sources[4].note=64;
        p.sources[5].on=false; p.sources[5].note=60;  // off: must not fire
        fem::PlateSynth s; s.prepare(fs); s.update(&model,p);
        check(s.strikeSourcesForNote(60, 1.0f) == 2, "both sources on note 60 fire");
        check(s.strikeSourcesForNote(64, 1.0f) == 1, "one source on note 64 fires");
        check(s.strikeSourcesForNote(62, 1.0f) == 0, "an unmapped note fires nothing");
    }

    // --- 6. Published hit positions ---------------------------------------
    // The editor's hit markers read these. Nothing else knows where a hit
    // landed: a source scatters its own, and one note can fire several at
    // once, so a marker drawn from the position the caller asked for would be
    // wrong in exactly the cases the controls exist to make visible.
    std::printf ("\n== Published hit positions ==\n");
    {
        auto p = basePatch();
        p.sources[0].on = true; p.sources[0].x = 0.36f; p.sources[0].y = 0.52f;
        p.sources[1].on = true; p.sources[1].x = 0.62f; p.sources[1].y = 0.44f;
        p.sources[0].note = 60; p.sources[1].note = 60;

        fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
        check (s.getHitCount() == 0, "no hits before anything is struck");

        // A plain strike reports exactly where it was aimed.
        s.strike (0.44f, 0.41f, 1.0f);
        const auto h0 = s.getHit (s.getHitCount() - 1);
        std::printf ("  strike (0.440, 0.410) -> (%.3f, %.3f)\n", h0.x, h0.y);
        check (s.getHitCount() == 1, "a strike publishes one hit");
        check (std::abs (h0.x - 0.44f) < 1e-6f && std::abs (h0.y - 0.41f) < 1e-6f,
               "the published point is the struck point");

        // ...and how hard, which is what sizes the marker.
        check (std::abs (h0.amplitude - 1.0f) < 1e-6f,
               "the published amplitude is velocity x the force that applied");

        // A source with no spread reports its own point, not the last one.
        s.strikeSource (1, 1.0f);
        const auto h1 = s.getHit (s.getHitCount() - 1);
        std::printf ("  source b (0.620, 0.440) -> (%.3f, %.3f)\n", h1.x, h1.y);
        check (std::abs (h1.x - 0.62f) < 1e-6f && std::abs (h1.y - 0.44f) < 1e-6f,
               "a source publishes its own point");

        // Amplitude follows the source's own Force, not the global one, and
        // scales with velocity: a soft hit on a strong source and a hard hit
        // on a weak one are different sizes on the plate, as they should be.
        {
            auto q = basePatch();
            q.force = 1.0f;
            q.sources[3].on = true;
            q.sources[3].x = 0.5f; q.sources[3].y = 0.47f;
            // Force is a mapping now: 0 at velocity 0, 8 at full velocity,
            // which is what a plain "Force 8" used to mean.
            q.sources[3].force = 0.0f;
            q.sources[3].forceMax = 8.0f;
            q.sources[3].forceCtl = fem::ctlVelocity;

            fem::PlateSynth u; u.prepare (fs); u.update (&model, q);
            u.strikeSource (3, 1.0f);
            const float loud = u.getHit (u.getHitCount() - 1).amplitude;
            u.strikeSource (3, 0.25f);
            const float soft = u.getHit (u.getHitCount() - 1).amplitude;
            std::printf ("  source d (Force 8): velocity 1.00 -> %.2f, 0.25 -> %.2f\n",
                         loud, soft);
            check (std::abs (loud - 8.0f) < 1e-4f, "amplitude uses the source's own Force");
            check (std::abs (soft - 2.0f) < 1e-4f, "and scales with velocity");
        }

        // One note firing two sources publishes two distinct hits.
        const int before = s.getHitCount();
        const int fired = s.strikeSourcesForNote (60, 1.0f);
        const int after = s.getHitCount();
        const auto a0 = s.getHit (after - 2);
        const auto a1 = s.getHit (after - 1);
        std::printf ("  note 60 fired %d, published %d: (%.3f, %.3f) and (%.3f, %.3f)\n",
                     fired, after - before, a0.x, a0.y, a1.x, a1.y);
        check (after - before == 2, "two sources on one note publish two hits");
        check (std::abs (a0.x - a1.x) > 1e-3f, "and they are at different points");

        // Spread shows up as scatter in the published points, all on the plate.
        p.sources[0].spread = 0.10f;
        fem::PlateSynth t; t.prepare (fs); t.update (&model, p);
        float minX = 1e30f, maxX = -1e30f;
        bool allInside = true;
        for (int i = 0; i < 24; ++i)
        {
            t.strikeSource (0, 1.0f);
            const auto h = t.getHit (t.getHitCount() - 1);
            minX = std::min (minX, h.x); maxX = std::max (maxX, h.x);
            double bary[3];
            if (findTriangle (*model.mesh, h.x, h.y, bary) < 0)
                allInside = false;
        }
        std::printf ("  spread 0.10 over 24 hits: x in %.3f..%.3f\n", minX, maxX);
        check (maxX - minX > 1e-3f, "spread scatters the published points");
        check (allInside, "every published point is on the plate");

        // The ring wraps without the count going backwards.
        const int n0 = t.getHitCount();
        for (int i = 0; i < 3 * fem::PlateSynth::hitRingSize; ++i)
            t.strikeSource (0, 1.0f);
        check (t.getHitCount() == n0 + 3 * fem::PlateSynth::hitRingSize,
               "the hit count keeps counting past the ring size");
    }

    // --- 7. Stereo spread of the statistical tail -------------------------
    // Above the last FEM mode the shimmer is spread over hundreds of modes at
    // once, and each channel's gain on it tends to sum_k phi_k^2, which
    // converges to the same spatial average wherever the pickup sits. Left
    // alone, every part of that spectrum lands in the middle. updateTailSpread
    // takes a level difference per band from the *signed* sum of the readout
    // weights instead, which is a random walk and so never converges.
    //
    // Measured one auditory filter at a time, because that is the resolution
    // the listener has: bands much narrower than an ERB get summed back
    // together by the ear, and a coarser analysis than this averages the
    // scatter away and reports a success as a failure.
    std::printf ("\n== Tail stereo spread ==\n");
    {
        auto model2 = buildModel();
        appendTail (model2, 384);
        const auto& lam = model2.modes.lambda;
        const int nFem = model2.numFemModes();
        const double base = std::sqrt (lam[0]);

        double mean = 0.0;
        auto rmsIld = [&] (const fem::PlateSynth::Params& p, const char* what)
        {
            fem::PlateSynth s; s.prepare (fs); s.update (&model2, p);
            s.strike (0.42f, 0.45f, 1.0f);
            const int n = (int) (1.5 * fs);
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                float l = 0.0f, r = 0.0f;
                s.processSample (0.0f, 0.0f, l, r);
                L[(size_t) i] = l; R[(size_t) i] = r;
            }
            double s2 = 0.0, s1 = 0.0; int cnt = 0;
            for (int b = 1; b < 16; b += 2)   // spread across the tail range
            {
                const int k = nFem + (int) ((384 - nFem) * (double) b / 16.0);
                const double f = 110.0 * std::sqrt (lam[(size_t) k]) / base;
                if (f > 0.35 * fs) break;
                const double erb = 24.7 * (4.37 * f / 1000.0 + 1.0);
                const double ild = bandIldDb (L, R, f, erb);
                s2 += ild * ild; s1 += ild; ++cnt;
            }
            const double rms = cnt > 0 ? std::sqrt (s2 / cnt) : 0.0;
            mean = cnt > 0 ? s1 / cnt : 0.0;
            std::printf ("  %-28s rms ILD %.2f dB, mean %+.2f dB, over %d auditory filters\n",
                         what, rms, mean, cnt);
            return rms;
        };

        auto one = basePatch();
        one.numModes = 384;
        one.cascade = 1.0f;          // the ladder running, so the tail is fed
        one.pickups[0].x = 0.5f; one.pickups[0].y = 0.47f; one.pickups[0].pan = 0.0f;
        const double single = rmsIld (one, "one centred pickup:");

        auto two = one;
        two.pickups[0].x = 0.24f; two.pickups[0].y = 0.47f; two.pickups[0].pan = -1.0f;
        two.pickups[1].x = 0.63f; two.pickups[1].y = 0.33f; two.pickups[1].pan = +1.0f;
        two.pickups[1].on = true;
        const double pair = rmsIld (two, "two pickups, opposite pans:");

        check (single < 0.5,
               "one pickup cannot manufacture a stereo image out of the tail");
        check (pair > 3.0,
               "two separated pickups scatter the tail across the image");
        // Scatter, not shift: bands have to land on both sides of centre. A
        // spread that pushed every band the same way would only move the
        // shimmer off centre, which is what a delay would have done.
        check (std::abs (mean) < pair,
               "the tail is scattered across the image, not shifted to one side");
    }

    // --- 8. Notes: pitch, glide, and the separation of the two -------------
    // A note does two independent things, and the processor routes them by
    // MIDI channel, so the synth has to offer them as two calls that do not
    // imply each other: glideToNote must not strike, noteOn must not retune.
    std::printf ("\n== Notes, pitch and glide ==\n");
    {
        // Runs the synth for `ms` and returns where the pitch got to.
        auto advance = [&] (fem::PlateSynth& s, double ms, double rate)
        {
            const int n = (int) (ms * 1.0e-3 * rate);
            for (int i = 0; i < n; ++i)
            {
                float l = 0.0f, r = 0.0f;
                s.processSample (0.0f, 0.0f, l, r);
            }
            return (double) s.getBaseFrequency();
        };

        auto p = basePatch();
        p.glideMs = 50.0f;

        fem::PlateSynth s; s.prepare (fs); s.update (&model, p);
        s.glideToNote (69);                       // A4
        std::printf ("  first note (69): %.1f Hz\n", (double) s.getBaseFrequency());
        check (std::abs (s.getBaseFrequency() - 440.0f) < 1.0f,
               "a note tunes mode 1 to its pitch");

        // The first note lands directly: there is nothing to glide from.
        check (std::abs (advance (s, 1.0, fs) - 440.0) < 1.0,
               "the first note of the session does not glide");

        // The second does. Halfway through it must be on the way, not there.
        s.glideToNote (81);                       // A5, an octave up
        const double at5 = advance (s, 5.0, fs);
        const double at60 = advance (s, 55.0, fs);
        std::printf ("  glide 50 ms to note 81: 5 ms -> %.1f Hz, 60 ms -> %.1f Hz\n", at5, at60);
        check (at5 > 445.0 && at5 < 860.0, "a second note glides rather than jumping");
        check (std::abs (at60 - 880.0) < 1.0, "and arrives within the glide time");

        // Same glide, twice the sample rate, same wall-clock time. A step
        // picked per decimated update rather than per second would halve here.
        auto glidePosition = [&] (double rate)
        {
            auto q = basePatch();
            q.glideMs = 50.0f;
            fem::PlateSynth t; t.prepare (rate); t.update (&model, q);
            t.glideToNote (69);
            advance (t, 1.0, rate);
            t.glideToNote (81);
            return advance (t, 25.0, rate);       // half way through
        };
        const double half48 = glidePosition (48000.0);
        const double half96 = glidePosition (96000.0);
        std::printf ("  half way: 48 kHz %.1f Hz, 96 kHz %.1f Hz\n", half48, half96);
        check (std::abs (12.0 * std::log2 (half96 / half48)) < 0.2,
               "the glide takes the same time at any sample rate");

        // Tuning does not strike.
        fem::PlateSynth quiet; quiet.prepare (fs); quiet.update (&model, p);
        quiet.glideToNote (69);
        double pk = 0.0, d = 0.0;
        render (quiet, 0.5, [] (int, float&, float&) {}, pk, d);
        std::printf ("  peak after glideToNote alone: %.3e\n", pk);
        check (pk < 1e-9, "tuning the plate does not strike it");

        // ...and striking does not tune.
        fem::PlateSynth hit; hit.prepare (fs); hit.update (&model, p);
        hit.glideToNote (69);
        const float before = hit.getBaseFrequency();
        hit.noteOn (1.0f);
        check (std::abs (hit.getBaseFrequency() - before) < 1e-6f,
               "striking the plate does not move its pitch");
    }

    std::printf("\n%s (%d failure%s)\n", failures==0?"ALL TESTS PASSED":"TESTS FAILED",
                failures, failures==1?"":"s");
    return failures==0?0:1;
}
