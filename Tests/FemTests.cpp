/*
  ------------------------------------------------------------------------------
    FemTests.cpp

    Console validation of the fxme::acoustics plate FEM (JUCE-free):

      1. Simply supported unit square: eigenvalues lambda_mn = pi^4 (m^2+n^2)^2
         and tension coefficients g_mn = pi^2 (m^2+n^2).
      2. Clamped circle: frequency ratios omega_k/omega_1 vs the classic
         Leissa values (2.081, 3.414, 3.893 for radius-independent ratios).
      3. Mixed boundary sanity: clamped/free edges, mode shapes vanish on the
         clamped side and not on the free side.
      4. Mesh/point-location round trip.

    Exit code 0 when everything passes.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include <FxmeTools/acoustics/FemMesh.h>
#include <FxmeTools/acoustics/PlateModes.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace fxme::acoustics;

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++failures;
}

static void checkClose (double got, double expected, double relTol, const char* what)
{
    const double rel = std::abs (got - expected) / std::max (std::abs (expected), 1e-30);
    std::printf ("  [%s] %s: got %.6g, expected %.6g (rel err %.2e, tol %.1e)\n",
                 rel <= relTol ? "PASS" : "FAIL", what, got, expected, rel, relTol);
    if (rel > relTol)
        ++failures;
}

static std::vector<Point2> rectangle (double w, double h)
{
    return { { 0.0, 0.0 }, { w, 0.0 }, { w, h }, { 0.0, h } };
}

static std::vector<Point2> circle (double r, int npts = 128)
{
    std::vector<Point2> out;
    for (int i = 0; i < npts; ++i)
    {
        const double a = 2.0 * M_PI * i / npts;
        out.push_back ({ 0.5 + r * std::cos (a), 0.5 + r * std::sin (a) });
    }
    return out;
}

static void testSimplySupportedSquare()
{
    std::printf ("\n== Simply supported unit square ==\n");
    const auto mesh = generateMesh (rectangle (1.0, 1.0), 1.0 / 16.0);
    std::printf ("  mesh: %d vertices, %d triangles, %d edges\n",
                 mesh.numVertices(), mesh.numTriangles(), mesh.numEdges());
    check (mesh.numTriangles() > 200, "mesh has a sensible triangle count");

    BoundarySpec bc;
    bc.segmentStart = { 0.0 };
    bc.segmentBc = { BoundaryCondition::SimplySupported };

    ModalOptions opt;
    opt.numModes = 10;
    const auto res = computePlateModes (mesh, bc, opt);
    check (res.numModes() == 10, "requested modes returned");
    if (res.numModes() < 10)
        return;

    const double pi4 = M_PI * M_PI * M_PI * M_PI;
    // lambda_mn = pi^4 (m^2 + n^2)^2 : 4, 25, 25, 64, 100, 100, 169, 169, ...
    // Morley converges from below at O(h^2), higher modes carry larger
    // discretisation error at a given h; tolerances follow measured errors
    // at h = 1/16 with ~40% margin.
    const double expected[8] = { 4, 25, 25, 64, 100, 100, 169, 169 };
    const double tol[8] = { 0.03, 0.06, 0.06, 0.09, 0.11, 0.11, 0.14, 0.14 };
    for (int k = 0; k < 8; ++k)
    {
        char buf[64];
        std::snprintf (buf, sizeof (buf), "lambda[%d]", k);
        checkClose (res.lambda[(size_t) k], pi4 * expected[k], tol[k], buf);
    }

    // Convergence rate: refining h = 1/10 -> 1/20 must shrink the error of
    // lambda[0] by ~4x (O(h^2)); require at least 3x.
    {
        ModalOptions o1;
        o1.numModes = 1;
        const auto rc = computePlateModes (generateMesh (rectangle (1.0, 1.0), 1.0 / 10.0), bc, o1);
        const auto rf = computePlateModes (generateMesh (rectangle (1.0, 1.0), 1.0 / 20.0), bc, o1);
        const double ec = std::abs (rc.lambda[0] - 4.0 * pi4);
        const double ef = std::abs (rf.lambda[0] - 4.0 * pi4);
        std::printf ("  convergence: err(h=1/10) = %.3g, err(h=1/20) = %.3g, ratio %.2f\n",
                     ec, ef, ec / ef);
        check (ec / ef > 3.0, "eigenvalue error shrinks ~O(h^2) under refinement");
    }

    // Tension coefficient of mode (1,1): g = pi^2 (m^2+n^2) = 2 pi^2.
    checkClose (res.tensionG[0], 2.0 * M_PI * M_PI, 0.03, "tensionG[0] (membrane term)");

    // Mode (1,1) shape: mass-normalised phi = 2 sin(pi x) sin(pi y),
    // so |phi| at the centre should be 2.
    int hint = -1;
    const double centreVal = std::abs (evalNodalField (mesh, res.shapes[0], 0.5, 0.5, &hint));
    checkClose (centreVal, 2.0, 0.05, "mode (1,1) amplitude at centre");

    // Rectangle 1 x 0.7 frequency law: omega ~ m^2 + (n/0.7)^2.
    const auto mesh2 = generateMesh (rectangle (1.0, 0.7), 1.0 / 16.0);
    const auto res2 = computePlateModes (mesh2, bc, opt);
    if (res2.numModes() >= 2)
    {
        const double r11 = 1.0 + 1.0 / 0.49;
        const double r21 = 4.0 + 1.0 / 0.49;
        checkClose (std::sqrt (res2.lambda[1] / res2.lambda[0]), r21 / r11, 0.03,
                    "1 x 0.7 rectangle: omega2/omega1");
    }
    else
        check (false, "rectangle 1 x 0.7 solved");
}

static void testClampedCircle()
{
    std::printf ("\n== Clamped circular plate ==\n");
    const auto mesh = generateMesh (circle (0.45), 1.0 / 22.0);
    std::printf ("  mesh: %d vertices, %d triangles\n", mesh.numVertices(), mesh.numTriangles());

    BoundarySpec bc;
    bc.segmentStart = { 0.0 };
    bc.segmentBc = { BoundaryCondition::Clamped };

    ModalOptions opt;
    opt.numModes = 8;
    const auto res = computePlateModes (mesh, bc, opt);
    check (res.numModes() == 8, "requested modes returned");
    if (res.numModes() < 6)
        return;

    // omega/omega1 ratios for the clamped circle (Leissa): (1,1)/(0,1) = 2.0808..
    // Modes (1,1) and (2,1) are degenerate pairs (cos/sin).
    const double w1 = std::sqrt (res.lambda[0]);
    checkClose (std::sqrt (res.lambda[1]) / w1, 2.0808, 0.03, "omega(1,1)/omega(0,1)");
    checkClose (std::sqrt (res.lambda[2]) / w1, 2.0808, 0.03, "omega(1,1)b/omega(0,1)");
    checkClose (std::sqrt (res.lambda[3]) / w1, 3.4141, 0.04, "omega(2,1)/omega(0,1)");
    checkClose (std::sqrt (res.lambda[4]) / w1, 3.4141, 0.04, "omega(2,1)b/omega(0,1)");
    checkClose (std::sqrt (res.lambda[5]) / w1, 3.8933, 0.05, "omega(0,2)/omega(0,1)");

    // Absolute check: lambda1 = (beta01/R)^4 with beta01^2 = 10.2158,
    // R = 0.45 -> omega1 = 10.2158 / 0.2025.
    checkClose (w1, 10.2158 / (0.45 * 0.45), 0.03, "omega(0,1) absolute");

    // Clamped edge: the first mode must vanish at the rim.
    int hint = -1;
    const double rim = std::abs (evalNodalField (mesh, res.shapes[0], 0.5 + 0.449, 0.5, &hint));
    hint = -1;
    const double ctr = std::abs (evalNodalField (mesh, res.shapes[0], 0.5, 0.5, &hint));
    check (rim < 0.05 * ctr, "first mode vanishes at the clamped rim");
}

static void testMixedBoundary()
{
    std::printf ("\n== Mixed boundary (half clamped, half free) square ==\n");
    const auto mesh = generateMesh (rectangle (1.0, 1.0), 1.0 / 14.0);

    // Outline walks (0,0)->(1,0)->(1,1)->(0,1). Arc params: bottom = [0,0.25),
    // right = [0.25,0.5), top = [0.5,0.75), left = [0.75,1). Clamp the bottom
    // and right, leave top and left free (a cantilever-ish corner plate).
    BoundarySpec bc;
    bc.segmentStart = { 0.0, 0.5 };
    bc.segmentBc = { BoundaryCondition::Clamped, BoundaryCondition::Free };

    ModalOptions opt;
    opt.numModes = 4;
    const auto res = computePlateModes (mesh, bc, opt);
    check (res.numModes() == 4, "requested modes returned");
    if (! res.valid())
        return;

    // Just inside the clamped side the deflection is O(d^2) of the distance
    // to the edge (w = 0 and dw/dn = 0), so the linear interpolation there
    // must be a tiny fraction of the free-corner amplitude.
    int hint = -1;
    const double clampedSide = std::abs (evalNodalField (mesh, res.shapes[0], 0.5, 0.002, &hint));
    hint = -1;
    const double freeCorner = std::abs (evalNodalField (mesh, res.shapes[0], 0.02, 0.98, &hint));
    check (freeCorner > 0.5, "mode 1 moves at the free corner");
    check (clampedSide < 0.02 * freeCorner, "mode 1 vanishes on the clamped side");

    // Frequencies must interlace sensibly: fully clamped > mixed > fully free.
    BoundarySpec allClamped;
    allClamped.segmentStart = { 0.0 };
    allClamped.segmentBc = { BoundaryCondition::Clamped };
    const auto resC = computePlateModes (mesh, allClamped, opt);
    check (resC.valid() && resC.lambda[0] > res.lambda[0],
           "fully clamped plate is stiffer than the mixed one");
}

static void testTensionAndMesh()
{
    std::printf ("\n== Tension reference + mesh utilities ==\n");
    const auto poly = circle (0.4, 96);
    const auto mesh = generateMesh (poly, 1.0 / 16.0);

    // Boundary params must be present exactly on boundary edges.
    int nBoundary = 0, nParamOk = 0;
    for (int e = 0; e < mesh.numEdges(); ++e)
        if (mesh.isBoundaryEdge (e))
        {
            ++nBoundary;
            if (mesh.edgeParam[(size_t) e] >= 0.0 && mesh.edgeParam[(size_t) e] < 1.0)
                ++nParamOk;
        }
    check (nBoundary > 20, "boundary edges found");
    check (nBoundary == nParamOk, "every boundary edge carries an arc parameter");

    double bary[3];
    check (findTriangle (mesh, 0.5, 0.5, bary) >= 0, "centre point located");
    check (findTriangle (mesh, 0.98, 0.98, bary) < 0, "outside point rejected");

    // Solving WITH the reference tension must match lambda + (T-T0) g of the
    // T0 = 0 solve reasonably well for moderate tension (same mesh).
    BoundarySpec bc;
    bc.segmentStart = { 0.0 };
    bc.segmentBc = { BoundaryCondition::SimplySupported };

    ModalOptions o0;
    o0.numModes = 3;
    o0.tension = 0.0;
    const auto r0 = computePlateModes (mesh, bc, o0);

    ModalOptions oT = o0;
    oT.tension = 50.0;
    const auto rT = computePlateModes (mesh, bc, oT);

    if (r0.numModes() >= 1 && rT.numModes() >= 1)
    {
        const double predicted = r0.lambda[0] + 50.0 * r0.tensionG[0];
        checkClose (rT.lambda[0], predicted, 0.05,
                    "first-order tension law vs exact solve (T = 50)");
    }
    else
        check (false, "tension solves returned modes");
}

int main()
{
    std::printf ("fxme::acoustics plate FEM validation\n");
    testSimplySupportedSquare();
    testClampedCircle();
    testMixedBoundary();
    testTensionAndMesh();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
