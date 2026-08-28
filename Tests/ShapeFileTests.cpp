/*
  ------------------------------------------------------------------------------
    ShapeFileTests.cpp

    The JSON geometry reader (Source/ShapeFile.cpp): coordinate mapping, the
    point-index-to-arc-parameter conversion behind the boundary map, and the
    rejections. Unlike the other suites this one needs juce_core, because the
    thing under test is a JSON parser — but only juce_core, not a host.

    The arithmetic here is the kind that produces a plausible-sounding wrong
    answer: a boundary landing one segment off, or a shape silently mirrored,
    both still load and still make a sound.

    Pass the Shapes/ directory as argv[1]. Exit code 0 when everything passes.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "ShapeFile.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
static void check (bool ok, const char* what)
{ std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

int main (int argc, char** argv)
{
    const juce::String dir = argc > 1 ? argv[1] : ".";

    // ---- the shipped examples parse -------------------------------------
    std::printf ("\n== Example files ==\n");
    for (const char* n : { "square.json", "trapezoid-gong.json", "notched-disc.json" })
    {
        const auto r = fem::shapefile::load (juce::File (dir + "/" + n));
        std::printf ("  %-22s %s%s\n", n, r.ok ? "ok" : "FAILED: ",
                     r.ok ? "" : r.error.toRawUTF8());
        check (r.ok, n);
    }

    // ---- coordinate mapping ---------------------------------------------
    std::printf ("\n== Mapping ==\n");
    {
        const auto r = fem::shapefile::parse (R"({
            "points": [[-1,-1],[1,-1],[1,1],[-1,1]] })");
        check (r.ok, "a full-range square parses");
        // [-1,1] must land on the canvas margin box, 0.08..0.92.
        double lo = 1e9, hi = -1e9;
        for (const auto& p : r.shape.outline) { lo = std::min (lo, p.x); hi = std::max (hi, p.x); }
        std::printf ("  x range %.4f .. %.4f (expect 0.0800 .. 0.9200)\n", lo, hi);
        check (std::abs (lo - 0.08) < 1e-9 && std::abs (hi - 0.92) < 1e-9,
               "[-1,1] maps onto the canvas margin box");
        // y up: the point given y=+1 must have the LARGER plate y.
        check (r.shape.outline[2].y > r.shape.outline[0].y,
               "y is up, not flipped to screen order");
        check (r.shape.segStarts.size() == 1 && r.shape.segBcs[0] == 1,
               "no boundary map gives one simply supported edge");
    }

    // ---- boundary map -> arc parameters ----------------------------------
    std::printf ("\n== Boundary map ==\n");
    {
        // Unit square, so each side is exactly a quarter of the perimeter.
        const auto r = fem::shapefile::parse (R"({
            "points": [[-1,-1],[1,-1],[1,1],[-1,1]],
            "boundary": { "2": "free", "0": "clamp" } })");
        check (r.ok, "an out-of-order boundary map parses");
        check (r.shape.segStarts.size() == 2, "two entries give two segments");
        std::printf ("  segStarts %.4f, %.4f   bcs %d, %d\n",
                     r.shape.segStarts[0], r.shape.segStarts[1],
                     r.shape.segBcs[0], r.shape.segBcs[1]);
        check (std::abs (r.shape.segStarts[0] - 0.0) < 1e-9
               && std::abs (r.shape.segStarts[1] - 0.5) < 1e-9,
               "keys sort into ascending arc order regardless of file order");
        check (r.shape.segBcs[0] == 0 && r.shape.segBcs[1] == 3,
               "clamp=0 then free=3, matching the stiffness enum");
    }
    {
        // Unequal sides: index 1 is a third of the way round, not a quarter.
        const auto r = fem::shapefile::parse (R"({
            "points": [[0,0],[0.5,0],[0.5,0.5],[0,0.5]],
            "boundary": { "1": "slide" } })");
        check (r.ok && r.shape.segStarts.size() == 1, "single-entry map parses");
        std::printf ("  segStart %.4f (expect 0.2500)\n", r.shape.segStarts[0]);
        check (std::abs (r.shape.segStarts[0] - 0.25) < 1e-9,
               "arc parameter follows length, not index");
        check (r.shape.segBcs[0] == 2, "slide = 2");
    }

    // ---- names and aliases ----------------------------------------------
    std::printf ("\n== Names ==\n");
    {
        const char* spellings[] = { "clamp", "Clamped", "CLAMP" };
        bool all = true;
        for (const char* sp : spellings)
        {
            const auto r = fem::shapefile::parse (
                juce::String (R"({"points":[[0,0],[1,0],[0,1]],"boundary":{"0":")")
                + sp + R"("}})");
            all = all && r.ok && r.shape.segBcs[0] == 0;
        }
        check (all, "clamp spellings are case- and suffix-insensitive");

        const auto num = fem::shapefile::parse (
            R"({"points":[[0,0],[1,0],[0,1]],"boundary":{"0":3}})");
        check (num.ok && num.shape.segBcs[0] == 3, "a bare integer condition works");
    }

    // ---- errors ----------------------------------------------------------
    std::printf ("\n== Rejections ==\n");
    struct Bad { const char* json; const char* what; };
    const Bad bad[] = {
        { R"(not json at all)",                                   "garbage" },
        { R"({"points":[[0,0],[1,0]]})",                          "only two points" },
        { R"({"name":"x"})",                                      "no points at all" },
        { R"({"points":[[0,0],[1,0],[0,2]]})",                    "a point outside [-1,1]" },
        { R"({"points":[[0,0],[1,0],["a",1]]})",                  "a non-numeric point" },
        { R"({"points":[[0,0],[1,0],[0,1]],"boundary":{"9":"free"}})", "an index past the end" },
        { R"({"points":[[0,0],[1,0],[0,1]],"boundary":{"x":"free"}})", "a non-numeric key" },
        { R"({"points":[[0,0],[1,0],[0,1]],"boundary":{"0":"welded"}})", "an unknown condition" },
        { R"({"points":[[0,0],[0,0],[0,0]]})",                    "a zero-length outline" },
        { R"({"points":[[0,0],[1,0],[0,1]],"meshDensity":200})",  "a mesh density out of range" },
    };
    for (const auto& b : bad)
    {
        const auto r = fem::shapefile::parse (b.json);
        std::printf ("  %-32s -> %s\n", b.what,
                     r.ok ? "ACCEPTED (wrong)" : r.error.toRawUTF8());
        check (! r.ok, b.what);
    }

    std::printf ("\n%s (%d failures)\n", failures ? "FAILURES" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
