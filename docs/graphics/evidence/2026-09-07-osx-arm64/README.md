# Partial G01 evidence — Apple M4, 2026-09-07

Code revision: `215bb60` (full SHA in `index.json`). These results qualify the recorded native prerequisite checks on this Mac only. **Epic #22 and issue #23 remain incomplete.** No browser API implementation, retained GPU presentation, Kestrel run in WebScene, or standards conformance pass is claimed.

| Check | Result |
|---|---|
| Dawn/Metal texture clear, queue submission, padded copy and mapping | Passed; 68 pixels checked |
| ANGLE/Metal ES 2 WebGL-compatible context | Passed; 68 pixels checked |
| ANGLE/Metal ES 3 WebGL-compatible context | Passed; 68 pixels checked |
| SDK relocation and mismatch rejection | 10 checks passed |
| macOS probe/native relocation, actual dyld paths | 5 checks passed |
| Graphics-enabled V8-free native parser tests | 3 passed |
| Graphics-disabled V8-free native parser tests | 3 passed |
| Official suite acquisition | All three locked checkouts acquired; tests not run |
| Windows/Linux hardware | Not run; runner access unconfirmed |

`native-probes.json` contains exact native package revisions, file hashes, transitive source graph fingerprints, CMake/GN settings, tool information, host/GPU/driver identity, executable hashes and probe output. The package manifests describe the SDK inputs; the framework version fields are declared repository versions, not evidence of framework composition. `index.json` hashes the accompanying raw evidence. Logs retain original local paths to preserve provenance.

Reproduction starts with [the graphics prerequisite guide](../../../../eng/graphics/README.md). The local build paths used here are `artifacts/graphics-build/probes-osx-arm64`, `artifacts/graphics-build/native-enabled`, `artifacts/graphics-build/native-disabled`, and `artifacts/graphics-sdk/osx-arm64`. The native builds use Release, V8 OFF, default html5ever/cssparser/Servo parsers, and graphics ON/OFF respectively.

Hardware/ABI checks:

```sh
python3 eng/graphics/run-probes.py --rid osx-arm64 --sdk artifacts/graphics-sdk/osx-arm64 --probes artifacts/graphics-build/probes-osx-arm64 --output artifacts/graphics-evidence/osx-arm64/native-probes.json
python3 eng/graphics/check-sdk-integrity.py --sdk artifacts/graphics-sdk/osx-arm64/dawn --rid osx-arm64 --output artifacts/graphics-evidence/osx-arm64/sdk-integrity.json
python3 eng/graphics/check-macos-relocation.py --sdk artifacts/graphics-sdk/osx-arm64 --probes artifacts/graphics-build/probes-osx-arm64 --native-enabled artifacts/graphics-build/native-enabled --native-disabled artifacts/graphics-build/native-disabled --output artifacts/graphics-evidence/osx-arm64/relocation.json
ctest --test-dir artifacts/graphics-build/native-enabled --output-on-failure
ctest --test-dir artifacts/graphics-build/native-disabled --output-on-failure
python3 -m unittest discover -s eng/graphics/tests -v
python3 tests/GraphicsCompatibility/prepare-kestrel.py
```

The retained-render and retained-apply JSON files are initial CPU-side non-GPU measurements using unchanged benchmark/renderer source at the base revision. Commands:

```sh
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- probe native-retained-render --layers 2048 --visible 32 --iterations 40 --samples 11
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release --no-build -- probe native-retained-apply --layers 4096 --batch 256 --iterations 100 --samples 11
```

Sparse rendering measured 12.93 µs median / 14.53 µs p95 with zero allocated bytes per render; reference pixels matched. These are CPU retained-scene microbenchmarks at their recorded 320×240 viewport, **not** GPU submission or actual presentation timings, and not the required 1920×1080 Kestrel/Chrome baseline. No performance regression threshold is established from a single before-only sample. Browser reference captures, full native/V8 workload baselines and repeatability remain outstanding.
