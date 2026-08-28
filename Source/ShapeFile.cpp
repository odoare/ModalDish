/*
  ------------------------------------------------------------------------------
    ShapeFile.cpp — see ShapeFile.h for the format.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "ShapeFile.h"

#include <algorithm>
#include <cmath>

namespace fem::shapefile
{

namespace
{
    // Shapes live in a margin box inside the unit square, the same one
    // ShapeCanvas fits its own shapes into (its `margin` is 0.08). A file
    // using the full [-1, 1] therefore lands exactly where a drawn shape of
    // the same extent would, and one using less stays proportionally smaller
    // rather than being stretched to fill.
    constexpr double canvasMargin = 0.08;
    constexpr double halfSpan = 0.5 * (1.0 - 2.0 * canvasMargin);   // 0.42

    // A little slack so that a hand-written 1.0 survives decimal rounding.
    constexpr double rangeTolerance = 1.0e-6;

    Result fail (const juce::String& message)
    {
        Result r;
        r.ok = false;
        r.error = message;
        return r;
    }

    /** BoundaryCondition value for a name, or -1. Case and separators are
        ignored, so "SimplySupported", "simply supported" and "simple" all
        reach the same condition. */
    int conditionFromName (juce::String s)
    {
        s = s.toLowerCase().removeCharacters (" _-");

        if (s == "clamp" || s == "clamped")                 return 0;
        if (s == "support" || s == "simplysupported"
            || s == "simple" || s == "simplysupport"
            || s == "supported")                            return 1;
        if (s == "slide" || s == "sliding"
            || s == "slidingsupport")                       return 2;
        if (s == "free")                                    return 3;
        return -1;
    }

    /** A JSON value that must be a number. JUCE's var reports ints and
        doubles separately, so both are accepted; anything else is not. */
    bool asNumber (const juce::var& v, double& out)
    {
        if (v.isDouble() || v.isInt() || v.isInt64())
        {
            out = (double) v;
            return true;
        }
        return false;
    }
}

juce::StringArray conditionNames()
{
    return { "clamp", "support", "slide", "free" };
}

Result parse (const juce::String& json)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (json, root);
    if (parsed.failed())
        return fail ("Not valid JSON: " + parsed.getErrorMessage());

    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return fail ("The file must contain a JSON object at the top level.");

    Shape shape;
    shape.name = root.getProperty ("name", {}).toString();

    // ---- points ----------------------------------------------------------
    const auto ptsVar = root.getProperty ("points", {});
    const auto* pts = ptsVar.getArray();
    if (pts == nullptr)
        return fail ("Missing \"points\": expected an array of [x, y] pairs.");
    if (pts->size() < 3)
        return fail ("\"points\" needs at least 3 entries to enclose an area, found "
                     + juce::String (pts->size()) + ".");

    shape.outline.reserve ((size_t) pts->size());
    for (int i = 0; i < pts->size(); ++i)
    {
        const auto* pair = (*pts)[i].getArray();
        double x = 0.0, y = 0.0;
        if (pair == nullptr || pair->size() != 2
            || ! asNumber ((*pair)[0], x) || ! asNumber ((*pair)[1], y))
            return fail ("Point " + juce::String (i) + " is not a pair of numbers.");

        if (std::abs (x) > 1.0 + rangeTolerance || std::abs (y) > 1.0 + rangeTolerance)
            return fail ("Point " + juce::String (i) + " is outside [-1, 1]: ("
                         + juce::String (x, 4) + ", " + juce::String (y, 4)
                         + "). Coordinates are normalised, not millimetres.");

        // y is already up in plate coordinates (ShapeCanvas::plateToScreen
        // does the flip), so the file's convention needs no inversion here.
        shape.outline.push_back ({ 0.5 + halfSpan * x, 0.5 + halfSpan * y });
    }

    // ---- arc-length parameterisation -------------------------------------
    const size_t n = shape.outline.size();
    std::vector<double> cum (n, 0.0);
    double perimeter = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& a = shape.outline[i];
        const auto& b = shape.outline[(i + 1) % n];
        cum[i] = perimeter;
        perimeter += std::hypot (b.x - a.x, b.y - a.y);
    }
    if (perimeter <= 1.0e-9)
        return fail ("The points are all at the same place, so the outline has no length.");

    // ---- boundary --------------------------------------------------------
    const auto bcVar = root.getProperty ("boundary", {});
    if (auto* bcObj = bcVar.getDynamicObject())
    {
        // Sorted by arc parameter, because BoundarySpec requires ascending
        // starts and a JSON object carries no order of its own.
        std::vector<std::pair<double, int>> entries;

        for (const auto& prop : bcObj->getProperties())
        {
            const auto key = prop.name.toString();
            if (! key.containsOnly ("0123456789") || key.isEmpty())
                return fail ("Boundary key \"" + key + "\" is not a point index.");

            const int index = key.getIntValue();
            if (index < 0 || index >= (int) n)
                return fail ("Boundary key \"" + key + "\" is out of range: there are "
                             + juce::String ((int) n) + " points (0 to "
                             + juce::String ((int) n - 1) + ").");

            int bc = -1;
            if (prop.value.isString())
                bc = conditionFromName (prop.value.toString());
            else if (prop.value.isInt() || prop.value.isInt64())
                bc = (int) prop.value;

            if (bc < 0 || bc > 3)
                return fail ("Point " + juce::String (index) + " has unknown condition \""
                             + prop.value.toString() + "\". Use one of: "
                             + conditionNames().joinIntoString (", ") + ".");

            entries.push_back ({ cum[(size_t) index] / perimeter, bc });
        }

        std::sort (entries.begin(), entries.end(),
                   [] (const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& e : entries)
        {
            shape.segStarts.push_back (e.first);
            shape.segBcs.push_back (e.second);
        }
    }
    else if (! bcVar.isVoid())
    {
        return fail ("\"boundary\" must be an object mapping point index to condition.");
    }

    if (shape.segStarts.empty())
    {
        // No map at all: one segment round the whole edge, simply supported,
        // which is where the canvas starts a new shape too.
        shape.segStarts.push_back (0.0);
        shape.segBcs.push_back (1);
    }

    // ---- mesh density ----------------------------------------------------
    double density = 0.0;
    if (asNumber (root.getProperty ("meshDensity", {}), density))
    {
        const int d = (int) std::lround (density);
        if (d < 8 || d > 48)
            return fail ("\"meshDensity\" is " + juce::String (d)
                         + "; the Grid control accepts 8 to 48.");
        shape.meshDensity = d;
    }

    Result r;
    r.ok = true;
    r.shape = std::move (shape);
    return r;
}

Result load (const juce::File& file)
{
    if (! file.existsAsFile())
        return fail ("No such file: " + file.getFullPathName());

    return parse (file.loadFileAsString());
}

} // namespace fem::shapefile
