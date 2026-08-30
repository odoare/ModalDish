# ModalDish — FX-Mechanics compliance audit

Revisions:

- 2026-08-30, fourth pass: H1 and H2 applied, both verified bit-identical.
- 2026-08-30, third pass: R2 applied except the cascade column (declined, see
  below) and R3 applied. R1 declined for now.
- 2026-08-30, second pass: S1, S2, S3 and S4 applied (see the ticks below).
- 2026-08-30, first pass: original audit.

Audited 2026-08-30, at commit `d9dce24` (working tree dirty: the source
controller mappings, the preset and splash work, and the Place-points tool are
all uncommitted).

FxmeTools submodule `c75816d` on `main`, with one uncommitted change
(`core/FxmeTools/acoustics/PlateModes.h`, the boundary-condition enum reordered
by stiffness). The submodule is on the far side of the core/shell split
(`core/` is present) and `lib/FxmeTools/doc/api-changes.md` records this project
as done, last built against the split in 2026-08.

**The audit itself was static**: the findings below come from reading and
grepping the tree, with nothing built, so the compile-only failures noted under
Coverage cannot have been seen. The second pass then applied S1 to S4 on
request, which did change source (CMakeLists, README) and added a workflow; a
`cmake` configure was re-run to confirm the reordered CMakeLists, but still no
compile.

Ids are stable across re-runs: `S` silent bugs, `R` retrofit, `H` house style.

---

## Silent bugs

### S1 — the `if(APPLE)` block sits after `project()` — **safe to apply**

