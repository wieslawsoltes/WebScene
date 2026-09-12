# Native Linux offscreen WebGPU and SDK

This adds the WebScene half of AppScene's selectable `headless` platform. It is a native-only Linux x86_64 SDK profile, not a new application host. AppScene owns input, scheduling and capture composition; WebScene owns native document/layout and the application WebGPU device and immutable image leases.

## Profiles and selection

`src/WebScene.Sdk` selects the Linux producer without enabling Objective-C++. It installs Core, NativeWeb, SharedCSS, Compiler and, by default, WebGPU. Runtime/V8 is not built or installed. `WEBSCENE_SDK_ENABLE_WEBGPU=OFF` produces the independent non-GPU native SDK. Native consumers compile HTML/templates at build time, explicitly select `CSS_BACKEND shared`, and link `WebScene::SharedCSS`. Function/data sections plus native link garbage collection exclude runtime HTML parser code; shared CSS parsing is allowed.

On Linux `native_webgpu_surface` selects `native_webgpu_offscreen_surface`. Existing macOS Metal/IOSurface and Windows D3D12 surface selection remains unchanged. `WEBSCENE_NATIVE_WEBGPU_OFFSCREEN` can explicitly select the offscreen surface at compile time, but only the Linux profile is qualified by this work.

## Ownership and completion

The offscreen backend uses the existing `dawn_canvas_images` bounded three-slot pool, submission status and immutable image lease contracts from the macOS/Windows foundation. An application obtains `current_texture()`, records/submits its Dawn work and calls `present()`. `retire_submitted` holds producer ownership through the queue's submitted-work completion, even if the application drops its snapshot. Completion uses Dawn's spontaneous callback mode rather than a UI frame timer. A snapshot resolves only after success. No early external-image or producer-wait capability is advertised.

Resize increments the allocation generation. Already accepted leases remain valid while new-size drawing is pending. Pool byte limits and consumers prevent reuse while an image is in use. Device mismatch, foreign surface-thread access, unsupported image format, failed submission, software selection policy and capture size limits are explicit failures. Surface use/destruction is owner-thread-only; retained image dependencies keep the originating device alive after the surface is gone.

## Captures are explicit readbacks

Normal `present()` and `process_events()` keep textures on the Dawn device and do not copy pixels to the CPU. `capture_offscreen_image()` is a separately requested diagnostic/export operation. It retains an image consumer through queue completion, copies BGRA8/RGBA8 into a 256-byte-row-aligned map buffer, and returns tightly packed RGBA8 with the original alpha convention. Mapping timeout/cancellation cannot prematurely release the GPU image consumer. A capture counter records completed readbacks.

AppScene may then raster-compose those captured textures with the same scene-command compositor used by its existing platform. This is an explicit screenshot operation, **not** zero-copy Linux desktop presentation. The capture may wait for GPU completion; ordinary frame processing does not perform queue-idle waits or readbacks.

## Backend policy

The Linux backend is Dawn Vulkan at the existing pinned Dawn/Tint revision `2ca8cbfe0f8275aa0f739e7b6b4345a16e2f0378`. No opportunistic dependency upgrade is required. By default non-hardware adapters are rejected. Automated environments must explicitly opt into software:

```sh
export WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER=1
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
```

Locate the actual ICD filename on the host; it is distribution-specific. Record `vulkaninfo --summary`, the SDK manifest and application output. Lavapipe/SwiftShader results establish offscreen software execution, never hardware performance or X11/Wayland compatibility.

## Build and validation

The paired AppScene `eng/sdk/build_headless.py` builds, installs, relocates and qualifies the combined SDK. Independently, the original native document/compiler suite can run without graphics:

```sh
cmake -G Ninja -S src/WebScene.NativeWeb -B artifacts/native-linux \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DWEBSCENE_NATIVE_WEB_GPU=OFF
cmake --build artifacts/native-linux --parallel 2
ctest --test-dir artifacts/native-linux --output-on-failure

cmake -G Ninja -S src/WebScene.Sdk -B artifacts/native-sdk \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Release -DWEBSCENE_SDK_ENABLE_WEBGPU=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/artifacts/sdk"
cmake --build artifacts/native-sdk --parallel 2
cmake --install artifacts/native-sdk
```

The Linux SDK records the producer's exact C++ compiler version and rejects a mismatched consumer. The current CI toolchain is Ubuntu Clang 18.1.3 and CMake 3.31.6, with the producer's libstdc++ ABI. It does not change the existing macOS LLVM 22.1.1 profile.

`tests/SDKLinux` is an independent installed-SDK consumer. It tests compiled shared-CSS UI, native input and actual offscreen GPU pixels; unaligned readback rows; bounded image reuse; retained images after resize; foreign-device and thread rejection; and leases surviving surface destruction. The paired AppScene tests additionally exercise the unchanged NativeWebGPU sample through its real native host and composed PNGs. No missing-adapter result is a pass or skip.

## Limits

This does not implement or qualify Linux external-memory FD/dma-buf sharing, Vulkan-to-EGL composition, an X11/Wayland window host, Windows/Linux video decoding, WebGL browser bindings, full CTS/WPT conformance, or hardware GPU timing. It does not qualify Kestrel's complete application behavior or complete its C++ migration. Those are separate requirements, including issue #46. No parent epic is closed by this offscreen SDK milestone.
