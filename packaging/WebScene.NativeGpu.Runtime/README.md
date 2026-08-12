# WebScene.NativeGpu.Runtime

This optional RID package carries WebScene's ABI 2 GPU provider. It is kept
separate from `WebScene.NativeEngine.Runtime` so applications that do not use
WebGPU do not ship Dawn, Tint, or platform GPU bridge code. This package does
not contain WebGL or ANGLE.

The first package template is `WebScene.NativeGpu.Runtime.osx-arm64`. Its
`libwebscene_native_gpu.dylib` is copied beside the native engine and discovered
automatically by `NativeWebSceneView`. Set `NativeGpuLibraryPath` only when the
provider is not adjacent to the engine.

Build the provider and package from the repository root with:

```sh
scripts/build-native-gpu-runtime.sh --rid osx-arm64
```

The command checks out the exact Dawn/Tint revision recorded in
`third-party/dawn.version.json`, enables only the Metal backend, links the
monolithic Dawn library into the sidecar, runs its live adapter/IOSurface smoke
test, records the revision in the NuGet runtime manifest, and packages the Dawn,
Abseil, and WebGPU-Headers license texts used by the statically linked binary.

The matching native-engine `--webgpu` build also runs a Dawn-backed WGSL
triangle, a pinned Three.js r184 `WebGPURenderer` scene, and the curated WebGPU
CTS rendering/readback subset before packaging the engine.

The provider may advertise `WebGpu` only when its Metal adapter and IOSurface
external-texture path initialize successfully. There is intentionally no CPU
readback or software fallback. A load that requires an unavailable GPU
capability throws `WebSceneBackendCapabilityException` before document scripts
run. Provider advertisement is necessary but not sufficient: the core engine
must also report its WebGPU binding build feature.