- [x] Done. The `if(APPLE)` block now sits above `project()` at
      [../CMakeLists.txt:16](../CMakeLists.txt#L16), with a comment saying why
      the order matters. Configure re-run clean.

CMake latches the toolchain and the architecture list when `project()` runs, so
`CMAKE_OSX_ARCHITECTURES` set afterwards is read too late. The build succeeds
and produces a single-architecture binary that reports itself as universal. This
is the trap the super-skill calls out by name, and it is present here exactly as
described.

### S2 — macOS deployment target is 11.0 — **decision**

- [x] Done. [../CMakeLists.txt:17](../CMakeLists.txt#L17) is now `10.13`, and
      the README states the supported range.

11.0 silently excludes Catalina, which is still in use. Marked decision rather
than safe because lowering it is a support commitment (it widens the set of
systems the plugin claims to run on, and nothing here has been tested on one).

### S3 — no CI workflow at all — **decision**

- [x] Done. [../.github/workflows/release.yml](../.github/workflows/release.yml):
      Linux, Windows and macOS-universal builds plus a tag-gated release job.
      The macOS job passes the architectures and deployment target as `-D` as
      well (cache beats file ordering), builds BinaryData serially first, and
      verifies both slices in all three bundles with a step that exits
      non-zero. Linux runs `ctest -E CascadeMeasure`.

There is no `.github` directory. Nothing is shipping broken today because
nothing is shipping, but the release workflow is the thing that catches S1: its
verification step must be able to fail, and a step that only prints `lipo -info`
has already shipped an arm64-only "universal" build in another project. Worth
having in place before the first tag rather than after.

### S4 — README has no Installing → macOS section — **safe to apply**

- [x] Done. [../README.md:17](../README.md#L17) gains an Installing section
      with the quarantine commands, the right-click-Open note for the `.pkg`,
      the supported macOS range and the Windows/Linux paths.

These builds are not notarised, so a downloaded bundle is quarantined and the
DAW skips it silently. The per-host MIDI routing section is already present
([../README.md:448](../README.md#L448)), so only the macOS half is missing.

---

## Retrofit

### R1 — `setTooltip` is called but no `TooltipWindow` exists — **declined for now**

- [ ] [../Source/PluginEditor.cpp](../Source/PluginEditor.cpp) sets a tooltip on
      the presets button; nothing in the project owns a `juce::TooltipWindow`,
      so that tooltip never appears.

Declined on 2026-08-30. Worth re-reading before release, because R3 added a
second invisible tooltip: `fxme::InfoButton` sets one on itself in its
constructor ("Help & shortcuts for this page"). Neither is load-bearing (both
controls are legible without them), so nothing is broken, but the count of
tooltips that exist and cannot be seen is now two rather than one.

### R2 — no control enablement for superseded parameters — **safe to apply**

Both parts polled from a timer rather than driven from `onChange`, so a
parameter moved by host automation greys its dependants too.

- [x] ~~Cascade column when `cascade` is 0.~~ **Declined deliberately**: the
      cascade tuning is worth setting up *before* the amount is raised, so
      greying it out would take away a working habit rather than clarify
      anything. This is the case where the generic rule does not fit the
      instrument, and it is left as it is on purpose.
- [x] `Glide` greys out while `Freq Chan` is Off, in the editor's existing
      30 Hz timer ([../Source/PluginEditor.cpp](../Source/PluginEditor.cpp)).
- [x] Per source, `X2`/`Y2` grey out while `Pos Ctl` is Off, `Ham Max` while
      `Ham Ctl` is Off, and `Frc Max` while `Frc Ctl` is Off.
- [x] Per source, `Curve` greys out unless at least one of the three controls
      reads Vel.

The source items are `PlatePointPanel::updateEnablement()`, called from the
10 Hz timer the source panel already ran for MIDI learn, and once in the
constructor so the panel opens in the right state rather than a tick later.
`addKnob` now returns the box it made, so the five superseded controls are held
as named pointers rather than found by index into `knobs` — an index that would
have gone stale the next time a row was added.

### R3 — no `fxme::InfoButton` — **decision**

- [x] Done. Added to the top bar's right controls beside the preset strip,
      carrying the plate gestures, the placement keys (including the capitals
      for a source's velocity endpoint), the Place-points tool and a short
      statement of how the min/max mappings read.

The plugin has a substantial shortcut vocabulary (`1`-`8` and `a`-`h` to place a
pickup or source, `A`-`H` for a source's velocity endpoint, alt-click to switch
a marker off, right-click or ctrl-drag to move pickup 1, and in the Place-points
tool click/drag/alt-click). All of it is documented only in the README, which is
not in front of the player.

---

## House style

### H1 — `PlateSynth::Resonator` duplicates `fxme::Biquad` — **decision**

- [x] Done. `Resonator` is gone; the bank is
      `fxme::Biquad filters[fem::maxModes]`, with `process` renamed to
      `processSample` at its one call site and an explicit
      `#include <FxmeTools/dsp/Biquad.h>` (it had been arriving through
      `JuceHeader.h`, which the JUCE-free test build does not provide).

      Verified rather than assumed: IoTests renders the same peaks to five
      decimals as before the swap (centred 0.01451, hard left 0.02052, the
      live-switch pair 0.02210, hammer 1 ms 0.03911 against 20 ms 0.00521), and
      CascadeMeasure's sample-rate table is unmoved. The depletion still scales
      `z1`/`z2` directly, which `fxme::Biquad` allows because it exposes them.

`fxme::Biquad` in `core/FxmeTools/dsp/Biquad.h` is the same struct, member for
member, with the same TDF-II body and the same public `z1`/`z2` (which the audio
loop pokes directly for the depletion decay, so the field access survives the
swap). The only difference is the method name: `process` against
`processSample`.

Marked decision, not because it is risky but because it is a filter bank in the
hot loop and the user may want to hear it before and after. The change is a
using-declaration plus a rename at three call sites.

### H2 — `tailRand` reimplements `fxme::detrand::avalanche` — **safe to apply**

- [x] Done. `tailRand` now calls `fxme::detrand::avalanche`, keeping only the
      counter's golden-ratio increment as a named constant.

      The order matters and is not obvious: `avalanche` folds that increment in
      itself, so it must read the counter *before* the advance. Written the
      other way round the stream shifts by one draw and every plate's tail
      changes. Proved equivalent rather than argued: a harness ran the old and
      new functions side by side for 200 000 draws on each of five seeds
      (including the two the plugin actually uses) and compared both the
      returned doubles and the final counter state, with zero divergence.

      `detrand::u01` is deliberately not used: it takes the top 24 bits into a
      float where this takes the low 32 into a double, so it would renumber
      every tail for no gain.

The local splitmix64 step uses the identical three constants
(`0x9e3779b97f4a7c15`, `0xbf58476d1ce4e5b9`, `0x94d049bb133111eb`) and the same
30/27/31 shifts as `avalanche()` in `core/FxmeTools/dsp/DeterministicRandom.h`.
Because the arithmetic is bit-identical, the statistical mode tail regenerates
exactly as before, so this one really is a no-op swap.

### H3 — the polygon simplifier belongs in `core/` — **decision**

- [ ] `simplifyChain` and `simplifyClosed`, in the anonymous namespace of
      [../Source/Components/ShapeCanvas.cpp:44](../Source/Components/ShapeCanvas.cpp#L44).

Douglas-Peucker over a closed polygon is generic computational geometry. It
names no `juce::` type (only `fxme::acoustics::Point2`, already a core type) and
depends on nothing in this plugin, so by the two-decision rule it belongs in
`core/`, alongside `FemMesh`. Being in an anonymous namespace inside a GUI
translation unit it is also unreachable from the JUCE-free test harness, which
is why its wrap-around chain had to be verified with a throwaway copy rather
than a suite.

Decision because it edits the shared library.

### H4 — `ShapeFile` sits at the plugin level — **decision, and probably decline**

- [ ] [../Source/ShapeFile.h](../Source/ShapeFile.h) and its `.cpp`.

Listed for completeness because a JSON geometry reader looks generic. It is not:
it hard-codes ModalDish's plate-coordinate convention (the 0.08 canvas margin,
so `[-1,1]` maps onto the same box the shape canvas fits into) and it uses
`juce::JSON`, which would put it in the module half rather than core. The
recommendation is to leave it where it is; recorded so a later audit does not
re-raise it.

### H5 — `api-changes.md` still calls this project FemPlate — **decision**

- [ ] `lib/FxmeTools/doc/api-changes.md`, the per-project table and the
      per-project notes.

The project was renamed in commit `365e3ce`. The submodule doc still lists
`FemPlate`, including a note about its `FemTests` target compiling two FxmeTools
sources by path, which no longer describes this project (that target now links
`FxmeCore` and names no paths). Editing it touches the shared submodule, hence
decision.

---

## Already correct

Checked and clean, so that silence here is coverage rather than omission.

**Project shape.** An FX-Mechanics project: `PLUGIN_MANUFACTURER_CODE FXME`,
CMake-based (no `.jucer`), one `juce_add_plugin` target, FxmeTools as a
submodule at the usual `lib/FxmeTools`. Not a FxmeJuceTools-era project and not
a nested FxmeFX consumer, so neither the two-level bump nor the migration
command applies.

**Plugin type.** `NEEDS_MIDI_INPUT TRUE` with
`AU_MAIN_TYPE kAudioUnitType_MusicEffect`. This is the pairing the checklist
exists to catch, and it is right: `kAudioUnitType_Effect` would have left the AU
unable to receive MIDI at all.

**Core/shell wiring.** Wired by hand rather than through the helper, but
correctly and completely:
`add_subdirectory(lib/FxmeTools/core FxmeCore)`, `juce_add_module(...)`,
`target_link_libraries(FxmeTools INTERFACE FxmeCore)`
([../CMakeLists.txt:26](../CMakeLists.txt#L26)). Including the helper would be
tidier, but the three lines do the same job and `fxmetools_attach()` is only
needed for WDL convolution, which this project does not use.

**The auxiliary-target trap — clean, and this is the one worth stating.** All
four non-plugin targets were enumerated by reading every CMakeLists rather than
grepping one spelling: `FemTests` and `CascadeMeasure` and `IoTests`
(`add_executable`) and `ShapeFileTests` (`juce_add_console_app`). Every one links
`FxmeCore` on its own line. The `FemPlate` entry in `api-changes.md` warns that
this project's test target compiled `FemMesh.cpp` and `PlateModes.cpp` by path;
that is fixed, the paths are gone, and `FxmeCore` supplies both.

**Renamed APIs.** No hits for `shapeChoices`, `syncRateChoices`,
`syncDivisionChoices`, `getScaleTypeNames`, `getSortedSet`, `getRawNotes`,
`getDegrees` or `MidiTools::`. The project uses none of the moved music-theory
or LFO surface.

**Combo boxes — the usual finding does not apply here.** The generic rule is
that every `juce::ComboBox` needs its own `setLookAndFeel`, because components
in this family set the look-and-feel per widget. This editor sets it on
*itself* ([../Source/PluginEditor.cpp:17](../Source/PluginEditor.cpp#L17)), and
`Component::getLookAndFeel()` walks up the parent chain, so `toolBox` and
`viewBox` inherit `FxmeLookAndFeel` as children of the editor. The point panel
is launched in a `CallOutBox` parented to the editor rather than the desktop,
with a comment saying why, so it inherits too. No finding.

**Accent colour.** `lnf.setAccentColour` is called once
([../Source/PluginEditor.cpp:23](../Source/PluginEditor.cpp#L23)), so drop-down
menus are tinted, and the top bar, preset bar and preset browser each get theirs.

**Text entry.** Exactly one `fxme::TextEntryFocusFixer { *this }`
([../Source/PluginEditor.h:310](../Source/PluginEditor.h#L310)), which is what
the right-click value entry on every number box requires.

**Controls.** No bare `juce::Slider`, no `juce::ToggleButton`, no
`setTextBoxStyle`. The `juce::Slider::` hits in `Theme.h` are colour ids used to
style `FxmeSlider`/`FxmeNumberBox`, which is the intended use. Parameter-bound
toggles use `fxme::FxmeButton`; the plain `juce::TextButton`s (Compute, Perform,
Modal design, Advanced, Load) are actions and mode switches, not parameters, so
they are not `FxmeButton` candidates. `PointToggle` is a bespoke `juce::Button`
because it draws the plate marker itself, which no shared control does.

**Bipolar controls.** No `drawFromCentre` anywhere; the pan controls use
`setCentralValue()`, which is the recommended call.

**JUCE 8 deprecations.** No `juce::Font (number)` and no `createWriterFor`. The
two `.setFont (12.0f)` hits in `ShapeCanvas.cpp` are `juce::Graphics::setFont
(float)`, a genuine overload the sweep explicitly leaves alone, not the
deprecated `Font` conversion.

**State.** `getStateInformation`/`setStateInformation` are both present and the
root carries a `version` property. Backward-compatibility branches were
deliberately removed on 2026-08-30 while the plugin is unreleased and
single-user; the version stamp was kept as the hook for the first release. That
is a considered decision, not an omission.

**Presets.** `fxme::PresetManager` on the processor, `PresetBarComponent` in the
top bar and `PresetComponent` in an overlay. A preset carries the plate geometry
(folded into `apvts.state` on save) but not the computed modes, which would be
megabytes; loading recomputes them in the background.

**Embedded audio.** Not applicable. The plugin loads no audio files. The one
`loadFileAsString` is the JSON geometry reader, which is text and a deliberate
user action.

**Realtime safety.** `processBlock` opens with `juce::ScopedNoDenormals`
([../Source/PluginProcessor.cpp:516](../Source/PluginProcessor.cpp#L516)). No
allocation, locking, file or console I/O in it or anywhere in `PlateSynth`.
Every parameter is read through `std::atomic<float>*`. The model handoff is an
acquire/release atomic pointer with generation acknowledgement, and
`reclaimModels` is called only from `releaseResources` and `publishModel`, both
message thread. `getPlayHead()` is never called, so there is no unguarded
dereference. The one heap buffer on the audio path
(`pickupMeterScratch`) is sized in `prepareToPlay` and guarded against a longer
block rather than resized.

**Registration completeness.** One plugin target, present in the root
CMakeLists and in the README. There are no CI workflows to be missing from
(see S3), so there is no inconsistency to report.

---

## Coverage caveats

What a static audit cannot see:

- [ ] Paste the warnings from your last full build. The JUCE 8 sweep here is
      grep-based, and the deprecations surface as compiler warnings.
- [ ] Confirm the plugin target itself still compiles and links. The four test
      targets were built and run during recent work, and the three plugin
      translation units pass `-fsyntax-only` with `-Wall -Wextra`, but nothing
      has linked `ModalDish_VST3` in this session.

The renamed-API sweep is a grep for old spellings, which finds nothing because
this project uses none of that surface. It is not evidence that the project
compiles against the current FxmeTools: that only fails at compile time, and the
submodule currently carries an uncommitted enum reorder that no build has seen.

---

## Commit plan

Nothing here is staged or committed. The order matters because the submodule
pointer has to move after its own commit.

- [ ] In `lib/FxmeTools`: commit `core/FxmeTools/acoustics/PlateModes.h` (the
      boundary-condition enum reordered by stiffness, plus its doc comment).
      Push it, since the parent will pin the pushed SHA.
- [ ] If H5 is taken, that is the same submodule commit or a following one.
- [ ] In `ModalDish`: commit the source work, then the bumped `lib/FxmeTools`
      pointer.
- [ ] Add `Source/Assets/splash.png`, `Source/Assets/icon.png`,
      `Source/Presets/`, `Source/ShapeFile.{h,cpp}`, `Tests/ShapeFileTests.cpp`
      and `Shapes/`; `Source/Assets/splash.jpg` is deleted.
- [ ] This report is deliberately left unstaged.

Build commands, yours to run:

```sh
cmake -B build -DMODALDISH_BUILD_TESTS=ON
cmake --build build -j2 --target ModalDish_VST3
ctest --test-dir build --output-on-failure
```

Then copy the `.vst3` into the VST3 folder and rescan; the build does not
install and the DAW caches the module.
