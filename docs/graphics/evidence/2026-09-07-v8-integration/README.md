# G01 V8 integration attempt: failed qualification

Both graphics-enabled and graphics-disabled Release builds linked successfully with an existing local V8 15.3.10 SDK, generated bootstrap snapshots, and passed the three native parser suites. The native engine test crashed in both configurations: SIGSEGV with graphics enabled and SIGBUS with graphics disabled. These results do not qualify either runtime configuration. Graphics linkage is not necessary to trigger the failure; its root cause has not been established.

`result.json` records the repository/V8 revisions, input hashes and complete WebScene CMake settings for each build. The existing SDK was consumed read-only from a neighboring checkout. Its inspector header/source patches were already present. This is not evidence of a clean V8 SDK build, and should not be substituted for one. Its `args.gn` is retained alongside configure/build/test logs.

The macOS crash report for the enabled run identifies an invalid address in `mfm_free` on a native test thread. VS Code DebugMCP tools were unavailable. LLDB could print its version, but attempts to launch a debug session exited 137 without output, so no debugger root-cause finding is claimed. Do not suppress the native test or count the successful build as a runtime pass.

Reproduction uses `experiments/WebScene.NativeEngine.Probe` with the settings in `result.json`, separate new build directories, and `WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS` ON/OFF respectively. Substitute a local V8 SDK matching the recorded inputs and a verified graphics SDK; no author-specific path is required by the implementation. After configure:

```sh
cmake --build <build-directory> --parallel 6
cmake -E copy_if_different <v8-output>/icudtl.dat <build-directory>/icudtl.dat
ctest --test-dir <build-directory> --output-on-failure
```

Next verification must establish the V8 SDK/build compatibility and investigate the failing runtime test in both configurations. The earlier hosted CI package successes use their own SDK builds and do not establish that this reused local SDK is valid. Issue #23 remains incomplete.

## Follow-up: inspector ABI mismatch identified

Symbolicating a graphics-disabled RelWithDebInfo reproduction located the failure in `shutdown_inspector()` at the call to `allAsyncTasksCanceled()`, dispatching into `V8StackTraceId` construction. The reused archive predates the patched inspector header: the header inserts the virtual `consoleAPICalled` method, but the archive does not define `V8InspectorImpl::consoleAPICalled`. This mismatches the vtable layout. `symbolized-control-crash.txt` preserves the call path.

Native CMake now checks both the header declaration and the archive's defined implementation symbol when Inspector is enabled. It prefers the SDK's LLVM symbol reader for matching ThinLTO support, otherwise using the platform symbol tool. The actual stale archive fails configure with the explicit ABI diagnostic in `stale-sdk-rejection.log`. A newer local V8 15.3.10 archive containing the implementation passes the same check and links successfully. Matching-SDK runtime verification is still in progress; absence of the earlier immediate crash alone is not a suite pass. The original failed results above remain historical evidence.

The matching-SDK graphics-enabled test subsequently reported `binary invocation did not complete`. A live process sample located the main thread in the component-catalog test failure path, waiting during process shutdown. The test process was explicitly terminated after sampling (117.81 seconds elapsed); its CTest result is a failure, not a timeout pass. `v8-matching-sdk-tests.log` and `v8-matching-sdk-sample.txt` preserve this separate remaining failure. The immediate inspector ABI crash is addressed by rejecting the invalid SDK; runtime qualification still requires resolving this later failure.

## Upstream-aligned control

At merge commit `2de32f0` (upstream `96088f6`), the matching-SDK graphics-disabled build passes all four CTest suites, including the native runtime suite (12.52 seconds). The graphics-enabled configuration still reports an incomplete binary invocation and reaches the explicit 60-second CTest timeout. Both logs are retained. This comparison narrows the remaining failure to the graphics-enabled configuration; the earlier stale-SDK and old-runtime failures must not be conflated with it.

Both Dawn and V8 archives define Abseil spin-lock symbols without an inline version namespace. Their implementations differ. A shared-Dawn experiment is in progress to test symbol isolation; this is a hypothesis, not a confirmed fix or a qualified dependency change.

## Isolated shared-Dawn experiment

The disposable build was relinked against a shared Dawn monolith exporting only `_wgpu*`, retaining ANGLE and the same native sources/V8 SDK. All four native suites passed; the native runtime suite took 11.82 seconds. The preceding static-Dawn build timed out and the no-graphics control passed. This intervention supports a dependency symbol collision: Dawn and V8 bundle different unversioned Abseil implementations, and linking them into one image permits one library's implementation to satisfy the other's references.

This is an experiment, not the committed SDK configuration. Dawn's existing build directory was reconfigured with `DAWN_BUILD_MONOLITHIC_LIBRARY=SHARED` and `CMAKE_SHARED_LINKER_FLAGS=-Wl,-exported_symbols_list,<file>` where the file contains `_wgpu*`. In the disposable native `build.ninja`, only the installed Dawn archive path was replaced by that shared candidate path, and the candidate library was copied alongside the native engine. After the run the original Ninja file was restored. The installed, manifest-verified static SDK was not changed. The native binary in that disposable directory remains the experimental shared build until rebuilt.

The selected fix must now be integrated into the SDK builder and verified for exported-symbol isolation, native linkage, hardware probes and relocation. Windows/Linux qualification and production package changes remain outstanding. No CPU pixel presentation path is involved in this change.
