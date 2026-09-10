# PR #43: macOS and Windows WebGPU milestone

The agreed scope is the existing Avalonia native WebGPU rendering path on macOS (Metal) and Windows (Dawn D3D12 with ANGLE/D3D11 interop), running the unchanged Kestrel CAD fixture. This is a bounded milestone within epic #22. Subsequent application work adds AOT-safe resource/DOM compatibility and the macOS-first HTML media/Web Audio milestones (#54/#55). Windows/Linux video hardware parity remains #61/#62; the complete Frameforge editor remains #53.

## Merge acceptance

- Required ordinary CI, existing runtime packaging and package-consumer checks pass on the final revision. Linux baseline checks remain enabled to prevent regressions in existing support; they do not claim Linux GPU parity.
- macOS and Windows native builds, applicable native/managed regressions and unchanged Kestrel startup, editing/undo/redo, panning, sidebar/window resizing and BOX command have evidence for the integrated production code.
- Normal GPU presentation retains bounded image ownership and GPU synchronization, without a per-frame CPU pixel transfer. Retained-image resizing is compared against Chrome; a browser difference must be understood before claiming parity.
- NativeAOT is a product requirement. The Kestrel native executable must render successfully; generated checkpoint/archive serialization is covered by native executable CI probes. Broader unqualified interop APIs remain an explicit audit item, not a blanket AOT support claim.
- Unexpected runtime errors observed during validation are diagnosed and addressed or explicitly scoped with supporting evidence.
- Skipped platform tests and callback timing are not presented as complete conformance or physical scanout measurements.

## Deferred epic work

Browser WebGL 1/2 APIs and WebGPU-to-WebGL fallback, Linux GPU parity (#46), Uno GPU qualification, broader WebGPU APIs, workers/OffscreenCanvas, full CTS/WPT conformance, exhaustive loss/recovery and long-run qualification remain later epic work. They are not merge requirements for this milestone. Collapsed select popup support remains #44. These deferrals do not close epic #22 or mark its incomplete children complete.

## Binary packaging

This milestone ships multiple native libraries. V8, miniaudio and the existing static native dependencies are linked into WebScene's native engine. Dawn's shared monolith remains separate. macOS uses Dawn/Metal without ANGLE; Windows also ships ANGLE's EGL/GLES libraries. ANGLE is needed for the current Windows composition path even though browser WebGL fallback is deferred. Host Skia/native assets and runtime data also follow their existing packaging contracts.

A single distributable installer or archive is different from a single native binary. Static integration of all GPU libraries is outside this PR: it needs dependency symbol isolation (including the V8/Dawn Abseil collision), platform linking and package-consumer verification. OS graphics frameworks and drivers remain external.

## Final application smoke checks

Use the exact native runtime revision intended for packaging, including matching
Dawn, ICU and snapshot sidecars. Keep the original application assets unchanged.

- Kestrel: Native AOT with reflection JSON disabled; startup, edit/undo/redo,
  pan, sidebar and continuous/stepped window resize. Probe traces must use generated
  serialization so diagnostics cannot fail only in published builds.
- Frameforge: package with `experiments/WebScene.Frameforge/package-macos.py`, then
  run `verify-media.py` with the executable, native library and assets inside that
  bundle. `--media-demo` is the interactive player; it is separate from the editor.
- TradingView: live chart readiness through `--startup-profile`; interactive
  panning/performance remains distinct from this startup check.
- Final-revision CI must include all native packages and package consumers, not
  just managed builds or SDK cache validation.

The media contracts qualify the documented subset, not complete browser APIs.
Physical frame cadence, identical Chrome resize transients and long-duration A/V
latency must not be inferred from automated callback timestamps.
