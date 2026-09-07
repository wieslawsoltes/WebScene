# Shared-device Graphite spike

Experimental source pin: Skia `0f366c36621fc156664662b8ea5426d2f41cefe1`.
WebScene Dawn SDK pin: `2ca8cbfe0f8275aa0f739e7b6b4345a16e2f0378`.
Skia's own DEPS instead pins Dawn `f45d1eb98a88b29d2ec38171613525cd5e54c0c0`.
Do not silently substitute that runtime or pass objects between the two builds.

The first compatibility check is compile-only:

```sh
clang++ -std=c++20 -fsyntax-only \
  -I artifacts/graphics-src/skia \
  -I artifacts/graphics-sdk/osx-arm64/dawn/include \
  eng/graphics/probes/graphite/dawn_header_check.cpp
```

Passed on macOS arm64 with the revisions above. This verifies declarations for
supplying the existing Dawn device/queue and wrapping a WGPUTexture. It does not
compile Graphite's implementation, verify ABI compatibility, link Skia, compose
pixels, or prove host presentation.

Skia's `third_party/dawn/BUILD.gn` normally builds a static `dawn_combined` library.
The next build step must isolate Graphite's dependency on the existing Dawn shared
runtime, including any internal Dawn/Tint dependencies. Determine whether the
selected Skia revision supports that arrangement before changing production SDK
pins. Then run the composition and lifetime tests in
`docs/graphics/cross-platform-reuse-review.md`.

Implementation compatibility check:

```sh
python3 eng/graphics/probes/graphite/check-backend.py \
  --skia artifacts/graphics-src/skia \
  --dawn-include artifacts/graphics-sdk/osx-arm64/dawn/include
```

All 15 Dawn backend `.cpp` files passed syntax compilation on macOS arm64 at the
pins above with SK_GRAPHITE and SK_DAWN enabled. This strengthens the header-only
result but still does not link Skia, verify its full build configuration, or execute
GPU composition. The source scan found no direct `dawn::native` or Tint includes
in that backend directory; Skia's separate developer shader tools do depend on Tint.
Full dependency synchronization was started for the isolated build.

## Isolated macOS build

```sh
python3 eng/graphics/probes/graphite/use-external-dawn.py \
  --skia artifacts/graphics-src/skia \
  --dawn-sdk artifacts/graphics-sdk/osx-arm64/dawn
artifacts/graphics-src/skia/bin/gn gen out/webscene-graphite \
  --root=artifacts/graphics-src/skia \
  --args="$(cat eng/graphics/probes/graphite/spike-args.gn)"
ninja -C out/webscene-graphite skia -j 8
```

The patch verifies the source pin and refuses unrelated edits. It redirects
Dawn headers/link metadata to the existing shared SDK and removes the dependency
on Skia's `dawn_cmake` action. The initial configuration deliberately excludes
optional codecs, text libraries, PDF and tools; those exclusions are for the
composition experiment and are not production capability decisions.

Result: all 797 build steps completed, producing `out/webscene-graphite/libskia.a`.
A static archive does not resolve its external symbols. Executable linkage to the
single Dawn dylib and an actual GPU composition test are the next required checks.
No runtime ABI, pixel correctness or host-presentation pass is claimed yet.
