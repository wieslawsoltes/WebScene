# Linux ANGLE hosted build and incomplete artifact

[Job 101733463401](https://github.com/wieslawsoltes/WebScene/actions/runs/34119103227/job/101733463401) successfully built the pinned Vulkan ANGLE SDK, verified the installed SDK on Linux, and linked its diagnostic probe at revision `0d815e98422244d2242965f219acf4d1de93f41c`. No hardware execution is claimed.

Downloading `graphics-build-only-angle-linux-x64` and running the repository SDK verifier failed inventory validation: the upload omitted nine hidden `.clang-format` files. All delivered inventoried files match their recorded hashes, but the downloaded package is incomplete and is not an accepted SDK. The exact missing paths are recorded in `download-integrity.json`; original log, manifest, source graph and available GN arguments are retained compressed here.

Commit `055836e` enables hidden-file inclusion for the narrowly scoped SDK artifact upload. A new downloaded artifact must pass the complete verifier before this packaging failure is considered resolved. Do not repair this recorded failed artifact by copying local files into it.

Reproduce the complete build on Linux with:

```sh
python3 eng/graphics/build.py angle --rid linux-x64 --jobs 2
python3 eng/graphics/verify-sdk.py artifacts/graphics-sdk/linux-x64/angle --component angle --rid linux-x64
```

Full runner prerequisites and probe link commands are in `.github/workflows/graphics-sdk-build.yml`. GPU pixels, native runtime integration and relocation remain separate issue #23 gates.
