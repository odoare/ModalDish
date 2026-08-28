/*
  ------------------------------------------------------------------------------
    ShapeCanvas.h

    Independent, reusable component for drawing the plate shape and editing
    its boundary:

      * Draw tool      — freehand outline, smoothed on release with a closed
                         Catmull-Rom spline and refitted into the canvas.
      * Ellipse /      — standard shapes generated from the current aspect
        Rectangle        ratio (width / height).
      * Rotate tool    — click-drag rotates the shape about its centre.
      * Boundary tool  — Ns draggable points on the border split it into Ns
                         segments; clicking a segment cycles its boundary
                         condition, cycling stiffest-first (Clamped /
                         SimplySupported / Sliding / Free, colour-coded).

    The shape lives in plate coordinates in the unit square (y up); segment
    positions are arc-length parameters in [0,1) along the outline — the
    same conventions as fxme::acoustics::FemMesh / BoundarySpec, so the data
    can be handed to generateMesh / computePlateModes directly.

    The component owns no processor state: hosts read outline()/segment
    accessors and are notified through onShapeChanged / onBoundaryChanged.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <FxmeTools/acoustics/FemMesh.h>

namespace fem
{

class ShapeCanvas : public juce::Component
{
public:
    using Point2 = fxme::acoustics::Point2;

    enum class Tool { Draw = 0, Ellipse, Rectangle, Rotate, Boundary };

    ShapeCanvas();

    /** Ellipse / Rectangle regenerate the shape immediately. */
    void setTool (Tool t);
    Tool getTool() const noexcept                       { return tool; }

    /** Aspect ratio (width / height) of the standard shapes; regenerates the
        current shape when it is an ellipse or a rectangle. */
    void setAspect (double a);

    /** Number of border points Ns (= number of segments). Segment positions
        are redistributed uniformly; each new segment inherits the condition
        found at its midpoint. */
    void setNumBorderPoints (int ns);
    int numBorderPoints() const noexcept                { return (int) segStarts.size(); }

    void setShape (std::vector<Point2> newOutline,
                   std::vector<double> newSegStarts,
                   std::vector<int> newSegBcs);

    const std::vector<Point2>& outline() const noexcept  { return outlinePts; }
    const std::vector<double>& segmentStarts() const noexcept { return segStarts; }
    const std::vector<int>& segmentBcs() const noexcept  { return segBcs; }

    /** Colour code for boundary condition `bc` (same values as
        fxme::acoustics::BoundaryCondition), shared with legends. */
    std::function<juce::Colour (int)> bcColour;

    /** Short name of a boundary condition, for the legend the canvas draws in
        its own bottom-right corner. Supplied by the host for the same reason
        bcColour is: the canvas draws the key, the application owns the words. */
    std::function<juce::String (int)> bcName;

    std::function<void()> onShapeChanged;     // outline geometry changed
    std::function<void()> onBoundaryChanged;  // segment points / conditions changed

    //==========================================================================
    /** The mesh to draw faintly behind the outline, so that the grid being
        designed is visible while it is being designed. Drawn with this
        component's own coordinate mapping rather than FemViewComponent's,
        which fits the mesh bounding box and so would not line up with the
        outline handles. */
    void setMesh (std::shared_ptr<const fxme::acoustics::FemMesh> m)
    {
        mesh = std::move (m);
        repaint();
    }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Point<float> plateToScreen (const Point2& p) const;
    Point2 screenToPlate (juce::Point<float> p) const;

    void makeStandardShape (bool rectangle);
    void resetSegmentsUniform (int ns, bool atCorners);
    void finishFreehand();
    void rebuildArcTable();
    Point2 arcPoint (double t) const;
    double nearestParam (const Point2& p) const;
    int hitTestHandle (juce::Point<float> screenPos) const;
    int segmentOfParam (double t) const;
    void normalizeSegments();   // restore ascending order after a wrap-around drag
    void notifyShape()    { if (onShapeChanged) onShapeChanged(); repaint(); }
    void notifyBoundary() { if (onBoundaryChanged) onBoundaryChanged(); repaint(); }

    Tool tool = Tool::Boundary;
    double aspect = 1.2;

    std::vector<Point2> outlinePts;      // closed outline, unit square, y up
    std::vector<double> segStarts;       // sorted arc params in [0,1)
    std::vector<int> segBcs;             // one BoundaryCondition value per segment

    std::vector<double> arcCum;          // cumulative outline length (rebuilt lazily)
    double perimeter = 0.0;

    // Interaction state.
    std::shared_ptr<const fxme::acoustics::FemMesh> mesh;   // drawn, never edited
    std::vector<juce::Point<float>> rawStroke;   // freehand, screen coords
    int draggedHandle = -1;
    bool rotating = false;
    double rotateStartAngle = 0.0;
    std::vector<Point2> rotateBase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeCanvas)
};

} // namespace fem
