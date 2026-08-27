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
* The plate is heard through pickups placed on it (right-click/ctrl-drag
  moves pickup 1). There are up to eight, matching the sources, each with its
  own position, level and pan, summing into a stereo pair; only pickup 1 is on
  by default, which is the single mono listening point this replaced. Pan is
  equal-power with the centre at unity, so a centred pickup at 0 dB is exactly
  what the mono output used to be.

* **Sources** (up to eight, labelled a–h) are where the plate gets hit and
  where the input goes in. Each has a position, its own **Hammer** time and
  **Force**, a **Spread** (the standard deviation per axis of a random offset
  applied to every hit, so no two strikes land in quite the same place), a
  **Note** it answers to, and a **Send** with an **In Bal** — its share of the
  plugin input and which side of the incoming stereo pair it takes. Only
  source *a* is on out of the box, centred at full send, which is the single
  injection point the input used to have. A MIDI note fires every enabled
  source mapped to it, with velocity scaling that source's Force from zero;
  a note no source claims falls back to a hit at the last struck point with
  the global Hammer and Force knobs.

  Both are linear mixes, so the audio loop costs the same whether one pickup
  and one source are on or eight and eight.

  On the plate, pickups are orchid circles labelled 1–8 and sources teal
  circles labelled a–h; a disabled one is a faint ring rather than gone, so it
  can still be found. The **TRANSDUCERS** panel repeats those sixteen markers
  as two rows of switches, pickups above and sources below, drawn as the very
  markers they control so the rows read as a picture of what is currently on.
  They are the same parameters as the **On** button in a marker's own panel and
  as alt-click on the plate, so all three always agree. The gestures:

  | Gesture | Effect |
  | --- | --- |
  | `1`–`8` / `a`–`h` with the mouse over the plate | put that pickup or source under the cursor, switching it on |
  | click a marker | open its panel — every parameter of that one point |
  | alt-click a marker | switch it off |
  | click anywhere else | hit the plate there, with the global Hammer and Force |
  | right-click / ctrl-drag | move pickup 1, as before |

  A hit flashes a white ring where it actually landed — so a source with
  Spread visibly scatters, and a note firing several sources flashes each —
  sized by how hard the plate was struck (velocity times that source's own
  Force, not the global one).

  A source's panel carries a **MIDI learn** button: arm it and the next note
  received is captured as that source's note (and swallowed, so learning never
  costs you a stray hit). Clicking it again, or closing the panel, gives up.
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
  internals live in the CASCADE panel, revealed by the **A >** button in
  the DYNAMICS title row (the window widens). The ladder is driven by the
  plate's *motion* over the whole plate (a band-mean modal velocity), and
  deliberately not by its audible output: the output models plate
  acceleration, so it carries a frequency tilt ω_k the motion does not,
  and a mode nodal at the pickup vanishes from it altogether — neither of
  which the plate itself does, and the ladder's cubic would cube both. Driven by the motion, the cascade is flat to a few dB across
  both damping knobs and identical wherever you put the output point; the
  Cascade knob therefore acts directly, with no damping correction on it.
  Moving the pickup still changes what you *hear* of the shimmer, which is
  what a pickup is for, and the hit point still shapes the injection —
  that is the one position the nonlinearity has a physical claim to.
  `Tests/CascadeMeasure.cpp` measures all of this.

  There are two controls, not the three there once were. **Cascade** (main
  panel) is the amount: it scales the whole ladder and it is the off switch.
  That off position reaches further than the shimmer, because the Overlap
  bandwidth floor below is scaled by the amount too — at Cascade 0 the target
  modes keep the plate's own damping, where an ungated floor would quietly
  widen them while nothing was being pumped. **Casc Drive** (Advanced) is
  the tanh knee: it changes what the carrier is *made of* rather than how
  much of it there is, which is why it is the one of the pair worth its own
  knob. The separate injection gain that used to sit between them is pinned
  at 10, a gain and an amount in series being one control between them, so
  Cascade at 1 is what Amp 10 was and 0.11 what its voiced 1.1 was.

  Two things about Drive. It is not gradual: at Cascade 1 and the shipped
  Force, everything from 0.1 to 4 is silent and 4 to 16 covers 76 dB. And
  where that window sits depends on how hard you play, because the tanh knee
  does — 8 matches the old voiced sound at Force 1, 6 at Force 6, 4 at
  Force 15.

  The voiced defaults are Cascade 0 (off), Drive 8, Window 4,
  Attack 30 ms/band, Release 2 s, Overlap 0.1, Deplete 0.07, with
  Viscous ≈ 10⁻⁴ and Material ≈ 7·10⁻⁶ (Material's ζ ∝ ν law otherwise
  kills the top octave in milliseconds).

  Every one of those durations means the same thing at any sample rate.
  That is worth stating because it was not always true: the depletion rate
  and the Berger stiffening slew were stored as per-sample constants, which
  is a duration that halves each time the sample rate doubles. At 96 kHz
  with Deplete up, the plate's tail came out 35 dB down. Durations now live
  in seconds and become per-step coefficients in `prepare()`, where the rate
  is known; `Tests/CascadeMeasure.cpp` renders the same patch at 48 and
  96 kHz and holds the two within half a dB.
