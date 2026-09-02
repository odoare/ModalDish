/*
  ------------------------------------------------------------------------------
    ShapeCanvas.h

    Independent, reusable component for drawing the plate shape and editing
    its boundary:

      * Draw tool      — freehand outline, smoothed on release with a closed
                         Catmull-Rom spline and refitted into the canvas.
      * Rotate tool    — click-drag rotates the shape about its centre.
      * Polygon tool   — click to add a vertex, drag one to move it, alt-click
                         one to delete it. Fewer than three points is an open
                         chain being built; the third closes the shape.
      * Boundary tool  — Ns draggable points on the border split it into Ns
                         segments; clicking a segment cycles its boundary
                         condition, cycling stiffest-first (Clamped /
                         SimplySupported / Sliding / Free, colour-coded).

    An ellipse or a rectangle of the current aspect ratio is not one of those
    modes but a one-shot action (generateStandardShape): it replaces the
    outline and hands it straight back to whichever tool is selected, so a
    generated shape can be edited, drawn over or meshed without having to
    leave a mode first.

    The shape lives in plate coordinates in the unit square (y up); segment
    positions are arc-length parameters in [0,1) along the outline — the
    same conventions as fxme::acoustics::FemMesh / BoundarySpec, so the data
    can be handed to generateMesh / computePlateModes directly.

    The component owns no processor state: hosts read outline()/segment
    accessors and are notified through onShapeChanged / onBoundaryChanged.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
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

    // Appended, never reordered: the editor's tool combo maps its item ids
    // to these by position.
    enum class Tool { Draw = 0, Rotate, Boundary, Polygon };

    ShapeCanvas();

    void setTool (Tool t);
    Tool getTool() const noexcept                       { return tool; }

    /** Replaces the outline with an ellipse (or a rectangle) of the current
        aspect ratio, keeping the boundary conditions and leaving the selected
        tool alone, so the shape that appears is immediately editable. */
    void generateStandardShape (bool rectangle);

    /** Aspect ratio (width / height) of the standard shapes.

        Regenerates the shape while the outline is still exactly the one
        generateStandardShape produced, and leaves it alone from the first
        edit onwards: a drawn blob or a polygon with points moved in it has no
        aspect ratio to impose, and imposing one would discard the edit. */
    void setAspect (double a);
    double getAspect() const noexcept                   { return aspect; }

    /** Reinstates a tool and an aspect without touching the geometry.

        setAspect rebuilds the outline when the shape is still a generated
        one, which is exactly what turning the knob should do and exactly what
        reopening the editor must not do: the shape on screen is the one the
        user left. */
    void restoreToolAndAspect (Tool t, double a);

    /** True once if the outline has moved since the last call, then clears.

        A gesture still in progress deliberately does not call onShapeChanged:
        one remesh is several milliseconds at the top of the Grid range, and
        the mouse delivers events faster than there are frames to show them
        in. The drag sets this instead and the editor polls it from its 30 Hz
        timer, so the grid follows the shape at the frame rate rather than at
        the mouse rate, and a fast drag coalesces into one remesh per frame
        rather than a queue of them. */
    bool takeGeometryDirty() noexcept
    {
        const bool was = geometryDirty;
        geometryDirty = false;
        return was;
    }

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

    /** Where the boundary-condition key is drawn, in this component's own
        coordinates, so a host can put controls beside it instead of guessing
        where the corner ends. */
    juce::Rectangle<int> legendBounds() const;

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

    /** True while the outline is, point for point, the one the last call to
        generateStandardShape left behind. Comparing against a kept copy is
        the whole mechanism: anything that moves a point at all — a drag, a
        rotation, a redraw, a shape handed in by the host — fails the test,
        and that is exactly the set of cases where the aspect ratio has
        stopped describing what is on screen. */
    bool outlineIsGenerated() const noexcept;

    /** Reduce the outline to a handful of draggable vertices, so that a
        freehand or ellipse shape can be picked up by the Polygon tool. A
        shape that is already sparse enough is left exactly as it is. */
    void adoptOutlineAsPolygon();
    int hitTestVertex (juce::Point<float> screenPos) const;
    void resetSegmentsUniform (int ns, bool atCorners);
    void finishFreehand();
    void rebuildArcTable();
    Point2 arcPoint (double t) const;
    double nearestParam (const Point2& p) const;
    int hitTestHandle (juce::Point<float> screenPos) const;
    int segmentOfParam (double t) const;
    void normalizeSegments();   // restore ascending order after a wrap-around drag
    void notifyShape()
    {
        geometryDirty = false;   // this is the settling update
        if (onShapeChanged)
            onShapeChanged();
        repaint();
    }
    void notifyBoundary() { if (onBoundaryChanged) onBoundaryChanged(); repaint(); }

    Tool tool = Tool::Boundary;
    double aspect = 1.2;
    std::vector<Point2> standardOutline;   // outline as generated; see outlineIsGenerated
    bool standardIsRect = false;
    bool geometryDirty = false;   // outline moved mid-gesture; see takeGeometryDirty

    std::vector<Point2> outlinePts;      // closed outline, unit square, y up
    std::vector<double> segStarts;       // sorted arc params in [0,1)
    std::vector<int> segBcs;             // one BoundaryCondition value per segment

    std::vector<double> arcCum;          // cumulative outline length (rebuilt lazily)
    double perimeter = 0.0;

    // Interaction state.
    std::shared_ptr<const fxme::acoustics::FemMesh> mesh;   // drawn, never edited
    std::vector<juce::Point<float>> rawStroke;   // freehand, screen coords
    int draggedHandle = -1;
    int draggedVertex = -1;      // Polygon tool: outline point being moved
    bool rotating = false;
    double rotateStartAngle = 0.0;
    std::vector<Point2> rotateBase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeCanvas)
};

} // namespace fem
