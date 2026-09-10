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

`kestrel.viewport` connects drawing/camera/grid rendering to WebScene's native
shared-image surface. Hosts submit and poll without blocking the UI thread;
completed leases use the existing Foco GPU adapter. The Metal test exercises
three resized drawing frames and pending-frame backpressure. Foco window wiring,
annotation drawing and compiled Kestrel controls remain outstanding.

The initial compiled UI scaffold is in Main.html/Main.css: toolbar, layer panel,
viewport and status. Layer rows are standard HTML templates compiled into the
`kestrel.ui` module. A native test constructs the drawing's layers, attaches C++
visibility handlers and lays out the viewport without runtime HTML parsing.
This is an initial scaffold, not the complete upstream UI; toolbar commands,
remaining panels, full CSS coverage and window integration remain outstanding.

First Foco window proof: build `FocoKestrel` in the Foco-enabled configuration and
run `FocoKestrel.app/Contents/MacOS/FocoKestrel --capture /absolute/path.png`.
The app imports the native controller/viewport modules and presents shared GPU
images with the existing Foco adapter. The first capture shows compiled layers,
toolbar, grid and a box. This remains an incomplete application: most CAD commands/panels are absent, and
interactive lifecycle/resource packaging acceptance is not complete.

The initial truncated capture was fixed by closing the PNG stream before host
shutdown. A subsequent capture verifies the full viewport and status bar.

The POC Pan tool now moves the native camera through DOM pointer events and stops
on release within the application. Native tests cover movement and release.
Platform pointer capture/cancellation outside the window, wheel zoom and original
Kestrel gesture mappings remain outstanding; this is not full input parity.

The POC Line tool converts viewport pointer positions to the world XY plane and
creates consecutive native LINE entities in undoable transactions. Cancel ends
the operation. Tests exercise creation, undo and redo through DOM events. Snaps,
preview geometry, numeric command entry and original command semantics are still
pending; the POC is not yet a full replacement for upstream's Line tool.

The Foco capture runner accepts `--exercise-commands` alongside `--capture`.
It hit-tests compiled controls, creates a line through native DOM pointer events,
checks undo/redo entity counts and requires a subsequent GPU frame. The resulting
capture visibly contains the line. This tests the hosted DOM/controller/render
path; it does not inject operating-system mouse events.

### Foco frame scheduling

The native host uses `window::requires_host_frames()` and
`window::advance_host_frame()` through Foco's Cocoa display driver, matching
FocoUI's `samples/NativeKestrel` integration. Do not introduce an independent
fixed-interval rendering timer. GPU images continue through Foco's existing
WebScene IOSurface import and retained compositor resources.

Performance parity is not yet established: the native viewport currently
publishes completed frames and rebuilds/uploads scene geometry for camera-only
updates. Compare these with the existing WebScene integration before claiming
panning parity; preserve its synchronization and frame admission design when
extending the native API.

### Original-layout diagnostic

Run `FocoKestrelPreview.app/Contents/MacOS/FocoKestrelPreview --dump-layout`
to print the compiled original shell's major element bounds at 1280×800 and
1280×1000 without opening a window. This checks the static shell; dynamically
instantiated ribbon content is not included in this mode.

Current observation: the shell occupies the full viewport, but the status bar
ends at y=660 and y=860 respectively, leaving 140 pixels unused. The native flex
layout accounts for the workbench's constrained 140-pixel minimum in its base-size
sum, then uses its unconstrained zero flex basis during final size assignment.
The allocation must use consistent bases; this diagnostic does not yet fix it.
