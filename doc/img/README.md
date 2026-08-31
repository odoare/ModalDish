# Screenshots for the README

Eight shots, in the order the README uses them. Every one is referenced as
`doc/img/<name>.png`, so dropping the files here is all that is needed.

The window is **1022 x 760**, or **1142 x 760** with the Cascade column open.
Capture the plugin window alone (no host chrome, no desktop) at 1:1, PNG. The
standalone build is the easiest source. Panel crops should keep a little of
the surrounding background so they read as part of the same interface.

Suggested subject for each: a plate that looks interesting rather than the
default ellipse, so the shots also advertise what the plugin can do.

| File | Mode | What it should show |
| --- | --- | --- |
| `perform.png` | Perform, 1022 wide | The hero shot, first thing in the README. A computed non-obvious plate in *Displacement* view mid-ring, several pickups and sources switched on and visibly spread, at least one source pulled into an `a`-`A` segment. TRANSDUCERS row showing a mixed on/off pattern. |
| `design.png` | Modal design, 1022 wide | A hand-drawn or loaded outline with the mesh visible underneath, the border cut into four or more segments carrying *different* boundary conditions (so the colour key in the sketch corner earns its place), and the *Draw shape* or *Edit boundary* tool selected. |
| `points.png` | Modal design, crop | The *Place points* tool on a polygon of eight to twelve vertices, square handles visible, ideally mid-drag. A crop of the plate area (roughly 620 x 560) rather than the whole window. |
| `advanced.png` | Perform, 1142 wide | The same as `perform.png` but with **A >** pressed, so the CASCADE column is on screen. This is the one that shows the full control surface, so let the right-hand panels be legible. *Cascade* should be up, not at 0. |
| `view3d.png` | Perform, crop | *Displacement 3D* or *Velocity 3D*, rotated to a three-quarter angle so the surface reads as a surface. A crop of the plate area. |
| `pickup-panel.png` | Perform, crop | A pickup panel open over the plate, its meter showing something (capture while the plate is ringing, not silent). Crop tight enough that the panel is the subject but the plate is recognisable behind it. |
| `source-panel.png` | Perform, crop | A source panel open, with the five-by-three grid legible: *Pos Ctl* on Vel, *Frc Ctl* on a CC number, *Ham Ctl* on Off, so the three controls visibly differ. A *Note* assigned. Same crop treatment. |
| `presets.png` | Either, 1022 wide | The preset browser open over the working area, with more than one preset in the list. |

Two of these carry information the prose does not: `design.png` is the only
place the boundary-condition colours appear, and `source-panel.png` is the
only place the grid layout is shown as a picture rather than as a table. Those
two are worth the most care.
