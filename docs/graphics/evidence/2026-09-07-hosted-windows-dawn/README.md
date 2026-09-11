# Hosted Windows Dawn build evidence

[Job 101733462964](https://github.com/wieslawsoltes/WebScene/actions/runs/34119103227/job/101733462964) passed at revision `0d815e98422244d2242965f219acf4d1de93f41c` on windows-2022. It compiled pinned Dawn with D3D12, installed and verified the SDK, audited DLL exports, and configured and linked the diagnostic probe. The CMake cache confirms `ABSL_MSVC_STATIC_RUNTIME=ON` and `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, resolving the previous CRT linkage failure.

This is build evidence only: no GPU probe execution, verified pixels, V8 integration, relocation, or Windows package qualification is claimed. Issue #23 remains incomplete.

The full build log, package inventory with file hashes and tool versions, source graph, CMake cache and C API export audit are retained compressed here. The full SDK is available in Actions artifact `graphics-build-only-dawn-win-x64` for 14 days and locally under `artifacts/graphics-hosted-windows-dawn-34119103227`. This is not permanent binary archival.

The downloaded SDK passed the current verifier for all 81 inventoried files:

```sh
python3 eng/graphics/verify-sdk.py artifacts/graphics-hosted-windows-dawn-34119103227/graphics-sdk/win-x64/dawn --component dawn --rid win-x64
```

Reproduce from an x64 Visual C++ developer shell with Python, CMake and Ninja installed:

```sh
python eng/graphics/build.py dawn --rid win-x64 --jobs 2
python eng/graphics/verify-sdk.py artifacts/graphics-sdk/win-x64/dawn --component dawn --rid win-x64
```

See `.github/workflows/graphics-sdk-build.yml` for the exact compiler initialization and probe configure/link commands. Dedicated hardware execution remains required.
