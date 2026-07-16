/*
  ------------------------------------------------------------------------------
    ModalModel.h

    The immutable result of one FEM computation, shared between the message
    thread (display) and the audio thread (modal synthesis): the mesh, the
    modal data (eigenvalues, tension coefficients, mass-normalised shapes)
    and a point-evaluation helper.

    Instances are built on a background thread, published to the audio thread
    through an atomic pointer swap (see PluginProcessor) and never mutated
    afterwards, so no locking is needed anywhere. The mesh is held through a
    shared_ptr so the editor's FemViewComponent can keep displaying it
    independently of the model's own lifetime; the audio thread only ever
    dereferences it through the raw model pointer (no ref-count traffic).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <FxmeTools/acoustics/FemMesh.h>
#include <FxmeTools/acoustics/PlateModes.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "../ParamIDs.h"

namespace fem
{

struct ModalModel
{
    std::shared_ptr<const fxme::acoustics::FemMesh> mesh;
    fxme::acoustics::ModalResult modes;
    std::uint64_t generation = 0;   // publication order tag (see PluginProcessor)

    int numModes() const noexcept { return modes.numModes(); }

    /** Evaluates every mode shape at plate point (x, y) into out[0..count-1]
        (zeros when outside the plate). One point location for the whole bank.
        Bounded, allocation-free: safe on the audio thread. Returns the number
        of values written (= min (numModes(), maxOut)). */
    int evalShapes (double x, double y, float* out, int maxOut) const noexcept
    {
        const int count = std::min (numModes(), maxOut);
        for (int k = 0; k < count; ++k)
            out[k] = 0.0f;
        if (mesh == nullptr)
            return count;

        double bary[3];
        const int tri = fxme::acoustics::findTriangle (*mesh, x, y, bary);
        if (tri < 0)
            return count;

        const auto& t = mesh->triangles[(size_t) tri];
        for (int k = 0; k < count; ++k)
        {
            const auto& shape = modes.shapes[(size_t) k];
            out[k] = (float) (bary[0] * shape[(size_t) t[0]]
                            + bary[1] * shape[(size_t) t[1]]
                            + bary[2] * shape[(size_t) t[2]]);
        }
        return count;
    }
};

} // namespace fem
