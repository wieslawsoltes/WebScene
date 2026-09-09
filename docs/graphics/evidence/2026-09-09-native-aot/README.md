# NativeAOT Kestrel on macOS

The initial NativeAOT publish initialized Kestrel/WebGPU but displayed a white window. First-chance exception tracing reported repeated `The macOS IOSurface route requires a current host CGL context`. Reflection over the runtime host type could no longer identify `IMetalDevice` after trimming, so presentation selected the wrong importer.

The shared Metal importer now checks the interface directly and reflects its hidden reference-assembly properties through the statically known interface type. NativeAOT retains those accessors. The attached window screenshot confirms the native executable rendered the UI and original CAD canvas.

Validation on Apple M4/macOS:
- NativeAOT publish with reflection JSON disabled; native executable, no CoreCLR required.
- Original immutable Kestrel ZIP, WebGPU active, BOX, LINE, undo/redo and four window sizes; zero document exceptions and successful scene presentations. `--verify-kestrel` now fails if no scene is successfully rendered.
- `--aot-serialization-probe` verifies checkpoint PNG/state fields and capture/replay resource archive in the actual native executable.
- Generated JSON contracts replace reflection serialization in canvas checkpoints and resource archives. Presenter/checkpoint diagnostic JSON uses explicit nodes.
- Avalonia regressions: 310 passed, 8 skipped per net8.0/net10.0. Checkpoint and HTTP capture/replay regressions also pass with reflection JSON disabled on both targets.
- CI publishes and runs the native serialization probe on macOS and Windows and rejects linker AOT/trim warnings in reachable production source. This headless check does not qualify GPU presentation on hosted runners.

Publish command:

```sh
dotnet publish experiments/WebScene.GpuHost.Probe -c Release -r osx-arm64 \
  -p:PublishAot=true -p:JsonSerializerIsReflectionEnabledByDefault=false \
  -o artifacts/kestrel-aot/publish
```

The local app bundle additionally contains the graphics-enabled native engine, Dawn/ANGLE and Skia/Avalonia libraries, ICU/snapshot data and original Kestrel archive. Absolute SDK rpaths were removed from bundled libraries; sibling libraries resolve through loader-relative paths. This is a local ad-hoc signed multi-file app bundle, not a notarized release or single binary. JavaScript still runs in V8.

AOT compatibility is a required product constraint. This evidence qualifies the exercised Kestrel paths only. Other generic interop/converter APIs and probe diagnostics still produce compile-time warnings and need a broader audit; no blanket codebase AOT certification is claimed. NuGet graphics integration and removal of ANGLE from the macOS runtime are addressed in the follow-up below; hosted verification remains required.

## Dawn-only macOS and release packaging follow-up

macOS now compiles the native service without ANGLE contexts and links only Dawn. The 16 remaining native tests passed, including actual Metal/Dawn hardware tests locally. The three ANGLE-specific tests still apply on Windows/Linux builds, where ANGLE remains enabled.

The release workflow now supplies the verified graphics SDK to macOS and Windows native package builders. Both it and SDK validation use one shared exact-cache recipe. SDK cache identities now include that shared action, causing one cold rebuild; later exact hits reuse compiled SDKs. Linux release packages retain their existing non-GPU scope.

Hosted package builders explicitly exclude hardware-labelled tests; they compile GPU code and test CPU/package contracts. They do not substitute for the locally recorded hardware evidence. The V8 graphics runtime suite is correctly labelled hardware because it requests actual devices.

A real local macOS NuGet package was packed, its graphics manifest/library hashes validated, then restored into an independent NativeAOT consumer and published. dyld confirmed the engine and Dawn both loaded from that consumer's publish directory. No ANGLE library is in that package. The local test package uses the existing non-PartitionAlloc development SDK; the release workflow retains its pinned PartitionAlloc configuration and still requires CI validation.

The updated Dawn-only app bundle passed BOX and four window sizes with 239 successful scene renders and zero JavaScript exceptions. Release verification now rejects the previous non-graphics macOS package, a missing/corrupt Windows D3D compiler, and accidental ANGLE inclusion on macOS. Graphics tooling tests: 20 passed, one Windows-only skip.
