# Avalonia GPU host capability probe

Run `dotnet run --project experiments/WebScene.GpuHost.Probe` from the repository
root in a graphical desktop session. The probe briefly opens a window, queries the
actual compositor's GPU interop service, prints JSON and closes. Exit 0 means the
query completed with an interop object, not that any import or presentation works.
Exit 77 means no interop object; exit 1 means the query failed.

Observed on Apple M4, macOS 26.6.2, default Avalonia 11.3.4 platform selection:
interop available, device not lost, imageTypes empty, semaphoreTypes empty.
Therefore no external-handle route can be selected from this capability result.
Shared-context APIs or another explicitly supported host backend require separate
investigation. This does not contradict the standalone Dawn/Graphite GPU test.

The probe additionally queries Avalonia's public OpenGL texture-sharing feature.
On this host `canCreateSharedOpenGlContext` is true, despite the empty external
handle lists. This is the next candidate to exercise; no shared texture has yet
been drawn or presented by this capability probe.

The probe now exercises the shared-context route when available: create a 32x32
composition texture, attach it to an FBO, check completeness, clear via OpenGL,
flush, import and await CompositionDrawingSurface.UpdateAsync. On the M4 host it
reports `sharedTextureUpdateCompleted=true` and exits successfully. Imported image
disposal is awaited before texture/context teardown. The surface is not attached
to a visual and its snapshot pixels are not inspected, so `presentationVerified`
remains false. This is not yet a Dawn-to-host bridge test.

The updated surface is now attached to the window as a 128x128 composition surface
visual. The probe awaits RequestCommitAsync before detaching, then awaits a second
commit before teardown. M4 reports `visualCommitCompleted=true`. This proves the
visual changes were applied on the render thread, not that pixels were displayed;
`presentationVerified` remains false until an independent pixel observation exists.

Use `-- --inspect` to keep the attached visual alive for 30 seconds. A targeted
macOS window capture during this mode visibly confirms the blue shared-GL surface
on the left and untouched white background on the right. Evidence is stored at
`docs/graphics/evidence/avalonia-host/shared-gl-window.png`. This is visual evidence
for GL-to-Avalonia display, not a colorimetric pixel test or Dawn-to-host integration.
The runtime JSON keeps presentationVerified=false because the program itself does
not perform the independent window observation.

## Kestrel on Avalonia 12 (sample only)

WebScene's default build and published packages remain on Avalonia 11.3.4.
For the Kestrel host, opt into Avalonia 12.1.1 and its matching Skia dependencies
across the project-reference graph:

```sh
dotnet publish experiments/WebScene.GpuHost.Probe -c Release -r osx-arm64 \
  -p:PublishAot=true -p:WebSceneAvalonia12Sample=true \
  -o artifacts/kestrel-aot-avalonia12/publish
artifacts/kestrel-aot-avalonia12/publish/WebScene.GpuHost.Probe --aot-serialization-probe
WEBSCENE_TEST_NATIVE_LIBRARY="$PWD/artifacts/checkpoint-native/libwebscene_native_engine.dylib" \
  artifacts/kestrel-aot-avalonia12/publish/WebScene.GpuHost.Probe \
  --webgpu-metal --kestrel tests/GraphicsCompatibility/fixtures/Kestrel-CAD.zip \
  --continuous-resize-kestrel --resize-kestrel --verify-kestrel
```

The native engine path must refer to a graphics-enabled macOS build with its
Dawn runtime dependencies available. Omit the workload/verification switches
to leave Kestrel open for manual use.

Avalonia 12 includes the upstream Metal transactional presentation fix
([PR 21588](https://github.com/AvaloniaUI/Avalonia/pull/21588)).
This configuration does not patch Avalonia Native or change WebScene's
GPU mailbox/texture sharing. Packaging with this opt-in property is rejected.
The private Avalonia 11 Windows compositor-clock diagnostic is unavailable
in this configuration; the sample otherwise uses Avalonia's default render loop.

The continuous resize trace uses generated JSON metadata so it works in Native
AOT. Its geometry/intermediate-frame assertions do not establish physical
presentation cadence or native mouse-drag smoothness. Also check live edge/corner
resizing, sidebar resizing, pan/zoom, and display-scale transitions manually.
