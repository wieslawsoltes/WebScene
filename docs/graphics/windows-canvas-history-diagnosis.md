# Windows Kestrel canvas history diagnosis

This records the pre-fix diagnosis. The eligible Windows GPU path now retires
history through [raster checkpoints](windows-canvas-checkpoints.md), with a
160-second bounded-history qualification. Physical 60 Hz timing remains pending.

## Finding

The growing history belongs to Kestrel's Canvas2D annotation overlay, even while
the main scene uses WebGPU. WebScene retains commands as the authoritative canvas
content and only discards them on a proven full overwrite or bitmap reset. A
fractional clear prevents that optimization. Preserving the remaining pixels is
correct; retaining and republishing every historical command indefinitely is an
architectural limitation, not a requirement of canvas semantics.

This analysis inspected the current source and existing qualified traces. It did
not change rendering behavior or collect a new physical-presentation measurement.

## Exact trigger

The immutable fixture's `Kestrel-CAD/src/renderer.js:116-119` rounds CSS dimensions,
then separately rounds their product with DPR to allocate the bitmap. Lines
376-379 set the Canvas2D transform to DPR and clear using CSS dimensions.

At the default 1280-pixel window and 175% scale:

* CSS overlay: 806 by 463.
* Bitmap: round(806 × 1.75) by round(463 × 1.75) = 1411 by 810.
* Transformed clear: 1410.5 by 810.25.
* The clear does not fully cover the last pixel column.

`canvas_clear_rect` in `webscene_v8_runtime_canvas.inc` therefore does not call
`canvas_compact_full_overwrite`. Each subsequent annotation draw appends another
frame to the same canvas generation. The width-1282 control produces an exactly
aligned 1414-pixel clear and stays around 320 commands, supporting this diagnosis.

The recorded Chromium GPU edge experiment (`edge-clear-control.log`) preserves
alpha 128 after clearing 9.5 pixels of an initially opaque 10-pixel canvas, and
alpha 64 after clearing 9.75. Silently rounding the engine's clear outward would
change pixels. A client-side identity-transform clear of the whole bitmap could
express Kestrel's apparent full-clear intent, but would not fix WebScene's handling
of legitimate partial clears, trails, or accumulating drawing applications.

## Where history remains

1. **Native canvas state:** `canvas_node_data.commands` and its string table retain
   the generation's complete command stream. No raster checkpoint retires an old
   prefix after its pixels have been produced.
2. **Publication:** `native_document::build_canvas_display_lists` still copies all
   commands with `canvas_commands.insert`. Publication now compacts changed layers
   in place and transfers that allocation, avoiding a second copy, but the first
   full-history copy remains. It happens before unchanged-layer filtering.
3. **Protocol:** `WEBSCENE_CANVAS_LAYER_UNCHANGED_PREFIX` proves prefix identity;
   it does not encode an append-only payload. The scene still contains complete
   replacement commands. Older consumers may safely ignore the hint.
4. **Managed CPU representation:** `NativeCanvasBacking.cs` preserves `CpuHistory`
   for CPU rendering/export and GPU-context fallback. Its balanced picture forest
   avoids deep chains and repeated compilation, but merging pictures retains their
   contents. Memory and full CPU replay still grow with drawing history.
5. **GPU representation:** `MaterializeCanvasBacking` records the previous raster
   image plus new commands into a new image. This removes full-history GPU replay
   on the supported path, but does not retire native commands or the CPU forest.

For a roughly constant number of commands per frame, retained history is O(N)
after N frames. Copying it every publication makes cumulative copying O(N²).
Prefix hashing and dependency caching reduce other scans; they do not change this.

## Evidence and limits

The latest one-minute circular trace, `pacing-pool-budget-minute.log`, reports:

| Measurement at end of run | Value |
| --- | ---: |
| Retained Canvas2D layers | 1 |
| Retained Canvas2D commands | 997,662 |
| Command data, at 80 bytes per ABI command | 79,812,960 bytes |
| Native canvas storage metric | 84,011,596 bytes |
| Latest scene storage metric | 94,726,905 bytes |
| Last native publication | 8.32 ms |
| Maximum native publication | 15.26 ms |
| Host frame callbacks | 60.00/s |
| Completed draw callbacks | 56.00/s |
| Fallback clock ticks | 0 |
| Blocked publications | 0 |

The storage metrics include reserved vector capacity; they are not exact logical
payload sizes or total process memory. The command count gives the separate
79.8 MB logical command figure. A single RGBA bitmap at 1411 × 810 requires
4,571,640 bytes before alignment, buffering and driver overhead.

The 64 MiB free-scene cache rejected sufficiently large buffers. Raising its
aggregate budget to 256 MiB improved the one-minute average from 52.13 to 56.00
draws/s and reduced the last publication from 18.97 to 8.32 ms. These are sequential
diagnostic runs, not a controlled statistical benchmark. The increased cache
delays allocation churn; continued growth will eventually exceed it too.

This establishes a substantial CPU/memory bottleneck, not proof that every missed
frame has the same cause. JavaScript, layout, scheduling and drawing also consume
the 16.67 ms frame budget. Draw callbacks do not establish physical scanout.

## Chromium comparison and recommended correction

Chromium's pinned [`CanvasResourceProvider::FlushCanvas`](https://chromium.googlesource.com/chromium/src/+/c2523189e37e70b1b649d84896d4792ce53a080f/third_party/blink/renderer/platform/graphics/canvas_resource_provider.cc)
releases the current recording, rasterizes it into backing storage, and normally
does not retain that recording; printing has special preservation behavior.
The bitmap preserves partial-clear results without needing all earlier commands.

WebScene needs the equivalent lifecycle:

1. Publish bounded command batches identified by canvas generation and sequence,
   rather than complete history. Introduce explicit consumer capability negotiation;
   do not reinterpret the existing replacement payload or prefix hint.
2. Apply batches in order to authoritative raster backing. Acknowledge a batch
   only when its resulting content and resource lifetimes are secured.
3. Retire consumed command prefixes and their resources once no in-flight user
   needs them. Bound producer backlog and pending publications explicitly.
4. Preserve raster checkpoints plus only an unconsumed tail for recovery/export.
   PNG export can read a raster snapshot on demand. Any requirement for vector
   export needs a separate explicit policy; keeping all commands for every canvas
   should not be the ordinary display path.
5. Specify recovery for missing bases, resize/reset, context loss, CPU consumers,
   and canvas-to-canvas dependencies. A managed GPU snapshot alone is not currently
   an authoritative checkpoint that the native producer can recover from.

Incremental publication alone improves bandwidth but leaves native and CPU-picture
memory growth. Raster checkpoints alone leave redundant publication if full lists
are still sent. The durable fix must address both ownership and transport.

Qualification should include several minutes of fractional-clear drawing with
plateauing retained command/resource memory, unchanged edge pixels, continued
export correctness, reset/context recovery, and stable frame-time distributions.
Use physical presentation measurements separately to certify 60 Hz output.
