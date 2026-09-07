# G01 graphics-enabled macOS runtime package

The opt-in runtime wrapper built and packaged the verified shared Dawn/ANGLE dependencies with the native V8 runtime. Its complete verification run passed four native suites, the existing required WPT subset (149 documents / 509 subtests), extracted-package runtime probes, and the clean package-consumer build. This subset is existing WebScene compatibility coverage, not WebGPU/WebGL conformance.

The package contains all three graphics libraries with matching SDK hashes, full SDK manifests, and unchanged license bytes mapped from all 1,010 original license paths. Content-hash license filenames avoid path-length problems (maximum package entry length: 92 characters). Publish output contains matching libraries and loads native ABI 3. Removing Dawn from the disposable consumer's package cache caused the expected missing-asset build error; the file was restored afterward. Staging also rejected a mismatched adjacent Dawn library before creating output.

Reproduction command (substitute matching local SDK paths):

```sh
bash scripts/build-native-engine-runtime.sh --rid osx-arm64 \
  --v8-root /path/to/patched-v8-15.3.10 \
  --graphics-sdk /path/to/graphics-sdk/osx-arm64 \
  --output /new/local/package-directory --package-version 1.0.33-gpu-g01.3
```

The recorded run used the matching `native-engine-v8-latest` SDK identified in the previous integration investigation. Logs preserve the full build and consumer paths. `package-verification.json` records the implementation commit, package hash and all package entry hashes; `graphics-runtime.json` records dependency revisions and license provenance. The local experimental package is in `artifacts/graphics-runtime-packages-03` and was not published to a feed.

Earlier attempts exposed duplicated license paths and writes over a deduplicated read-only license file; both were corrected before the recorded successful run. An initial running shell script also failed after it was edited in place; the final complete run used stable script contents. No failed attempt is counted as successful.

Windows/Linux graphics package builds, their GPU/driver/compiler dependencies, and hardware qualification remain incomplete. #23 stays open; this package contains graphics prerequisites without claiming browser API support or Kestrel execution inside WebScene.
