# Native graphics prerequisites (G01)

Implements part of [#23](https://github.com/wieslawsoltes/WebScene/issues/23), the first sub-issue of [epic #22](https://github.com/wieslawsoltes/WebScene/issues/22). Browser bindings, GPU canvas leases and production presentation are later issues. Enabling these build prerequisites does not expose WebGPU/WebGL APIs or advertise backend capabilities.

## Dependency and ABI boundary

`dependencies.lock.json` pins Dawn (including Tint and generated WebGPU headers), ANGLE, depot_tools, WebGPU CTS, Khronos WebGL and WPT to exact commits. Dawn's own pinned `DEPS` supplies its transitive source revisions; ANGLE uses its pinned `DEPS` through gclient with depot_tools self-update disabled. The installed package records the transitive Git revision graph, exact CMake/GN settings, tool identities, source licenses/notices and hashes of every installed header/library. There is no ProGPU or renderer replacement dependency.

Dawn uses a shared `webgpu_dawn` monolith exporting only WebGPU C entry points. Static co-linkage with V8 exposed conflicting unversioned Abseil symbols and caused native runtime hangs; the shared boundary keeps those implementations private. `DawnSymbolBoundary.cmake` applies the export policy without patching upstream sources, and the builder checks actual binary exports before sealing the SDK. ANGLE uses shared EGL/GLESv2 with static internal dependencies and C ABI entry points. Windows release builds use static CRT for Dawn, matching the existing native engine/V8; ANGLE's non-component Chromium configuration controls its runtime. Windows build/link and runtime dependency validation (including D3D shader compiler packaging) remain hardware-runner gates. Never exchange CRT-owned allocations between these libraries.

`GraphicsDependencies.cmake` verifies the SDK before importing it, including revision, RID, lock, settings, complete installed file inventory and content hashes. CMake cannot silently choose another `Dawn_DIR`. A dependency roll requires rebuilding headers and libraries together. Package manifests prove integrity and provenance, not API conformance or hardware execution.

Dependency checkouts explicitly set both `core.autocrlf=false` and `core.eol=lf`: upstream `text=auto` attributes otherwise permit Windows-native CRLF license bytes. The optional Dawn C++ module wrapper is disabled; WebScene uses generated C/C++ headers and does not require compiler module scanning. This avoids a hosted GCC configuration that passed the language feature test but lacked CMake import-graph discovery support.

The macOS package step rewrites ANGLE's working-directory-relative install names to `@rpath`, normalizes inter-library references to `@loader_path`, and applies ad-hoc signatures after rewriting. Probes and opt-in native builds copy ANGLE libraries beside the consumer and carry relative runtime search paths. Production distribution/signing remains a later package gate.

## Build and run

Use a native host for the selected RID. Requirements: Python 3.11+, Git, CMake 3.22+, Ninja and the platform development SDK. Windows needs an x64 Visual Studio development environment and Windows SDK. macOS needs Xcode and its Metal Toolchain (`xcodebuild -downloadComponent MetalToolchain` if absent). Linux needs the Vulkan driver/loader and the development prerequisites from the pinned ANGLE build documentation. The builder does not install system packages or GPU drivers.

From the repository root, on macOS ARM64 (substitute `win-x64` or `linux-x64` on those hosts):

```sh
python3 eng/graphics/build.py dawn --rid osx-arm64 --jobs 6
python3 eng/graphics/build.py angle --rid osx-arm64 --jobs 6
cmake -S eng/graphics/probes -B artifacts/graphics-probes -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBSCENE_GRAPHICS_SDK_ROOT="$PWD/artifacts/graphics-sdk/osx-arm64"
cmake --build artifacts/graphics-probes --parallel 6
python3 eng/graphics/run-probes.py --rid osx-arm64 --sdk artifacts/graphics-sdk/osx-arm64 --probes artifacts/graphics-probes --output artifacts/graphics-evidence/native-probes.json
```

Use `python` and an absolute path in PowerShell. `--sources`, `--build` and `--sdk` permit task-specific dependency caches. Sources are fetched at the lock, not the default branch. Tracked local source modifications are rejected. Both checkout and transitive gclient checkouts use LF line endings.

Both Dawn and ANGLE honor `--build`. To verify clean compilation while retaining the verified source checkout, select new `--build` and `--sdk` directories. ANGLE's GN output may live outside its source checkout; no existing build objects need to be deleted or reused.

Each probe clears an RGBA texture and verifies all 68 pixels, including padded texture-copy rows for Dawn. ANGLE runs both ES 2 and ES 3 with WebGL compatibility and robust resource initialization. It requests the selected native hardware backend; Linux Vulkan additionally inspects the physical device type. Results distinguish `passed`, `failed` and `unavailable` (process codes 0, 1 and 77). The evidence runner rejects inconsistent process/JSON results, software/unknown hardware, missing adapter information and incomplete pixel checks. Declared Avalonia/Uno/Skia versions are repository target metadata; these native probes do not exercise framework composition.

**Readback is confined to these diagnostic executables.** No per-frame CPU readback/upload path, managed draw-call bridge or GPU canvas ownership implementation is introduced.

For a separately built GL option, use `build.py angle --angle-gl`, configure `WEBSCENE_GRAPHICS_ANGLE_VARIANT=angle-gl` and `WEBSCENE_ANGLE_PROBE_BACKEND=gl` in a separate probe build. That probe can check pixels, but presently reports hardware qualification unavailable until native GL adapter classification is implemented. It cannot substitute for the three selected backend gates.

## Opt-in native engine integration

Normal native builds retain `WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS=OFF` and do not require graphics SDKs. To validate linkage without building V8:

```sh
cmake -S experiments/WebScene.NativeEngine.Probe -B artifacts/native-graphics -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=OFF -DWEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS=ON -DWEBSCENE_GRAPHICS_SDK_ROOT="$PWD/artifacts/graphics-sdk/osx-arm64"
cmake --build artifacts/native-graphics --parallel 6
ctest --test-dir artifacts/native-graphics --output-on-failure
```

This checks the existing parser tests and native library linkage. V8-enabled native runtime builds, wrappers and NuGet distribution still require their own integration/qualification. Creating a GPU device remains an explicit probe action; no device is created by these CMake changes.

The runtime wrappers accept `--graphics-sdk /absolute/path/to/graphics-sdk/<rid>` (Bash) or `-GraphicsSdk` (PowerShell). Omission explicitly configures graphics OFF; enabled builds use a separate build-directory suffix. Before packing, `stage-runtime.py` verifies both SDK manifests and checks that the libraries beside the native engine match them. It stages Dawn/ANGLE libraries, the full SDK manifests, and license bytes with a map from original paths to short content-hash filenames. The generated package targets copy all graphics dependencies on build and publish and reject missing assets. This packages native prerequisites; it does not advertise WebGPU/WebGL browser support. Windows/Linux package and driver/compiler dependency qualification remain required.

After building the native engine in separate `artifacts/graphics-build/native-enabled` and `native-disabled` directories (toggle `WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS`), reproduce the SDK boundary and macOS loader checks with:

```sh
python3 eng/graphics/check-sdk-integrity.py --sdk artifacts/graphics-sdk/osx-arm64/dawn --rid osx-arm64 --output artifacts/graphics-evidence/sdk-integrity.json
python3 eng/graphics/check-macos-relocation.py --sdk artifacts/graphics-sdk/osx-arm64 --probes artifacts/graphics-probes --native-enabled artifacts/graphics-build/native-enabled --native-disabled artifacts/graphics-build/native-disabled --output artifacts/graphics-evidence/relocation.json
```

The SDK check mutates disposable copies to reject a wrong revision, RID, lock, build settings, generated header, unexpected header, library and cached foreign `Dawn_DIR`. The macOS check runs relocated binaries with an unrelated current directory and checks dyld's actual ANGLE load paths; use a Python installation that permits `DYLD_PRINT_LIBRARIES` (the local verification used Homebrew Python).

## Fixture, suites and coverage

The unchanged Kestrel archive and license are committed under `tests/GraphicsCompatibility/fixtures`; see that directory's README for checksum verification and disposable extraction. Its historical GPU results and screenshots are not qualification evidence.

```sh
python3 eng/graphics/sync-suites.py webgpu-cts
python3 eng/graphics/sync-suites.py webgl-cts
python3 eng/graphics/sync-suites.py wpt
```

These commands acquire full immutable upstream checkouts with their licenses. Acquisition reports explicitly say `conformanceStatus: not-run`. `coverage.json` tracks core API families, optional features, source and worker dependencies, downstream issue ownership and harness gaps. It is a family-level inventory; member-level CTS/WPT mappings and individual optional-feature coverage remain work for the corresponding binding/conformance issues.

## Hardware evidence and outstanding acceptance

`.github/workflows/graphics-sdk-build.yml` builds the pinned Dawn/ANGLE SDKs and links their diagnostic probes on hosted Windows and Linux runners. It deliberately does not run GPU probes or count successful compilation as hardware qualification. Its SDK artifacts are retained for 14 days for inspection. The separate dedicated-runner workflow below owns hardware execution.

`.github/workflows/graphics-prerequisites.yml` is manually dispatched on dedicated `self-hosted`, `webscene-gpu`, `<rid>` runners. An administrator must enroll suitable hardware and provision the platform tools, an interactive desktop and Chrome (`CHROME_BIN` may specify its executable). Windows initialization selects the installed x64 Visual C++ developer environment for subsequent Ninja builds. The workflow is an enrollment contract, not proof of available runners; this account could not enumerate repository runners (HTTP 403). It preserves evidence on failure and treats unavailable hardware as non-success. It also uploads the full Chrome reference PNG/trace set with 90-day artifact retention; copy accepted baselines to permanent project storage before expiration. Ordinary CI checks fixture integrity and evidence-validation behavior only.

Current local macOS evidence covers both native backend probes, graphics-enabled/disabled V8-free parser builds, native/probe relocation, SDK mismatch rejection and 32 repeated hardware-Chrome courtyard/fixture/seeded 10k/100k reference runs. Remaining #23 gates include Windows/Linux builds and hardware runs, native V8 runtime/package integration, independently qualified GL where needed, complete durable reference artifact storage, and the complete non-GPU baseline. No conformance suite or unchanged Kestrel run in WebScene has passed yet. Keep #23 open and do not begin #24 until all #23 acceptance evidence is complete.

Upstream build references: [Dawn CMake at the pin](https://github.com/google/dawn/blob/2ca8cbfe0f8275aa0f739e7b6b4345a16e2f0378/docs/quickstart-cmake.md), [ANGLE setup at the pin](https://github.com/google/angle/blob/082d85ba19efba24d3c25108dc1f0cad9cf149f9/doc/DevSetup.md). The exact checked-out source and generated headers take precedence over examples from other revisions.
