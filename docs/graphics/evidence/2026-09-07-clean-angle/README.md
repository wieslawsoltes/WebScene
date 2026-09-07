# Clean ANGLE and combined SDK hardware verification

ANGLE compiled all 1,322 build steps in a separate output directory and installed into the clean SDK root. The builder now honors `--build` for ANGLE as it already did for Dawn. GN configuration outside the source checkout was verified before this change; no previous compilation objects were reused.

The clean Dawn and ANGLE SDKs then passed all three Metal hardware probes on Apple M4: Dawn and ANGLE ES 2/ES 3, 68 verified pixels each. `native-probes.json.gz` includes both complete SDK manifests, hardware identity, executable hashes and strict adjacent-library verification. Logs retain the clean build and probe configure/link commands.

Reproduce with `python3 eng/graphics/build.py angle --rid osx-arm64 --jobs 6 --build <new-build-root> --sdk <new-sdk-root>`, using the same SDK root as the clean Dawn build. Configure a new `eng/graphics/probes` build with `WEBSCENE_GRAPHICS_COMPONENTS=dawn;angle` and that SDK root, build, and run `eng/graphics/run-probes.py` for `osx-arm64`.

This completes the local clean SDK compilation/probe check. Windows/Linux hosted builds and hardware gates remain outstanding; #23 is not complete.
