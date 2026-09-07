# Clean shared Dawn build: macOS ARM64

A new `artifacts/graphics-clean-build` directory compiled all 932 build steps using the verified pinned source tree and the updated dependency lock. It installed to a separate `artifacts/graphics-clean-sdk` directory. No objects from the previous static/shared experiment were reused.

A new probe build using only this Dawn SDK passed the Metal hardware test on Apple M4, verifying all 68 pixels. Compressed configure/build logs, the SDK manifest and probe result are retained here. This is Dawn-only clean-build evidence; it does not qualify ANGLE or another RID.

Reproduce with `python3 eng/graphics/build.py dawn --rid osx-arm64 --jobs 6 --build <new-build-directory> --sdk <new-sdk-directory>`. Configure `eng/graphics/probes` in a new directory with `WEBSCENE_GRAPHICS_COMPONENTS=dawn` and `WEBSCENE_GRAPHICS_SDK_ROOT=<new-sdk-directory>/osx-arm64`, build, and run `webscene_dawn_probe metal`.
