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
      5. Sparse and dense matrix storage agree, and by how much they differ in
         footprint.

    Exit code 0 when everything passes.

    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include <FxmeTools/acoustics/FemMesh.h>
#include <FxmeTools/acoustics/PlateModes.h>

#include <algorithm>
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

// ---------------------------------------------------------------------------
// 5. Sparse vs dense storage
// ---------------------------------------------------------------------------
// The two paths run the same assembly and the same eigensolver over the same
// element matrices. What differs is the storage, the renumbering the sparse
// path applies before assembling, and hence the order the factorisation
// eliminates in — so the two are not expected to agree bit for bit, only to
// converge to the same answer.
//
// How closely is worth being precise about, because "close enough" is where a
// real defect would hide. The eigensolver stops on a relative change in the
// eigenvalues: 1e-6 on the lower half of the requested range, 1e-4 on the
// upper half, the latter deliberately loose because that is where the finite
// element discretisation error is orders of magnitude larger anyway. So the
// converged modes must agree to near machine precision, while the top of the
// range is only entitled to its own stated tolerance. Measured, the split is
// exactly that: modes 0-9 of this problem agree to 1e-14 and the last few to
// 1e-5, tracking the stopping rule rather than the storage.
//
// Testing the two halves separately is what keeps this sharp. One loose
// tolerance over all the modes would still pass if the well-converged ones had
// quietly gone wrong.
static void testStorageEquivalence()
{
    std::printf ("\n== Sparse vs dense storage ==\n");

    const auto mesh = generateMesh (rectangle (1.0, 0.8), 1.0 / 22.0);

    BoundarySpec bc;
    bc.segmentStart = { 0.0 };
    bc.segmentBc = { BoundaryCondition::Clamped };

    ModalOptions base;
    base.numModes = 24;
    base.tension = 12.0;
    base.numThreads = 1;      // one thread: the comparison is of storage, and
                              // a fixed schedule keeps the run reproducible

    ModalOptions sparseOpt = base;
    sparseOpt.storage = MatrixStorage::sparse;
    ModalOptions denseOpt = base;
    denseOpt.storage = MatrixStorage::dense;

    const auto rs = computePlateModes (mesh, bc, sparseOpt);
    const auto rd = computePlateModes (mesh, bc, denseOpt);

    check (rs.valid() && rd.valid(), "both storage paths returned modes");
    if (! (rs.valid() && rd.valid()))
        return;

    check (rs.numModes() == rd.numModes(), "same number of modes survived");
    if (rs.numModes() != rd.numModes())
        return;

    // Index 0 = the tightly converged lower half, index 1 = the whole range.
    double worstLambda[2] = { 0.0, 0.0 };
    double worstG[2] = { 0.0, 0.0 };
    double worstShape[2] = { 0.0, 0.0 };
    const int converged = rs.numModes() / 2;

    for (int k = 0; k < rs.numModes(); ++k)
    {
        const double l0 = rd.lambda[(size_t) k], l1 = rs.lambda[(size_t) k];
        const double dl = std::abs (l1 - l0) / std::max (std::abs (l0), 1e-30);

        const double g0 = rd.tensionG[(size_t) k], g1 = rs.tensionG[(size_t) k];
        const double dg = std::abs (g1 - g0) / std::max (std::abs (g0), 1e-30);

        // Mode shapes are defined up to a sign; align on the largest component
        // before comparing, then measure relative to that component.
        const auto& a = rd.shapes[(size_t) k];
        const auto& b = rs.shapes[(size_t) k];
        double ds = 1.0;
        if (a.size() == b.size())
        {
            size_t big = 0;
            for (size_t v = 0; v < a.size(); ++v)
                if (std::abs (a[v]) > std::abs (a[big]))
                    big = v;
            const double peak = std::max ((double) std::abs (a[big]), 1e-30);
            const double sign = (a[big] * b[big] < 0.0f) ? -1.0 : 1.0;
            ds = 0.0;
            for (size_t v = 0; v < a.size(); ++v)
                ds = std::max (ds, std::abs (sign * b[v] - a[v]) / peak);
        }

        for (int half = (k < converged ? 0 : 1); half < 2; ++half)
        {
            worstLambda[half] = std::max (worstLambda[half], dl);
            worstG[half] = std::max (worstG[half], dg);
            worstShape[half] = std::max (worstShape[half], ds);
        }
    }

    std::printf ("  converged half (modes 0-%d): lambda %.2e, tensionG %.2e, shape %.2e\n",
                 converged - 1, worstLambda[0], worstG[0], worstShape[0]);
    std::printf ("  all %d modes:                lambda %.2e, tensionG %.2e, shape %.2e\n",
                 rs.numModes(), worstLambda[1], worstG[1], worstShape[1]);

    check (worstLambda[0] < 1e-10, "converged eigenvalues agree to machine precision");
    check (worstShape[0] < 1e-5, "converged mode shapes agree");
    // tensionG is x'Gx for an operator that is not the one being diagonalised,
    // so unlike the eigenvalue it is not stationary in x: its error is first
    // order in the eigenvector error, and it belongs with the shapes rather
    // than with the eigenvalues.
    check (worstG[0] < 1e-6, "converged tension coefficients agree");
    check (worstLambda[1] < 1e-4, "all eigenvalues agree within the solver tolerance");
    check (worstShape[1] < 1e-1, "all mode shapes agree within the solver tolerance");

    const double sparseMb = (double) rs.solverBytes / 1048576.0;
    const double denseMb  = (double) rd.solverBytes / 1048576.0;
    std::printf ("  footprint: sparse %.1f MB, dense %.1f MB (%.2fx)\n",
                 sparseMb, denseMb, denseMb / std::max (sparseMb, 1e-9));
    check (rs.solverBytes * 10 < rd.solverBytes, "sparse storage uses far less memory");
    check (rs.storageUsed == MatrixStorage::sparse
           && rd.storageUsed == MatrixStorage::dense, "result reports the storage used");
}

