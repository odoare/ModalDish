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
* **Hammer** is the duration of the half-sine shock of a mouse hit, and
  **Force** its amplitude (past unity it drives the nonlinearities hard).
* **Out X/Y** is the pickup point (also right-click/ctrl-drag on the plate).
* **In** injects the external audio input at the last hit point — the
  effect mode.

### Geometric nonlinearity

Two efficient, audio-thread-only approximations of the von Kármán plate
nonlinearity (both 0 = exactly the linear model):

* **Nonlinear** — Berger dynamic tension: deflection stretches the
  mid-plane, modelled as a uniform amplitude-dependent tension. It is
  parameterised as a *relative* stiffening of mode 1,
  `γ = 8·nonlin·⟨out²⟩` (so the knob is calibrated in audible glide,
  independently of the plate), fed into the *same* first-order tension
  law used by the Tension knob (slewed, retuning only above ~2 cents,
  capped at γ = 4). The whole spectrum — fundamental included — glides
  up by `√(1+γ)` on a hard hit and relaxes as the plate rings out: the
  hardening gong / tom pitch bend. (The static Tension knob, by
  contrast, keeps mode 1 pinned at f1 and reshapes the ratios only.)
* **Cascade** — inter-mode energy transfer as a windowed 8-band ladder
  with transfer inertia: each band is pumped by a tanh-bounded cubic of
  the bands directly below it (Window, default 4), injected at the last
  hit point through a per-band attack/release gate whose attack grows
  with the band's height. Loud hits brighten the spectrum progressively
  and the pumping tails off smoothly; at low amplitude the effect
  vanishes as w³. The band graph is a strict DAG — no feedback loop — so
  the scheme is unconditionally stable with no gain restriction.
  **Deplete** adds the matching low-frequency loss: while a band pumps
  the ones above it, its own filter states receive an extra dissipation
  proportional to the transfer activity, so the lows audibly hand their
  energy to the shimmer instead of ringing on beneath it. All cascade
  internals live in the CASCADE panel, revealed by the **Advanced**
  button in the top bar (the window widens). The effective cascade
  amount is automatically rescaled by a material-damping compensation,
  `min(1, (2.3·10⁻⁵/ζₘ)^0.64)` — a power law fitted on listening
  limits — so the knob's maximum stays just below the saturation point
  at any damping. The voiced defaults are
  Amp 1.1 (capped 1.5 — louder gets unpleasant), Drive 16, Window 4,
  Attack 30 ms/band, Release 2 s, Overlap 0.1, Deplete 0.07, with
  Viscous ≈ 10⁻⁴ and Material ≈ 7·10⁻⁶ (Material's ζ ∝ ν law otherwise
  kills the top octave in milliseconds).
* **Statistical mode tail** — above the FEM-computed modes (≤ 128) the
  bank is filled to 256 with synthetic modes: Weyl-law spacing with
  jittered gaps, tension sensitivity g = √λ (the exact high-frequency
  asymptote), and Berry random-plane-wave shapes `√(2/A)·cos(κ·x+ψ)`
  with κ = λ^¼ — so hit position still matters and the tail obeys the
  tension/damping/cascade laws like any FEM mode. This gives the cascade
  a dense receiving continuum up to Nyquist without solving for hundreds
  of FEM modes; raise the **Modes** knob past the FEM count to engage
  it (the status line marks tail modes when displayed).

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

## Playing it from MIDI

A note-on tunes the plate so that **mode 1 lands on the note** (the rest of
the spectrum follows the ratios the geometry and boundary conditions give it)
and strikes it at the last point you clicked, with a hammer amplitude of
`velocity / 127 × Force`. Note-offs are ignored: a struck plate rings out on
its own, so the decay is the damping, not the key.

The **Glide** knob (next to *Freq*) is the portamento time between played
notes, 0 to 2 s. It is a time rather than a rate, so a glide takes the same
Glide setting whatever the interval. Two deliberate exceptions never glide:
the *Freq* knob, which sets the pitch outright, and the first note after
loading, which lands in tune instead of swooping up from wherever *Freq*
happened to sit.

Hosts differ on whether an audio effect can receive MIDI at all:

| Host | How |
| --- | --- |
| REAPER | Any track; MIDI on it reaches the plugin directly. |
| Ableton Live | **Cannot** route MIDI to an audio effect on an audio track. Put FemPlate on a MIDI track *after* an instrument. |
| Bitwig / Studio One / Cubase | Route a MIDI track's output to the plugin (note-input assignment). |
| Logic | Use the AU: it appears under *MIDI-controlled Effects*, not Audio FX. |

If nothing seems to arrive, watch the plate view: a note that lands flashes
the same ring marker a mouse click does, at the last point you clicked.

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
