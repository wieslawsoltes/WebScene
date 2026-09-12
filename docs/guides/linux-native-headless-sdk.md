# Linux native headless SDK profile

The Linux x86_64 SDK is a Native-only producer profile. It installs `WebScene::Core`, `NativeWeb`, `SharedCSS`, `Compiler`, and optionally `WebGPU`. It does not build or package V8, the Runtime component, application scripts or runtime-loaded application HTML. The compiler/ABI pin is LLVM 22.1.1 with libc++. See `src/WebScene.Sdk/cmake/WebSceneLinuxSDK.cmake` and the checksum-pinned installer in `eng/sdk/install-linux-llvm.py`.

HTML/templates are still compiled to C++20 at build time. Applications explicitly select `CSS_BACKEND shared`; native CSS parsing is allowed. The Linux package exports relocatable imported targets and a platform marker so a macOS binary SDK cannot accidentally be consumed on Linux. Requesting unavailable Runtime components fails rather than substituting another host.

## Offscreen WebGPU

On Linux, `native_webgpu_surface` names the reusable `native_headless_webgpu_surface`. The macOS IOSurface and Windows DXGI implementations remain unchanged. The Linux surface uses pinned Dawn Vulkan textures and the existing immutable image-lease/pool contracts. It is an offscreen native authoring/presentation target, **not** a claim of Linux desktop dma-buf/opaque-FD interop, X11/Wayland composition or Avalonia/Uno GPU parity.

Three color allocations are admitted at most, under an explicit byte budget. A retained image remains usable across resize and after surface close; only idle slots can be reclaimed. Producer work retires from actual Dawn queue completion, independently of application frame pumping. Exhausted slots return an empty texture rather than blocking the owner thread or creating an unbounded queue. Adapter identity is exposed, unknown adapters are rejected, and software devices require explicit authorization (`WEBSCENE_HEADLESS_ALLOW_SOFTWARE=1`). CI additionally forces a fallback adapter and records it as software, never hardware qualification.

`capture_native_image` is an explicit native diagnostic/export API. The provider interface does not make CPU-only hosts link Dawn. Captures use bounded GPU-to-buffer copies, wait only on the explicit diagnostic operation, and retain the consumer until GPU copy completion even when mapping times out. Normal frame publication performs no CPU readback. Only top-left 8-bit sRGB images are currently composed by the paired AppScene headless PNG path; unsupported formats fail explicitly.

## Build and validation

```sh
python3 eng/sdk/install-linux-llvm.py "$HOME/.cache/webscene/llvm-22.1.1"
export WEBSCENE_LLVM_ROOT="$HOME/.cache/webscene/llvm-22.1.1"
cmake -S src/WebScene.Sdk -B build-sdk -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/src/WebScene.Sdk/cmake/WebSceneToolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$PWD/sdk" \
  -DWEBSCENE_SDK_WEBGPU=OFF
cmake --build build-sdk --parallel 3
cmake --install build-sdk
python3 eng/sdk/stage-linux-runtime.py --llvm "$WEBSCENE_LLVM_ROOT" --sdk sdk
cmake -S tests/Headless -B build-consumer -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/sdk" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/sdk/lib/cmake/WebScene/WebSceneToolchain.cmake" \
  -DHEADLESS_TEST_WEBGPU=OFF
cmake --build build-consumer --parallel 3
ctest --test-dir build-consumer --output-on-failure
```

For GPU qualification, first build the existing pinned Dawn Linux package with `eng/graphics/build.py dawn --rid linux-x64`, then set `WEBSCENE_SDK_WEBGPU=ON`, `WEBSCENE_GRAPHICS_SDK_ROOT=<graphics-sdk>/linux-x64`, and `HEADLESS_TEST_WEBGPU=ON`. Use the same LLVM/libc++ producer toolchain. Ubuntu 24.04 prerequisites and explicit software-Vulkan configuration are in the paired AppScene workflow.

The installed-consumer tests cover compiled shared-CSS document construction, template identity, native pointer/input interaction, theme layout, bounded frame ownership, idle producer progress, exact captured pixels, retained resize, capture budgets and repeated teardown. They also compile the existing native Kestrel GPU modules against the installed SDK and run the original pipeline, mesh/line pixel, scene invalidation, pending-frame and resize assertions. The only platform change in that original test is Vulkan adapter selection on Linux.

The paired AppScene PR supplies the control host, deterministic stepping, screenshots, native application samples, relocation/bundle auditing and end-to-end tests. This infrastructure does not complete the original JavaScript Kestrel migration. Software-Vulkan results do not establish hardware performance, full conformance, desktop external-memory sharing, media support, or browser/WebGL parity. Issue #46 and the parent graphics epic remain open until their separate acceptance gates are met.
