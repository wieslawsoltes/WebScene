# Clean Graphite output build

At WebScene commit f5b8de5, the build entry point completed all 797 Ninja steps
in a new out/webscene-graphite-clean-01 directory on macOS arm64. The sealed
Dawn SDK and previously synchronized Skia source/DEPS were reused. This is a
clean Graphite object build, not a fresh source acquisition or clean Dawn build.

Reproduction command:

```sh
python3 eng/graphics/probes/graphite/build-probe.py --skia artifacts/graphics-src/skia --dawn-sdk artifacts/graphics-sdk/osx-arm64/dawn --output out/webscene-graphite-clean-01 --jobs 8
```

build.log.gz records the complete target graph build. probe-build.json records
the compiler, commands, source input hashes and hashes of libskia.a and both
probe binaries. No binaries are committed here.

The resulting standalone executable ran with the pinned Dawn SDK on its loader
path and passed all 68 IOSurface/CGL pixels (pixels.json). The Avalonia probe ran
with the newly built host dylib directory first on DYLD_LIBRARY_PATH, using
--graphite --verify-markers. It completed 64 changing-marker checks and 64
host updates, with one device, one Graphite context, two canvas textures and one
output allocation (host.json). Both commands exited zero.

files.json seals the machine-readable evidence and compressed build log.
This does not qualify production WebScene integration, physical presentation,
other operating systems, source dependency acquisition or the full epic.
