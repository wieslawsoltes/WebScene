# Native Kestrel port (in progress)

Source baseline: https://github.com/wieslawsoltes/KestrelCAD at
`7a1e84c67fd24410c22f0d1a45b2e54120b32d0e`. Upstream MIT attribution is in LICENSE.

The target is a compiled HTML/CSS interface with predefined dynamic templates,
C++ application logic and WebScene native WebGPU, hosted by Foco. No JavaScript
runtime or runtime HTML parsing is permitted in the resulting application.

The port is not yet runnable. `native/math.cppm` and `native/camera.cppm` port the
geometry math, triangulation and camera operations from src/math.js. Camera tests
compare 154 values against the pinned upstream implementation; additional native
tests cover transforms, concave/vertical triangulation and intersection behavior.
The renderer, commands, UI templates and other application modules remain to be
ported; drawing and geometry coverage is described below.
This directory is not a claim of Kestrel feature parity.

`native/drawing.cppm` ports the core drawing/project representation, layer rules,
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

`native/geometry.cppm` ports curve tessellation (including rational splines and
bulged polylines) and box, cylinder, cone, sphere and torus construction. Reference
tests compare 365 curve vertices and five meshes, including exact face topology,
against the pinned upstream JavaScript implementation. JavaScript is only the
reference oracle; these native tests do not execute it. General entity geometry,
text/dimensions, solid operations and the GPU renderer remain outstanding.

Shared UI markup must use standard HTML `<template>` elements. Browser code can
clone their content; native code instantiates compiler-generated construction
functions. Dynamic content must not introduce runtime HTML parsing in the native
application. Browser JavaScript and native C++ application logic remain separate.

Native hatch-line generation now covers cross patterns, concave boundaries and
non-XY planes, with endpoint comparisons against upstream geometry output.
Solid hatch triangulation still needs wiring into the entity geometry output.

Dimension geometry now emits extension lines, arrows and label metadata in C++.
Reference cases cover default placement, negative and zero offsets, tilted planes,
custom labels and coincident endpoints. Text orientation axes are also available.
Numeric labels currently use C++ fixed formatting; exact JavaScript `toFixed`
rounding at decimal ties and scientific formatting for very large values remain
to be matched before claiming full label parity. Text rasterization and integration
with the renderer are still outstanding.

Native extrusion and signed mesh volume are implemented. Reference comparisons
cover boundary cleanup, concave profiles, negative heights and non-XY profiles,
including exact cap/side topology and volume. Revolution is also implemented, with full, partial, negative and translated-axis
reference cases. Seven invalid-profile/sweep/axis cases are rejected by both
upstream and native implementations. Other solid operations and renderer
integration remain outstanding.

Offset and three-point arc construction are now native, with upstream reference
cases for lines, closed and bulged polylines, conic axes and arc direction.
These operations still need connection to the native command/UI layer.

The native implementation now uses C++20 named modules (`kestrel.math`,
`kestrel.camera`, `kestrel.drawing`, `kestrel.geometry`) with CMake module dependency
scanning. Build with CMake 3.28+ and a supported compiler; LLVM 22 with Ninja is
verified on macOS. Configure an explicit `CMAKE_OSX_SYSROOT` from
`xcrun --show-sdk-path` when using Homebrew LLVM. Third-party JSON remains a
header dependency in global module fragments and JSON-consuming tests. Generated
HTML/CSS view modules remain to be implemented.

Entity transforms now preserve native conic axes, normal transforms, hatch
spacing, dimension offsets, text orientation and mirrored mesh winding. Seven
upstream comparisons cover these under reflection and nonuniform scaling.

Mesh edge extraction now preserves upstream boundary/crease semantics, including
coincident edges with separate vertex indices. Reference tests compare both
feature-only and all-edge output for boxes, extrusions and split coplanar faces.
The renderer-facing aggregate geometry output still needs integration.

The native `geometry` API now assembles segments, wire edges, triangles, text,
snap points and bounds points. Complete output is compared against upstream for
curves, hatches, dimensions, a mesh, Unicode multiline text and point entities.
This provides renderer input; GPU rendering and command/UI integration remain
outstanding. Numeric dimension-label formatting retains the limitation above.

`kestrel.render_data` defines the upstream GPU line-instance and triangle-vertex
layouts with checked strides and offsets. Coordinate packing subtracts the scene
origin in double precision before conversion to floats; a large-coordinate test
verifies sub-unit detail is retained. GPU pipelines, scene styling and upload/
submission still need implementation; this module alone does not render a frame.

Native scene assembly now applies layer visibility, selection colors, line styles,
lineweights, locked opacity and wireframe/shaded/xray modes to GPU upload data.
Tests cover display-mode primitive counts, selection, opacity and hidden layers.
Production entity expansion and geometry caching are not yet connected; scene
assembly currently processes the drawing's direct entities.

The original WGSL line/mesh shader source is embedded in `kestrel.shaders`, with
no JavaScript dependency. Camera uniforms now match its 96-byte layout and use
the origin-relative MVP and eye position. Native packing tests pass; creating
and validating GPU pipelines with these shaders is still pending.

`kestrel.gpu_pipelines` now constructs the native camera bindings and three
upstream pipelines (instanced AA lines, lit meshes and xray meshes), including
4x MSAA, blending and depth states. Creation succeeds on Metal through WebScene's
native device helper without V8. Frame submission/readback and visible Kestrel
rendering remain unverified; pipeline creation alone is not an end-to-end test.

`kestrel.gpu_renderer` now uploads native vertices/uniforms, manages 4x MSAA and
depth attachments, submits mesh/line draws and resolves to a supplied texture
view. A Metal readback test verifies red triangle pixels, green line pixels and
background pixels from a real submitted frame. Readback is diagnostic only.
This test does not yet exercise Foco presentation, Kestrel UI, resize sequences,
grid rendering or annotations; those remain required integration work.

Adaptive grid generation now follows upstream zoom spacing, major/minor lines
and colored axes. The renderer accepts a separate grid buffer and draws it
before scene triangles and lines. CPU spacing/count and existing GPU regression
tests pass; a visible integrated viewport remains outstanding.
