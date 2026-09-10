# Native Kestrel port (in progress)

Source baseline: https://github.com/wieslawsoltes/KestrelCAD at
`7a1e84c67fd24410c22f0d1a45b2e54120b32d0e`. Upstream MIT attribution is in LICENSE.

The target is a compiled HTML/CSS interface with predefined dynamic templates,
C++ application logic and WebScene native WebGPU, hosted by Foco. No JavaScript
runtime or runtime HTML parsing is permitted in the resulting application.

The port is not yet runnable. `native/math.hpp` begins the port of src/math.js:
vector operations, transforms, inverse, angles, intersections and polygon area.
The remaining math, drawing model/history/persistence, geometry and renderer,
commands, UI templates and the other application modules remain to be ported.
This directory is not a claim of Kestrel feature parity.
