# Aureon Studio Native AOT host

This sample runs the original Aureon Studio browser assets through WebScene's
native V8/Dawn/Skia stack. It includes a loopback-only static asset server, so
Node.js and an installed .NET runtime are not required by the published app.
The process is named AureonStudio.

Source: https://github.com/wieslawsoltes/AureonStudio
commit `b2b01a55893bd6372a6d00759922025de51846a5`.
No application JavaScript or WGSL is patched.

## Build and verify

```sh
dotnet publish experiments/WebScene.AureonStudio -c Release -r osx-arm64 \
  -p:PublishAot=true -p:WebSceneAvalonia12Sample=true \
  -o artifacts/aureon-aot/publish
AUREON_ASSETS="$PWD/artifacts/AureonStudio" \
WEBSCENE_TEST_NATIVE_LIBRARY="$PWD/artifacts/checkpoint-native/libwebscene_native_engine.dylib" \
  artifacts/aureon-aot/publish/AureonStudio --verify
```

Use an upstream checkout at the pinned commit for AUREON_ASSETS. The native engine
must include the graphics SDK and these runtime changes. Omitting --verify leaves
the app open for interactive use.

Verification requires a populated raster scene and completed BVH, adds a box,
checks undo/redo and rebuilds, resizes the window, renders four progressive
path-traced samples, and reads back finite, non-uniform HDR values from the
original renderer. It fails for JS/GPU errors. The application-reported IndexedDB
recovery warning is counted separately and printed explicitly.

Avalonia 12 is confined to the opt-in sample configuration; production defaults
remain v11. The executable is Native AOT; JavaScript still runs in V8.

## Packaging

```sh
python3 experiments/WebScene.AureonStudio/package-macos.py \
  --publish artifacts/aureon-aot/publish --source artifacts/AureonStudio \
  --runtime artifacts/aureon-runtime \
  --output /absolute/output/AureonStudio-WebScene-AOT.app
```

The runtime directory must contain the rebuilt engine, matching Dawn dylib, ICU
data, bootstrap snapshot and graphics manifest. Copy both snapshot files from
the same native build as the engine: generated DOM bindings are part of their
compatibility fingerprint. The script creates an app and ZIP,
records the source revision, removes absolute development rpaths, and signs the
bundle locally. It does not notarize the app. Run the packaged executable with
--verify again to qualify dependency resolution.

## Scope and limits

Implemented for this workload: native JS modules/import.meta/dynamic imports,
dedicated background module workers, V8 structured clone/ArrayBuffer transfer,
secure-origin reporting, compute pipelines/passes/dispatch, native asynchronous
pipelines, samplers, automatic pipeline layouts, queue completion promises and
buffer/texture transfers. Ordinary GPU display continues through the existing
mailbox and shared-texture path; CPU readback is for export/verification only.

Regression coverage lives in the native graphics runtime tests and
tests/WebPlatformSubset/webscene-aureon-runtime-profile.json. These are bounded
contracts, not a claim of full Web Platform conformance. Worker MessagePort/shared
memory transfer, import attributes/import maps, and comprehensive worker
credentials/CORS behavior are not qualified.

Aureon's automatic recovery requires IndexedDB, which remains unsupported.
Save a scene file to retain edits. PNG export also depends on OffscreenCanvas
and is not qualified by the HDR test. Native file picker workflows, all production
tools/scene examples, physical 60fps and Windows/Linux parity need further
qualification. The macOS raster/editing/compute acceptance is the current scope.
