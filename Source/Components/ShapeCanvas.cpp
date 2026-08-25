/*
  ------------------------------------------------------------------------------
    ShapeCanvas.cpp — see ShapeCanvas.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "ShapeCanvas.h"

#include <algorithm>

namespace fem
{

namespace
{
    constexpr float handleRadius = 7.0f;
    constexpr double margin = 0.08;          // shape fitting box inside [0,1]
    constexpr int splineResolution = 128;    // freehand outline points

    juce::Colour defaultBcColour (int bc)
    {
        static const juce::Colour c[4] = { juce::Colour (0xff8a93a6), juce::Colour (0xff4cc9f0),
                                           juce::Colour (0xffe0784a), juce::Colour (0xff9ac93c) };
        return c[juce::jlimit (0, 3, bc)];
    }
}

ShapeCanvas::ShapeCanvas()
{
    bcColour = defaultBcColour;
    makeStandardShape (false);   // sensible default until the host sets a shape
}

//==============================================================================
juce::Point<float> ShapeCanvas::plateToScreen (const Point2& p) const
{
    const float s = (float) juce::jmin (getWidth(), getHeight());
    const float ox = 0.5f * ((float) getWidth() - s);
    const float oy = 0.5f * ((float) getHeight() - s);
    return { ox + s * (float) p.x, oy + s * (1.0f - (float) p.y) };
}

ShapeCanvas::Point2 ShapeCanvas::screenToPlate (juce::Point<float> p) const
{
    const float s = (float) juce::jmin (getWidth(), getHeight());
    const float ox = 0.5f * ((float) getWidth() - s);
    const float oy = 0.5f * ((float) getHeight() - s);
    if (s <= 0.0f)
        return { 0.5, 0.5 };
    return { (double) ((p.x - ox) / s), (double) (1.0f - (p.y - oy) / s) };
}

//==============================================================================
void ShapeCanvas::setTool (Tool t)
{
    tool = t;
    rawStroke.clear();
    if (t == Tool::Ellipse || t == Tool::Rectangle)
    {
        makeStandardShape (t == Tool::Rectangle);
        notifyShape();
        notifyBoundary();
    }
    repaint();
}

void ShapeCanvas::setAspect (double a)
{
    aspect = juce::jlimit (0.2, 5.0, a);
    if (tool == Tool::Ellipse || tool == Tool::Rectangle)
    {
        makeStandardShape (tool == Tool::Rectangle);
        notifyShape();
        notifyBoundary();
    }
}

void ShapeCanvas::makeStandardShape (bool rectangle)
{
    const double maxDim = 1.0 - 2.0 * margin;
    const double w = aspect >= 1.0 ? maxDim : maxDim * aspect;
    const double h = aspect >= 1.0 ? maxDim / aspect : maxDim;

    outlinePts.clear();
    if (rectangle)
    {
        outlinePts.push_back ({ 0.5 - 0.5 * w, 0.5 - 0.5 * h });
        outlinePts.push_back ({ 0.5 + 0.5 * w, 0.5 - 0.5 * h });
        outlinePts.push_back ({ 0.5 + 0.5 * w, 0.5 + 0.5 * h });
        outlinePts.push_back ({ 0.5 - 0.5 * w, 0.5 + 0.5 * h });
    }
    else
    {
        constexpr int npts = 96;
        for (int i = 0; i < npts; ++i)
        {
            const double a = juce::MathConstants<double>::twoPi * i / npts;
            outlinePts.push_back ({ 0.5 + 0.5 * w * std::cos (a),
                                    0.5 + 0.5 * h * std::sin (a) });
        }
    }
    rebuildArcTable();
    resetSegmentsUniform (juce::jmax (1, (int) segStarts.size() > 0 ? (int) segStarts.size() : 4),
                          rectangle);
}

void ShapeCanvas::resetSegmentsUniform (int ns, bool atCorners)
{
    ns = juce::jlimit (1, 16, ns);

    // Keep the old conditions where possible: sample the previous spec at the
    // midpoint of each new segment.
    const auto oldStarts = segStarts;
    const auto oldBcs = segBcs;
    auto oldBcAt = [&] (double t) -> int
    {
        if (oldStarts.empty() || oldStarts.size() != oldBcs.size())
            return 1;   // SimplySupported default
        size_t seg = oldStarts.size() - 1;
        for (size_t i = 0; i < oldStarts.size(); ++i)
        {
            if (oldStarts[i] > t)
                break;
            seg = i;
        }
        if (t < oldStarts[0])
            seg = oldStarts.size() - 1;
        return oldBcs[seg];
    };

    segStarts.clear();
    segBcs.clear();

    if (atCorners && outlinePts.size() == 4 && perimeter > 0.0)
    {
        // Rectangle: one segment per side regardless of ns.
        for (size_t i = 0; i < 4; ++i)
            segStarts.push_back (arcCum[i] / perimeter);
    }
    else
    {
        for (int i = 0; i < ns; ++i)
            segStarts.push_back ((double) i / ns);
    }

    for (size_t i = 0; i < segStarts.size(); ++i)
    {
        const double next = i + 1 < segStarts.size() ? segStarts[i + 1] : segStarts[0] + 1.0;
        double mid = 0.5 * (segStarts[i] + next);
        if (mid >= 1.0)
            mid -= 1.0;
        segBcs.push_back (oldBcAt (mid));
    }
}

void ShapeCanvas::setNumBorderPoints (int ns)
{
    if (ns == (int) segStarts.size())
        return;
    resetSegmentsUniform (ns, false);
    notifyBoundary();
}

void ShapeCanvas::setShape (std::vector<Point2> newOutline,
                            std::vector<double> newSegStarts,
                            std::vector<int> newSegBcs)
{
    if (newOutline.size() < 3 || newSegStarts.size() != newSegBcs.size()
         || newSegStarts.empty())
        return;
    outlinePts = std::move (newOutline);
    segStarts = std::move (newSegStarts);
    segBcs = std::move (newSegBcs);
    rebuildArcTable();
    repaint();
}

//==============================================================================
void ShapeCanvas::rebuildArcTable()
{
    const size_t n = outlinePts.size();
    arcCum.assign (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        const auto& p = outlinePts[i];
        const auto& q = outlinePts[(i + 1) % n];
        arcCum[i + 1] = arcCum[i] + std::hypot (q.x - p.x, q.y - p.y);
    }
    perimeter = arcCum[n];
}

ShapeCanvas::Point2 ShapeCanvas::arcPoint (double t) const
{
    const size_t n = outlinePts.size();
    if (n == 0 || perimeter <= 0.0)
        return { 0.5, 0.5 };
    t -= std::floor (t);
    const double s = t * perimeter;
    size_t seg = 0;
    while (seg + 1 < n && arcCum[seg + 1] < s)
        ++seg;
    const double len = arcCum[seg + 1] - arcCum[seg];
    const double u = len > 0.0 ? (s - arcCum[seg]) / len : 0.0;
    const auto& p = outlinePts[seg];
    const auto& q = outlinePts[(seg + 1) % n];
    return { p.x + u * (q.x - p.x), p.y + u * (q.y - p.y) };
}

double ShapeCanvas::nearestParam (const Point2& pt) const
{
    const size_t n = outlinePts.size();
    if (n == 0 || perimeter <= 0.0)
        return 0.0;

    double best = 1.0e30, bestS = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& a = outlinePts[i];
        const auto& b = outlinePts[(i + 1) % n];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        double u = len2 > 0.0 ? ((pt.x - a.x) * dx + (pt.y - a.y) * dy) / len2 : 0.0;
        u = juce::jlimit (0.0, 1.0, u);
        const double ex = a.x + u * dx - pt.x, ey = a.y + u * dy - pt.y;
        const double d2 = ex * ex + ey * ey;
        if (d2 < best)
        {
            best = d2;
            bestS = arcCum[i] + u * std::sqrt (len2);
        }
    }
    double t = bestS / perimeter;
    if (t >= 1.0)
        t -= 1.0;
    return t;
}

int ShapeCanvas::hitTestHandle (juce::Point<float> screenPos) const
{
    for (size_t i = 0; i < segStarts.size(); ++i)
    {
        const auto hp = plateToScreen (arcPoint (segStarts[i]));
        if (hp.getDistanceFrom (screenPos) < handleRadius + 4.0f)
            return (int) i;
    }
    return -1;
}

void ShapeCanvas::normalizeSegments()
{
    // A handle dragged across the arc origin leaves the array cyclically
    // rotated (e.g. {0.98, 0.25, 0.5}); rotate starts and conditions together
    // so segStarts is ascending again, as BoundarySpec requires.
    const size_t ns = segStarts.size();
    if (ns < 2)
        return;
    size_t minIdx = 0;
    for (size_t i = 1; i < ns; ++i)
        if (segStarts[i] < segStarts[minIdx])
            minIdx = i;
    if (minIdx == 0)
        return;
    std::rotate (segStarts.begin(), segStarts.begin() + (long) minIdx, segStarts.end());
    std::rotate (segBcs.begin(), segBcs.begin() + (long) minIdx, segBcs.end());
}

int ShapeCanvas::segmentOfParam (double t) const
{
    if (segStarts.empty())
        return -1;
    size_t seg = segStarts.size() - 1;
    for (size_t i = 0; i < segStarts.size(); ++i)
    {
        if (segStarts[i] > t)
            break;
        seg = i;
    }
    if (t < segStarts[0])
        seg = segStarts.size() - 1;
    return (int) seg;
}

//==============================================================================
void ShapeCanvas::mouseDown (const juce::MouseEvent& e)
{
    switch (tool)
    {
        case Tool::Draw:
            rawStroke.clear();
            rawStroke.push_back (e.position);
            repaint();
            break;

        case Tool::Rotate:
        {
            const auto c = plateToScreen ({ 0.5, 0.5 });
            rotateStartAngle = std::atan2 (e.position.y - c.y, e.position.x - c.x);
            rotateBase = outlinePts;
            rotating = true;
            break;
        }

        case Tool::Boundary:
        {
            draggedHandle = hitTestHandle (e.position);
            if (draggedHandle < 0)
            {
                // Click on a segment (near the border): cycle its condition.
                const auto p = screenToPlate (e.position);
                const auto onBorder = arcPoint (nearestParam (p));
                const auto d = plateToScreen (onBorder).getDistanceFrom (e.position);
                if (d < 14.0f)
                {
                    const int seg = segmentOfParam (nearestParam (p));
                    if (seg >= 0)
                    {
                        segBcs[(size_t) seg] = (segBcs[(size_t) seg] + 1) % 4;
                        notifyBoundary();
                    }
                }
            }
            break;
        }

        case Tool::Ellipse:
        case Tool::Rectangle:
            break;
    }
}

void ShapeCanvas::mouseDrag (const juce::MouseEvent& e)
{
    switch (tool)
    {
        case Tool::Draw:
            if (rawStroke.empty() || rawStroke.back().getDistanceFrom (e.position) > 3.0f)
            {
                rawStroke.push_back (e.position);
                repaint();
            }
            break;

        case Tool::Rotate:
        {
            if (! rotating)
                break;
            const auto c = plateToScreen ({ 0.5, 0.5 });
            const double a = std::atan2 (e.position.y - c.y, e.position.x - c.x);
            // Screen y is flipped vs plate y: invert the angle delta.
            const double da = -(a - rotateStartAngle);
            const double ca = std::cos (da), sa = std::sin (da);
            outlinePts = rotateBase;
            for (auto& p : outlinePts)
            {
                const double x = p.x - 0.5, y = p.y - 0.5;
                p = { 0.5 + ca * x - sa * y, 0.5 + sa * x + ca * y };
            }
            rebuildArcTable();
            repaint();
            break;
        }

        case Tool::Boundary:
        {
            if (draggedHandle < 0)
                break;
            const size_t i = (size_t) draggedHandle;
            const size_t ns = segStarts.size();
            double t = nearestParam (screenToPlate (e.position));

            if (ns > 1)
            {
                // Unwrap the candidate near the current value, then clamp
                // between the cyclic neighbours (small gap).
                const double cur = segStarts[i];
                if (t - cur > 0.5)  t -= 1.0;
                if (cur - t > 0.5)  t += 1.0;

                double prev = segStarts[(i + ns - 1) % ns];
                double next = segStarts[(i + 1) % ns];
                if (prev >= cur) prev -= 1.0;
                if (next <= cur) next += 1.0;
                constexpr double gap = 0.02;
                t = juce::jlimit (prev + gap, next - gap, t);
                t -= std::floor (t);
            }

            segStarts[i] = t;
            repaint();
            break;
        }

        case Tool::Ellipse:
        case Tool::Rectangle:
            break;
    }
}

void ShapeCanvas::mouseUp (const juce::MouseEvent&)
{
    switch (tool)
    {
        case Tool::Draw:
            finishFreehand();
            break;

        case Tool::Rotate:
            if (rotating)
            {
                rotating = false;
                notifyShape();
                notifyBoundary();
            }
            break;

        case Tool::Boundary:
            if (draggedHandle >= 0)
            {
                draggedHandle = -1;
                normalizeSegments();
                notifyBoundary();
            }
            break;

        case Tool::Ellipse:
        case Tool::Rectangle:
            break;
    }
}

void ShapeCanvas::finishFreehand()
{
    if (rawStroke.size() < 8)
    {
        rawStroke.clear();
        repaint();
        return;
    }

    // Decimate the stroke, convert to plate coordinates.
    std::vector<Point2> ctrl;
    const float minDist = 8.0f;
    for (const auto& sp : rawStroke)
    {
        if (! ctrl.empty())
        {
            const auto last = plateToScreen (ctrl.back());
            if (last.getDistanceFrom (sp) < minDist)
                continue;
        }
        ctrl.push_back (screenToPlate (sp));
    }
    rawStroke.clear();
    if (ctrl.size() < 3)
    {
        repaint();
        return;
    }

    // Closed Catmull-Rom through the control points, resampled uniformly.
    std::vector<Point2> smooth;
    const size_t nc = ctrl.size();
    const int perSeg = juce::jmax (1, splineResolution / (int) nc);
    for (size_t i = 0; i < nc; ++i)
    {
        const auto& p0 = ctrl[(i + nc - 1) % nc];
        const auto& p1 = ctrl[i];
        const auto& p2 = ctrl[(i + 1) % nc];
        const auto& p3 = ctrl[(i + 2) % nc];
        for (int k = 0; k < perSeg; ++k)
        {
            const double u = (double) k / perSeg;
            const double u2 = u * u, u3 = u2 * u;
            const double b0 = -0.5 * u3 + u2 - 0.5 * u;
            const double b1 =  1.5 * u3 - 2.5 * u2 + 1.0;
            const double b2 = -1.5 * u3 + 2.0 * u2 + 0.5 * u;
            const double b3 =  0.5 * u3 - 0.5 * u2;
            smooth.push_back ({ b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                                b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y });
        }
    }

    // Refit into the margin box, preserving the drawn aspect ratio.
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (const auto& p : smooth)
    {
        minx = std::min (minx, p.x); maxx = std::max (maxx, p.x);
        miny = std::min (miny, p.y); maxy = std::max (maxy, p.y);
    }
    const double w = std::max (1.0e-9, maxx - minx);
    const double h = std::max (1.0e-9, maxy - miny);
    const double scale = (1.0 - 2.0 * margin) / std::max (w, h);
    for (auto& p : smooth)
        p = { 0.5 + scale * (p.x - 0.5 * (minx + maxx)),
              0.5 + scale * (p.y - 0.5 * (miny + maxy)) };

    outlinePts = std::move (smooth);
    rebuildArcTable();
    resetSegmentsUniform ((int) segStarts.size(), false);
    notifyShape();
    notifyBoundary();
}

//==============================================================================
void ShapeCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141a24));

    // The mesh, faintly, underneath everything else: in design mode the grid
    // is what is being designed, so it is always on screen rather than behind
    // a button press. Interior edges only — the boundary is drawn below in
    // its own per-segment colours.
    if (mesh != nullptr && ! mesh->empty())
    {
        g.setColour (juce::Colour (0xff4a5670).withAlpha (0.45f));
        for (int e = 0; e < mesh->numEdges(); ++e)
        {
            if (mesh->isBoundaryEdge (e))
                continue;
            const auto& ed = mesh->edges[(size_t) e];
            const auto a = plateToScreen (mesh->vertices[(size_t) ed.v0]);
            const auto b = plateToScreen (mesh->vertices[(size_t) ed.v1]);
            g.drawLine (a.x, a.y, b.x, b.y, 0.6f);
        }
    }

    const auto border = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff35415a));
    g.drawRoundedRectangle (border, 4.0f, 1.0f);

    // Freehand stroke in progress.
    if (! rawStroke.empty())
    {
        juce::Path p;
        p.startNewSubPath (rawStroke.front());
        for (const auto& sp : rawStroke)
            p.lineTo (sp);
        g.setColour (juce::Colour (0xff4cc9f0));
        g.strokePath (p, juce::PathStrokeType (1.6f));
        return;
    }

    if (outlinePts.size() < 3)
        return;

    // Filled shape.
    juce::Path shape;
    shape.startNewSubPath (plateToScreen (outlinePts[0]));
    for (size_t i = 1; i < outlinePts.size(); ++i)
        shape.lineTo (plateToScreen (outlinePts[i]));
    shape.closeSubPath();
    g.setColour (juce::Colour (0xff232d3d));
    g.fillPath (shape);

    // Boundary segments, colour-coded by condition.
    const size_t ns = segStarts.size();
    for (size_t i = 0; i < ns; ++i)
    {
        const double t0 = segStarts[i];
        double t1 = segStarts[(i + 1) % ns];
        if (t1 <= t0)
            t1 += 1.0;

        juce::Path seg;
        const int steps = juce::jmax (8, (int) (128.0 * (t1 - t0)));
        seg.startNewSubPath (plateToScreen (arcPoint (t0)));
        for (int k = 1; k <= steps; ++k)
            seg.lineTo (plateToScreen (arcPoint (t0 + (t1 - t0) * k / steps)));

        g.setColour (bcColour ((int) segBcs[i]));
        g.strokePath (seg, juce::PathStrokeType (3.0f));
    }

    // Border-point handles (boundary tool emphasises them).
    const bool editing = tool == Tool::Boundary;
    for (size_t i = 0; i < ns; ++i)
    {
        const auto hp = plateToScreen (arcPoint (segStarts[i]));
        g.setColour (juce::Colours::white.withAlpha (editing ? 0.95f : 0.5f));
        g.fillEllipse (hp.x - handleRadius * 0.6f, hp.y - handleRadius * 0.6f,
                       handleRadius * 1.2f, handleRadius * 1.2f);
        g.setColour (juce::Colour (0xff141a24));
        g.drawEllipse (hp.x - handleRadius * 0.6f, hp.y - handleRadius * 0.6f,
                       handleRadius * 1.2f, handleRadius * 1.2f, 1.2f);
    }

    // Tool hint.
    g.setColour (juce::Colour (0xff97a1b4));
    g.setFont (12.0f);
    const char* hint = "";
    switch (tool)
    {
        case Tool::Draw:      hint = "draw a closed shape with the mouse"; break;
        case Tool::Ellipse:   hint = "ellipse from the Aspect knob"; break;
        case Tool::Rectangle: hint = "rectangle from the Aspect knob"; break;
        case Tool::Rotate:    hint = "drag to rotate the shape"; break;
        case Tool::Boundary:  hint = "drag border points - click a segment to change its condition"; break;
    }
    g.drawText (hint, getLocalBounds().reduced (8).removeFromBottom (16),
                juce::Justification::centredLeft);
}

} // namespace fem
