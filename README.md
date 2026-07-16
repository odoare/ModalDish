# FemPlate

FX-Mechanics plugin: physical model of a **plate / membrane of arbitrary
shape**, simulated by finite elements and played as an instrument (click the
plate) or used as an effect (feed it audio).

Draw a shape (freehand spline, ellipse or rectangle), split its border into
segments and give each one a boundary condition (free / simple support /
clamp / sliding support), build a finite-element grid, compute the eigenmodes
in the background — then hit the plate anywhere with the mouse, or send any
signal through it. The synthesis is a resonant filter bank (one band-pass per
mode, the MechanOdd approach) driven by the FEM modal data.

The original design brief lives in [doc/starting_spec.md](doc/starting_spec.md).

## Physics

Scaled Kirchhoff plate with tension (flexural rigidity = 1, density = 1):

```
d²w/dt² + c_v dw/dt + c_m Δ²(dw/dt) + Δ²w − T Δw = f
```

discretised with Morley triangles by the reusable FEM library living in the
FxmeTools submodule (`lib/FxmeTools/FxmeTools/acoustics/`, JUCE-free core,
documented in its own README). Eigenvalues converge at O(h²) — validated
against analytic plates in `Tests/FemTests.cpp`.

* **Base Freq** maps the first eigenfrequency to f1; all other modes scale
  accordingly.
* **Tension** T retunes the bank at audio rate through the first-order law
  `ωₖ(T)² = λₖ + (T − T₀)gₖ` (exact at the tension the modes were computed
  at; press *Compute* again for exactness at a very different tension).
* **Viscous / Material** are the damping ratio of mode 1; viscous damping
  (`dw/dt`) decays relatively slower for high modes (ζ ∝ 1/ν), material
  damping (`Δ² dw/dt`) faster (ζ ∝ ν).
* **Hammer** is the duration of the half-sine shock of a mouse hit.
* **Out X/Y** is the pickup point (also right-click/ctrl-drag on the plate).
* **In** injects the external audio input at the last hit point — the
  effect mode.

## GUI workflow

1. **GEOMETRY** — pick a tool: *Draw shape* (freehand, smoothed with a
   closed spline), *Ellipse* / *Rectangle* (from the *Aspect* knob),
   *Rotate* (drag), *Edit boundary*. In boundary mode, drag the border
   points (*Points* knob sets how many) and click a segment to cycle its
   condition (colour-coded, see legend).
2. **Grid** builds the mesh (*Grid* knob = element density), **Compute**
   runs the modal analysis in the background (progress bar).
3. **Plate view** — click to hit the plate; the *Mode* knob displays any
   single eigenmode as filled contours (its frequency shows in the status
   line).

## Building

JUCE ≥ 7 is expected as a sibling directory (`../JUCE`); FxmeTools is a git
submodule:

```
git clone --recurse-submodules <this repo>
cmake -B Builds
cmake --build Builds -j
```

Console FEM validation tests (JUCE-free):

```
cmake -B Builds -DFEMPLATE_BUILD_TESTS=ON
cmake --build Builds -j --target FemTests
ctest --test-dir Builds
```

## Repo layout

| Path | Contents |
| --- | --- |
| `Source/PluginProcessor.*` | parameters, background compute, model publication, audio |
| `Source/Dsp/PlateSynth.*` | modal resonant filter bank (audio thread) |
| `Source/Dsp/ModalModel.h` | immutable mesh + modes shared with the audio thread |
| `Source/Components/ShapeCanvas.*` | shape drawing / boundary editing component |
| `Source/PluginEditor.*` | FX-Mechanics UI (TopBar, knobs, plate view) |
| `lib/FxmeTools/FxmeTools/acoustics/` | reusable FEM library (mesh, Morley solver, contour view) |
| `Tests/FemTests.cpp` | numerical validation vs analytic plates |

(c) 2026 Olivier Doaré — LGPL-3.0-or-later