* **Statistical mode tail** — above the FEM-computed modes (≤ 256) the
  bank is filled to 1024 with synthetic modes: Weyl-law spacing with
  jittered gaps, tension sensitivity g = √λ (the exact high-frequency
  asymptote), and Berry random-plane-wave shapes `√(2/A)·cos(κ·x+ψ)`
  with κ = λ^¼ — so hit position still matters and the tail obeys the
  tension/damping/cascade laws like any FEM mode. This gives the cascade
  a dense receiving continuum up to Nyquist without solving for hundreds
  of FEM modes; raise the **Modes** knob past the FEM count to engage
  it (the status line marks tail modes when displayed). The bank only
  processes modes below Nyquist, so at a high **Freq** the Modes knob
  costs nothing past the point where the spectrum runs out: at f₁ = 110 Hz
  425 of them are live, at f₁ = 440 Hz only 90.

  Because the tail continues at the plate's *own* Weyl spacing, bank size
  buys range and not density — the gaps between modes are what they are, and
  adding modes puts them above the ones already there rather than between
  them. What that range is for is the bottom of the **Freq** range, where the
  plate used to run out of spectrum: at f₁ = 55 Hz the bank stopped at
  13.4 kHz and now reaches Nyquist (+12.8 dB at 16 kHz, +36.8 dB at 20 kHz),
  and at f₁ = 20 Hz it reaches 9.5 kHz instead of 4.9 kHz. At f₁ = 110 Hz and
  above, Nyquist was already the limit and nothing changes at all.

  The tail is also where the shimmer's stereo image is made. A pickup
  collects `Σ φ_k(x)²` from the modes it hears, and every term of that is
  positive, so over hundreds of modes it converges to the same value
  wherever the pickup sits: two pickups differ by 2.1 dB over four modes
  and by 0.29 dB over all 512. The cascade spreads its shimmer across the
  whole tail at once, so all of it used to arrive dead centre while the
  low modes, being few, kept the ±15 dB per-mode differences that make
  the bottom of the spectrum wide. Decorrelation cannot fix that (the
  fine structure up there is already uncorrelated, at −0.2 to −0.35) and
  neither can a delay: the two channels' envelopes match at 0.96–0.98 for
  *every* lag within ±1 ms, so even the physically correct delay — the
  200–560 µs it takes a bending wave to reach one pickup rather than the
  other — could only shift the shimmer sideways. What does not converge
  is the *signed* sum `Σ φ_k(x)` over a band, a random walk whose terms
  cancel instead of accumulating, and whose ratio between two pickups
  stays broadly spread however many modes the band holds. FemPlate takes
  each band's pan from it, minus the mean so the tail scatters without
  drifting to one side, over bands about three to the octave (any
  narrower and the ear sums them back to the centre). Measured one
  auditory filter at a time, the interchannel level difference across the
  tail goes from 1.1–1.8 dB to 5.1–7.9 dB, the mono sum is unchanged to
  0.001 dB, and a single centred pickup is left exactly as it was. It
  applies only above the last FEM mode, where the shapes are synthetic
  anyway; below that the computed eigenvectors carry a real position
  dependence worth keeping. `Tests/IoTests.cpp` measures it.

  The **Grid** knob reaches 48, a limit inherited from the dense
  eigensolver that no longer applies. The matrices are sparse (a Morley
  element couples each degree of freedom to about eleven others whatever
  the mesh density) and the shifted operator is factorised inside the
  narrow envelope left by a reverse Cuthill–McKee renumbering, so nothing
  in the solve is quadratic in the mesh size any more: n = 6029 went from
  336 s and 1.0 GB to 5.5 s and 39 MB, and n = 15381 — which the dense
  solver could not run at all, needing 7.6 GB — solves 256 modes in 48 s
  and 196 MB. What a solve costs is now set by the **mode count**, not by
  the Grid setting. The status line shows the projected footprint once it
  passes 48 MB.

