# Cross-platform rendering and interop reuse review

Status: source review, not a working integration or acceptance pass. Recorded
2026-09-07 after the user questioned expansion of platform-specific code.
The epic's native Dawn/Tint, ANGLE and GPU-resident Skia requirements remain intact.
Further bespoke platform expansion should follow a demonstrated composition path.

## Findings from current code

WebScene's Avalonia project targets net8.0 and net10.0. Directory.Packages.props
pins SkiaSharp 2.88.9. NativeSceneDrawOperation.Render obtains Avalonia's
ISkiaSharpApiLeaseFeature; WebScene does not currently own that renderer's device.
The versioned native image leases exist, but this path does not compose them yet.
The new D3D12 helpers are preliminary, largely Windows-unverified infrastructure.
They are not evidence that the final presenter needs every helper.

ProGPU revision reviewed: `102e39e5088b462624da6296ff70a43ed2c5d8b4`.
Its [Dawn package](https://github.com/wieslawsoltes/ProGPU/blob/102e39e5088b462624da6296ff70a43ed2c5d8b4/src/ProGPU.Backend.Dawn/ProGPU.Backend.Dawn.csproj)
targets net10.0, references WebGPUSharp (0.5.5 in central dependencies) and
ProGPU.Backend. The latter includes Silk.NET WebGPU/wgpu-native and GLFW
windowing/input dependencies. Direct consumption therefore requires runtime,
packaging and net8 compatibility work, not just an additional namespace import.

Its [external texture contract](https://github.com/wieslawsoltes/ProGPU/blob/102e39e5088b462624da6296ff70a43ed2c5d8b4/src/ProGPU.Backend/ExternalGpuTextureInterop.cs)
abstracts IOSurface, DXGI, AHardwareBuffer and DMA-BUF. Its
[Dawn sharing implementation](https://github.com/wieslawsoltes/ProGPU/blob/102e39e5088b462624da6296ff70a43ed2c5d8b4/src/ProGPU.Backend.Dawn/DawnSharedTextureMemory.cs)
wraps the same native shared-memory and fence mechanisms as our helpers. Platform
interop exists in ProGPU too. Its SkiaSharp compatibility shim and WebGPU renderer
are a different integration choice from retaining our existing native Skia host.
No source review here establishes ProGPU's hardware or standards qualification.

Skia Graphite's [Dawn backend context](https://skia.googlesource.com/skia/+/refs/heads/main/include/gpu/graphite/dawn/DawnBackendContext.h)
accepts a caller-supplied Dawn instance, device and queue. Its
[Dawn backend texture API](https://skia.googlesource.com/skia/+/refs/heads/main/include/gpu/graphite/dawn/DawnGraphiteTypes.h)
can wrap a WGPUTexture. The backend texture itself does not retain that texture;
wrapping SkImage/SkSurface objects do. GPU completion still requires lease tracking.
These moving-main APIs must be pinned with a compatible Dawn revision before build.
This gives a concrete cross-platform route for internal WebScene composition, but
does not make an independent Avalonia/Uno Skia device consume the result automatically.

## Decision and bounded next implementation

Keep the native versioned lease ABI, Dawn execution service and ANGLE architecture.
Do not replace native Skia with ProGPU's compatibility shim as an incidental change.
Do not add more native platform adapters merely because a platform issue exists.
First test shared-device Skia Graphite composition against the actual ownership
boundary. Treat ProGPU as a reusable implementation candidate and reference;
direct adoption remains unproven due to its runtime and renderer dependencies.

The next spike must:

1. Pin compatible Skia Graphite and Dawn revisions and confirm an isolated build
   can use the same Dawn runtime as WebScene (no cross-library object pointers).
2. Create a Graphite context using WebScene's Dawn device/queue; wrap a retained
   canvas texture and compose it with ordinary Skia content, clip, opacity and
   transforms. Preserve the lease until GPU completion.
3. Verify output with diagnostic readback only in the test, and instrument the
   production composition path to establish no CPU pixel transfer. Exercise resize
   and delayed consumers; texture references alone do not authorize pool reuse.
4. Identify the host presentation API for both Avalonia and Uno. Prove whether
   their existing renderers can consume this result or require one external image
   adapter. Do not call an offscreen Graphite test end-to-end presentation.
5. Record which existing platform helpers are required, redundant or replaceable
   by a library. Integrate only the demonstrated boundary. Preserve public .NET
   target compatibility and avoid changing the application's renderer implicitly.

A JavaScript WebGPU triangle inside composed WebScene remains the useful visible
milestone, followed by full bindings, ANGLE fallback, unchanged Kestrel and the
remaining conformance/platform/performance gates. This review closes no sub-issue
and does not change the epic's completion criteria or numbered issue ordering.
