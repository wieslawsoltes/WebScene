# Hosted Linux Dawn build evidence

GitHub Actions [job 101724950691](https://github.com/wieslawsoltes/WebScene/actions/runs/34116313032/job/101724950691) succeeded at revision 1ef6394dbd6f2f6a06ad65f06d4bdfb421a168ee on ubuntu-22.04. The job built the pinned Vulkan Dawn SDK, audited its exported C API, verified its installed inventory, and configured and linked the native probe. It did **not** execute the probe or qualify GPU hardware, native runtime integration, or relocation.

The compressed full build log, SDK manifest (including every installed file hash and tool versions), CMake cache, source graph and export audit are retained here. Full SDK binaries are in Actions artifact `graphics-build-only-dawn-linux-x64` (artifact ID 10017202937; 14-day retention), and downloaded locally under `artifacts/graphics-hosted-linux-dawn-34116313032`. Artifact retention is not permanent binary archival.

The downloaded artifact passed the current verifier with:

```sh
python3 eng/graphics/verify-sdk.py artifacts/graphics-hosted-linux-dawn-34116313032/graphics-sdk/linux-x64/dawn --component dawn --rid linux-x64
```

Reproduce compilation on Linux x64 from this checkout:

```sh
python3 eng/graphics/build.py --component dawn --rid linux-x64 --jobs 2
python3 eng/graphics/verify-sdk.py artifacts/graphics-sdk/linux-x64/dawn --component dawn --rid linux-x64
```

See `.github/workflows/graphics-sdk-build.yml` for system packages and probe link commands. Hardware pixel verification remains required by issue #23.
