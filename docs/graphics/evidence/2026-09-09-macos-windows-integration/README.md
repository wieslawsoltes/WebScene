# macOS validation of Windows integration

Validated production revision `755f808837b89e5181a6a4b05f9a7f22558b57b1` in a clean checkout on the existing Apple M4 macOS host, with the pinned macOS Dawn/ANGLE SDKs and graphics enabled. Existing uncommitted dialog UA/test edits in the original worktree were excluded.

- Release native and managed GPU host builds passed.
- All 19 native CTest suites passed (17.08 seconds).
- The unmodified Avalonia tests found an exact-pixel failure in `RasterCheckpointPreservesFractionalClearPathTransformAndExport` on both frameworks: alpha 0x91 versus 0x92. The comparison bitmap used the platform default whereas PNG export uses BGRA8888/premultiplied. Matching that surface format fixes the test without tolerances or production changes. After this correction, with the current native library supplied, .NET 8 and .NET 10 each passed 310 tests with 8 skipped.
- Unchanged original Kestrel ZIP/document checksums were verified by the host. Native Metal WebGPU startup, line edit/undo/redo, four stepped window sizes (980x680, 1440x900, 1100x740, 1280x800; DPR 2), validated pan and left-sidebar drag, and BOX creation passed. Each verification process exited 0. Command-history errors remained zero.
- The screenshot was taken from a separate live run of this production revision.

Commands from the checkout root:

```sh
ctest --test-dir artifacts/checkpoint-native --output-on-failure
WEBSCENE_TEST_NATIVE_LIBRARY="$PWD/artifacts/checkpoint-native/libwebscene_native_engine.dylib" dotnet test tests/WebScene.Backend.Avalonia.Tests -c Release -m:1
```

For each Kestrel check use `dotnet run --no-build -c Release --project experiments/WebScene.GpuHost.Probe -- --webgpu-metal --kestrel tests/GraphicsCompatibility/fixtures/Kestrel-CAD.zip`, with that same native library override, followed by:

- `--edit-kestrel --resize-kestrel --verify-kestrel`
- `--pan-kestrel --sidebar-kestrel --verify-kestrel`
- `--mesh-kestrel --verify-kestrel`

Scope: this validates the existing Metal Kestrel path after integrating Windows changes, not full WebGPU/WebGL conformance, physical 60fps, exhaustive app behavior, or all skipped platform tests. Pan telemetry contains an engine ScriptErrors count of 1 despite zero Kestrel command-history errors and successful workload validation; this report does not claim zero engine-wide errors or a new performance baseline. No native user-resize recording or full Chrome comparison was repeated.

## Stabilization follow-up

The previously unexplained engine ScriptErrors counter came from the pan probe's own ResizeObserver: it appended to `p.widths` without initializing that array, and the observer was left connected after cleanup. The probe now initializes the array, disconnects the observer, prints runtime/JavaScript failures, and rejects Kestrel verification when uncaught JavaScript exceptions occurred. Two subsequent pan attempts recorded engine errors 0 before/after but were rejected for unrelated zero-button pointer movements from the desktop. They are not new interaction or performance passes.

The graphics metadata fixture explicitly targets `win-x64`; an additional host-OS-dependent compiler assertion conflicted with that target on macOS/Linux. Removing that duplicate preserves the existing exact `clang-cl.exe` assertion. The tooling suite passes locally (16 passed, one Windows-specific test skipped).

Final original-Kestrel BOX plus four-size resize/startup verification exited 0 with zero uncaught JavaScript exceptions; see `stabilization-box-resize.log.gz`. Production renderer code was unchanged by these probe/test corrections.
