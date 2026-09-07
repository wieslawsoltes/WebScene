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
