# Aureon compatibility implementation

The target is the original Aureon Studio commit
b2b01a55893bd6372a6d00759922025de51846a5 in a macOS Native AOT host.
Production WebScene defaults remain Avalonia 11; sample host uses the existing
Avalonia 12 opt-in. No synchronous worker substitutes or simulated GPU output.

Implementation order:
1. JavaScript modules: URL resolution, module identity/cache, cycles, live bindings,
   import.meta.url, dynamic import, errors and navigation disposal. Add native
   regression tests using real resource loaders, including negative cases.
2. Secure contexts: accurately report trustworthy origins without granting GPU
   admission to untrusted documents. Test localhost, loopback, HTTPS and negatives.
3. Dedicated module workers: separate isolate/background execution, structured
   cloning and ArrayBuffer transfer, ordered messages, error reporting and shutdown.
   Test responsiveness, buffer ownership, termination and document teardown.
4. WebGPU compute resources/commands: descriptors, ownership and lifetime, validation,
   bind groups, dispatch and copies; test real Dawn buffer results and invalid use.
5. Async pipelines: native Dawn completion delivery through the existing wake path,
   promise/error semantics and cancellation. Test success/failure/device loss.
6. Original Aureon acceptance: raster geometry, geometry rebuild/edit/undo,
   progressive compute output, resize, orbit, save/open, AOT regression and bundle.

Do not interpret shell painting, API presence, or successful compilation alone as
application success. Record remaining gaps and measure physical presentation
separately from scripted workloads.

## Implemented and exercised

The ordered work now reaches the original raster editor and progressive path
tracer. Further gaps found during acceptance were implemented: structuredClone,
samplers, getBindGroupLayout, GPUQueue.onSubmittedWorkDone, buffer/texture copy
commands and SVGElement.style. Generated DOM and GPU enum catalogs are updated
with their generator inputs.

Native tests exercise module cycles/live bindings/import identity, clone/transfer
atomicity, background worker replies and infinite-loop termination, secure-origin
positive/negative cases, real compute output, texture transfer bytes and failed
async pipeline validation. Project-owned WPT contracts exercise module workers,
cloning and SVG style/namespace behavior.

The AOT acceptance uses original application methods for edit/undo/rebuild,
resizing, four progressive compute samples and HDR readback. The initial passing
HDR result was 1185x554, finite=true, RGB range 0..13.836970329284668.
See README for intentionally unqualified APIs and optional recovery/export gaps.

## macOS qualification — 2026-09-09

- Native CTest: 16/16 suites passed, including real Metal/Dawn compute and
  worker-created transferred buffers surviving worker termination. ArrayBuffer
  allocators use V8 shared ownership so transferred backing stores can outlive
  their originating isolate.
- Project WPT contract profile: 2/2 documents, 14/14 subtests passed.
- Avalonia 11 backend regressions: 310 passed, 8 skipped on each of net8.0 and
  net10.0 (before the final allocator lifetime fix; native tests rerun afterward).
- DOM/enum generation check: passed.
- macOS arm64 Native AOT publish: passed production AOT/trim warning gate.
- Final bundle, without asset/native-library environment overrides: original
  edit/undo/redo/BVH rebuild, resize, four progressive samples and HDR readback
  passed; 341 rendered scenes, zero JS/console errors, one explicitly reported
  IndexedDB recovery warning; process exited successfully after disposal.
- Bundle deep/strict code signature verification passed (local ad-hoc signing).

The standalone check caught and corrected a stale bootstrap snapshot in staging.
The shipped engine and both snapshot files now come from the same native build.