// ---------------------------------------------------------------------------
// 6. Boundary parameter vs polygon winding
// ---------------------------------------------------------------------------
// Boundary conditions are carried as arc-length fractions along the outline,
// so whatever attaches them has to measure that fraction the way generateMesh
// does. It reverses a clockwise polygon before parameterising it, which flips
// the direction *and* moves the origin — attach a condition at t = 0.25 on a
// clockwise outline and it lands at the far end of the plate. This pins the
// precondition down so it cannot be rediscovered the hard way.
static void testBoundaryParamWinding()
{
    std::printf ("\n== Boundary parameter follows a counter-clockwise outline ==\n");

    std::vector<Point2> ccw;
    for (int i = 0; i < 48; ++i)
    {
        const double a = 2.0 * M_PI * i / 48;
        ccw.push_back ({ 0.5 + 0.40 * std::cos (a), 0.5 + 0.30 * std::sin (a) });
    }
    check (polygonArea (ccw) > 0.0, "the reference outline is counter-clockwise");

    // Where the polygon itself puts a parameter, walking it as given.
    const auto polygonPointAt = [] (const std::vector<Point2>& o, double t)
    {
        const size_t n = o.size();
        std::vector<double> cum (n + 1, 0.0);
        for (size_t i = 0; i < n; ++i)
            cum[i + 1] = cum[i] + std::hypot (o[(i + 1) % n].x - o[i].x,
                                              o[(i + 1) % n].y - o[i].y);
        const double s = (t - std::floor (t)) * cum[n];
        size_t seg = 0;
        while (seg + 1 < n && cum[seg + 1] < s)
            ++seg;
        const double len = cum[seg + 1] - cum[seg];
        const double u = len > 0.0 ? (s - cum[seg]) / len : 0.0;
        const auto& p = o[seg];
        const auto& q = o[(seg + 1) % n];
        return Point2 { p.x + u * (q.x - p.x), p.y + u * (q.y - p.y) };
    };

    // ...and where the mesh puts it.
    const auto meshPointAt = [] (const FemMesh& m, double t)
    {
        double bx = 0.0, by = 0.0;
        int n = 0;
        for (int v = 0; v < m.numVertices(); ++v)
        {
            const double vt = m.vertexParam[(size_t) v];
            if (vt >= 0.0 && std::abs (vt - t) < 0.03)
            {
                bx += m.vertices[(size_t) v].x;
                by += m.vertices[(size_t) v].y;
                ++n;
            }
        }
        return Point2 { n > 0 ? bx / n : 0.0, n > 0 ? by / n : 0.0 };
    };

    const auto mesh = generateMesh (ccw, 1.0 / 14.0);
    double worst = 0.0;
    for (const double t : { 0.0, 0.25, 0.5, 0.75 })
    {
        const auto a = polygonPointAt (ccw, t);
        const auto b = meshPointAt (mesh, t);
        worst = std::max (worst, std::hypot (a.x - b.x, a.y - b.y));
    }
    std::printf ("  worst gap between outline and mesh parameter: %.3f\n", worst);
    // A boundary sample sits within about half an element of the outline, so
    // the tolerance is the element size rather than machine precision.
    check (worst < 1.0 / 14.0, "mesh parameter follows the outline as given");

    // The same shape wound the other way disagrees, which is the trap.
    auto cw = ccw;
    std::reverse (cw.begin(), cw.end());
    const auto cwMesh = generateMesh (cw, 1.0 / 14.0);
    double cwWorst = 0.0;
    for (const double t : { 0.25, 0.75 })
    {
        const auto a = polygonPointAt (cw, t);
        const auto b = meshPointAt (cwMesh, t);
        cwWorst = std::max (cwWorst, std::hypot (a.x - b.x, a.y - b.y));
    }
    std::printf ("  same shape wound clockwise: %.3f\n", cwWorst);
    check (cwWorst > 0.3, "a clockwise outline does NOT agree - callers must normalise");
}

int main()
{
    std::printf ("fxme::acoustics plate FEM validation\n");
    testSimplySupportedSquare();
    testClampedCircle();
    testMixedBoundary();
    testTensionAndMesh();
    testStorageEquivalence();
    testBoundaryParamWinding();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
