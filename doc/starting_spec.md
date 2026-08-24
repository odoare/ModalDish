FemPlate is a Juce plugin that simulates the vibration of plates or mambrane of arbitrary shape with the following design spec for the user perspective:

- The user draw a shape on a square canvas with is then approximated/smoothed using a spline. The user can also define standard shapes such as ellipses or rectangles with given aspect ratio.

- A finite element grid can be created after a shape has been defined

- On these segments the user can choose de type of boundary condition : Clamp, simple support, free, sliding support

- The user can move Ns points around the border that define N segments in between

- Nm modes can then be calculated, as well as the eigenmodes on the finite elements degrees of freedom. The equations are that of linear plate dynamics with flexural and viscous damping, scaled so that the term in front of the flexural stiffness is unity. The user selects the first eigenfrequency f1, the other Nm frequencies are scaled accordingly

- The user can then display individual modes, by selecting a number, shown as filled contours

- If it involves a not too complicated calculation, the user could be able to rotate the plate by mouse click-drag

- Next, once the modes, frequencies and dampings have been calculated, the user cal click anywhere on the plate to excite the plate along its modes

- The tuning parameters are : tension to flexural stiffness parameter (a dimensionless parameter in front of the tension, order 2 derivatives once the equations have been scales so that the flexural stiffness is 1), viscous damping (term in factor of dy/dt, once equations are scaled), material damping (in factor of d^5 y/dy^4 dt), base frequency f1, duration of the hammer shock (typical half sine shock), the position on the plate where the signal is outputed.

- It will be possible also to let the user to send any signal to the plate input, thus allowing this to work as an effect

In terms of internal design here are the specs:

- We should develop an independent component for the graphical drawing of the shape

- We should develop an independent set of functions to solve by finite elements the plate equations, and independent component to display a FEM grid, and a solution on the grid using filled contour. This should be done e.g. in the acoustics folder of FxmeTools, included in the project repo as a git submodule, using a clear and documented API allowing us to reuse it in other projects.

- The way the system is simulated can be that from the MechanOdd project in ~/src/MechanOdd, where rectangular plates are simulated, using resonant filter banks. MechanOdd is included in the workspace for reference. Once we have frequencies, normalized damping (or quality factor), and the value of each eigenmode at the hit point, the signal to feed in the filters is fully determined. 

- The plugin interface should follow the new Fx-Mechanics design, with a top bar with tiltle and logo, you can look at the project Mango for instance: ~/src/Mango

Nonlinear extension:
1. amplitude-driven dynamic tension
2. Simplified model of energy cascade
