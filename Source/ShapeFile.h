/*
  ------------------------------------------------------------------------------
    ShapeFile.h

    Reading a plate geometry from a JSON file.

    The format is a closed polygon plus a sparse map of boundary conditions:

        {
          "modaldish_shape": 1,
          "name": "Trapezoid gong",
          "meshDensity": 16,
          "points": [ [-0.8, -0.6], [0.8, -0.6], [0.6, 0.7], [-0.6, 0.7] ],
          "boundary": { "0": "clamp", "2": "free" }
        }

    Points are (x, y) in [-1, 1] with y *up* (the mathematical convention, not
    the screen one), linked in order and closed from the last back to the
    first. `boundary` maps a point index to the condition starting at that
    point; a point that is not listed inherits the condition of the previous
    listed one, wrapping around, which is exactly what BoundarySpec::bcAt
    already does — so the map converts to arc parameters and needs no further
    interpretation. An absent or empty `boundary` gives a simply supported
    edge all round, the same default the canvas starts from.

    JSON rather than XML or plain text because the boundary map really is a
    dictionary, juce::JSON parses it with no new dependency, and a file can
    gain a key later without invalidating the ones written today.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <FxmeTools/acoustics/FemMesh.h>

#include <vector>

namespace fem::shapefile
{

/** A geometry read from a file, already converted to plate coordinates and
    to the arc-parameter segment form the rest of the plugin speaks. */
struct Shape
{
    std::vector<fxme::acoustics::Point2> outline;  // plate coords in [0,1]^2, y up
    std::vector<double> segStarts;                 // ascending arc params in [0,1)
    std::vector<int> segBcs;                       // BoundaryCondition per segment
    int meshDensity = 0;                           // 0 when the file did not say
    juce::String name;
};

struct Result
{
    bool ok = false;
    juce::String error;    // names the offending element, for a dialog
    Shape shape;
};

/** Parse the text of a shape file. Never throws; a malformed file comes back
    as ok == false with `error` set. */
Result parse (const juce::String& json);

/** Read and parse a file. A file that cannot be opened is an error like any
    other. */
Result load (const juce::File& file);

/** The condition names a file may use, stiffest first. Index is the
    BoundaryCondition value; each also accepts the aliases documented in the
    .cpp (so "clamped" works as well as "clamp"). */
juce::StringArray conditionNames();

} // namespace fem::shapefile