## GUI workflow

The plugin has two modes, and the controls on screen are the ones that
belong to the mode you are in.

1. **Modal design** — what the plate *is*. Pick a tool: *Draw shape*
   (freehand, smoothed with a closed spline), *Ellipse* / *Rectangle* (from
   the *Aspect* knob), *Rotate* (drag), *Edit boundary*. In boundary mode,
   drag the border points (*Points* knob sets how many) and click a segment
   to cycle its condition (colour-coded, see legend). *Grid* sets the element
   density and *Modes* the size of the filter bank; the mesh is rebuilt as
   you edit and drawn under the outline, so the grid is always in front of
   you. **Modes lives here rather than with the playing controls on purpose**
   — it reallocates and retunes the whole bank, and raising it while the
   plate is ringing can produce a very loud transient.
2. **Compute** runs the modal analysis in the background (progress bar) and
   drops you into Perform when it finishes. **Perform** on its own goes back
   to the last model computed, keeping the shape — so a shape can be worked
   on across several passes without the plate ever falling silent.
3. **Perform** — click to hit the plate. The view selector next to it
   chooses what the contours show: *Modes*, where the *Mode* knob picks a
   single eigenmode to display (its frequency shows in the status line), or
   *Displacement*, the live deflection of the sounding plate,
   `w = Σ q_k φ_k`, refreshed at 30 Hz, or *Velocity*, the same sum one
   power of frequency higher (`q̇_k = ω_k q_k`), which weights the high
   modes up. The colour scale is held across frames, so a ring visibly cools
   as it decays rather than being renormalised each frame; hard hits stay
   bright longer, and heavy damping fades out in a second.

   Velocity only looks different from displacement when high modes are
   actually ringing: with the default 3 ms hammer the two are
   indistinguishable, while at 0.3 ms the velocity field carries about twice
   the spatial detail. Neither shows the cascade, whose shimmer lives in the
   statistical tail, and tail modes have no mesh shape to draw.

## Playing it from MIDI

A note does two independent things: it **triggers** and it **tunes**. Which
of them a given note does is decided by the two channel controls, so the same
keyboard can do both or two controllers can split the job.

**Src Chan** (default *Omni*) is the channel that triggers. A note on it
fires every enabled source mapped to that note; a note no source claims falls
back to a hit at the last struck point with the global Hammer and Force.
Note-offs are ignored: a struck plate rings out on its own, so the decay is
the damping, not the key.

**Unmapped note hits** (in the same panel) says what a note on the sources
channel does when no source claims it: strike the last touched point, or
nothing. On is how the plugin has always behaved and is what makes it
playable from a keyboard before any source is mapped; switch it off once
every note is mapped, so a stray one stays silent rather than hitting
wherever the mouse last was.

**Freq Chan** (default *Off*) is the channel that tunes. A note on it moves
the plate so **mode 1 lands on the note**, the rest of the spectrum following
the ratios the geometry and boundary conditions give it. It ships Off, so
notes only trigger until you assign it; set both controls to the same channel
(or leave Src Chan on Omni) to play the plate as one instrument, or give them
different channels to retune it from a second controller without striking.

The **Glide** knob (next to *Freq*) is the portamento time between tuned
notes, 0.1 to 100 ms. It is a time rather than a rate, and the travel is in
log₂, so a glide takes the same Glide setting whatever the interval — half
way through an octave you are at the geometric mean, not the arithmetic one.
It is also a time in seconds rather than in samples, so it lasts as long at
96 kHz as at 48. Two deliberate exceptions never glide: the *Freq* knob,
which sets the pitch outright, and the first note after loading, which lands
in tune instead of swooping up from wherever *Freq* happened to sit.

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
| `lib/FxmeTools/core/FxmeTools/acoustics/` | reusable FEM library (mesh, Morley plate solver) |
| `lib/FxmeTools/core/FxmeTools/math/` | the linear algebra under it (sparse/dense storage, Cholesky, subspace eigensolver) |
| `lib/FxmeTools/FxmeTools/acoustics/` | the contour view component |
| `Tests/FemTests.cpp` | numerical validation vs analytic plates, and sparse vs dense storage |

(c) 2026 Olivier Doaré — LGPL-3.0-or-later
