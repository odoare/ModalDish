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

    Both features are linear mixes that collapse into two per-mode vectors
    (PlateSynth::updatePickupMix / updateSourceMix), which is what keeps the
    audio loop's cost independent of how many are enabled. These tests exist
    because that collapse is exactly the kind of indexing work that produces
    a plausible-sounding wrong answer.

    Exit code 0 when everything passes.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include <FxmeTools/acoustics/FemMesh.h>
#include <FxmeTools/acoustics/PlateModes.h>
#include <ModalModel.h>
#include <PlateSynth.h>
#include <cmath>
#include <cstdio>
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

static fem::PlateSynth::Params basePatch()
{
    fem::PlateSynth::Params p;
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
        (void) d;
    }

    // --- 1. A source strike scales with its own Force, not the global -----
    std::printf("\n== Per-source force ==\n");
    {
        double a=0,b=0,lo=0,hi=0;
        auto p = basePatch();
        p.force = 1.0f;                       // global, deliberately different
        p.sources[2].on = true; p.sources[2].x = 0.42f; p.sources[2].y = 0.45f;
        p.sources[2].force = 1.0f;
        fem::PlateSynth s1; s1.prepare(fs); s1.update(&model,p);
        s1.strikeSource(2, 1.0f);
        render(s1, 2.0, [](int,float&,float&){}, lo, a);

        p.sources[2].force = 4.0f;
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
        p.sources[0].on=true; p.sources[0].force=4.0f;
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

    std::printf("\n%s (%d failure%s)\n", failures==0?"ALL TESTS PASSED":"TESTS FAILED",
                failures, failures==1?"":"s");
    return failures==0?0:1;
}
