# Native Kestrel port (in progress)

Source baseline: https://github.com/wieslawsoltes/KestrelCAD at
`7a1e84c67fd24410c22f0d1a45b2e54120b32d0e`. Upstream MIT attribution is in LICENSE.

The target is a compiled HTML/CSS interface with predefined dynamic templates,
C++ application logic and WebScene native WebGPU, hosted by Foco. No JavaScript
runtime or runtime HTML parsing is permitted in the resulting application.

The port is not yet runnable. `native/math.hpp` and `native/camera.hpp` port the
geometry math, triangulation and camera operations from src/math.js. Camera tests
compare 154 values against the pinned upstream implementation; additional native
tests cover transforms, concave/vertical triangulation and intersection behavior.
The renderer, commands, UI templates and other application modules remain to be
ported; drawing and geometry coverage is described below.
This directory is not a claim of Kestrel feature parity.

`native/drawing.hpp` ports the core drawing/project representation, layer rules,
entity mutation, native history snapshots and base project validation. Tests cover
rollback, undo/redo, selection pruning, project round-trip and history bounds.
Production metadata, constraint enforcement/removal, fields and source-archive
validation still require their native modules; retaining their data does not mean
those features are implemented. Geometry caching, entity transforms and spatial
indexing are also outstanding. Layer-name case folding currently covers ASCII.

Project data uses vendored nlohmann JSON 3.12.0 (MIT). Header SHA-256:
`aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`.
This is project serialization only; HTML remains compiled into predefined views
and templates.

`native/geometry.hpp` ports curve tessellation (including rational splines and
bulged polylines) and box, cylinder, cone, sphere and torus construction. Reference
tests compare 365 curve vertices and five meshes, including exact face topology,
against the pinned upstream JavaScript implementation. JavaScript is only the
reference oracle; these native tests do not execute it. General entity geometry,
text/dimensions, solid operations and the GPU renderer remain outstanding.

Shared UI markup must use standard HTML `<template>` elements. Browser code can
clone their content; native code instantiates compiler-generated construction
functions. Dynamic content must not introduce runtime HTML parsing in the native
application. Browser JavaScript and native C++ application logic remain separate.
