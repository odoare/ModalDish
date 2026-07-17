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
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "../ParamIDs.h"

namespace fem
{

struct ModalModel
{
    /** One statistical tail mode above the FEM set: its lambda/tensionG
        live in `modes` like any other mode, but its shape is a Berry
        random plane wave  amp * cos(kx*x + ky*y + phase)  with wavenumber
        |k| = lambda^(1/4) — the correct high-frequency asymptotics of
        chaotic-billiard eigenfunctions, mass-normalised via
        amp = sqrt(2/area). See PluginProcessor::appendStatisticalTail. */
    struct TailWave
    {
        float kx = 0.0f, ky = 0.0f, phase = 0.0f, amp = 0.0f;
    };

    std::shared_ptr<const fxme::acoustics::FemMesh> mesh;
    fxme::acoustics::ModalResult modes;          // FEM modes + appended tail
    std::vector<TailWave> tailWaves;             // one per tail mode
    std::uint64_t generation = 0;   // publication order tag (see PluginProcessor)

    /** All modes, FEM + statistical tail. */
    int numModes() const noexcept    { return modes.numModes(); }
    /** FEM-computed modes only — the ones with a displayable mesh shape. */
    int numFemModes() const noexcept { return (int) modes.shapes.size(); }

    /** Evaluates every mode shape at plate point (x, y) into out[0..count-1]
        (zeros when outside the plate): mesh interpolation for the FEM modes,
        random plane waves for the tail. One point location for the whole
        bank. Bounded, allocation-free: safe on the audio thread. Returns the
        number of values written (= min (numModes(), maxOut)). */
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
            return count;   // outside the plate: nothing gets excited

        const auto& t = mesh->triangles[(size_t) tri];
        const int nFem = std::min (count, numFemModes());
        for (int k = 0; k < nFem; ++k)
        {
            const auto& shape = modes.shapes[(size_t) k];
            out[k] = (float) (bary[0] * shape[(size_t) t[0]]
                            + bary[1] * shape[(size_t) t[1]]
                            + bary[2] * shape[(size_t) t[2]]);
        }
        for (int k = nFem; k < count; ++k)
        {
            const auto& w = tailWaves[(size_t) (k - nFem)];
            out[k] = w.amp * std::cos (w.kx * (float) x + w.ky * (float) y + w.phase);
        }
        return count;
    }
};

} // namespace fem
