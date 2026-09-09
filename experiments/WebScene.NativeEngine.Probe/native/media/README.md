# Native media and Web Audio (#54 / #55)

WebScene now implements the Frameforge media-engine subset through native media
sessions and an actual audio graph. The unchanged `MediaEngine` runs in a macOS
Native AOT host. This is not full HTML media/Web Audio standards conformance and
is not completion of the Frameforge editor epic: WebGPU external textures and
external image copies remain #40, Canvas2D export remains #34, and recording/
full editor qualification remain #58/#59.

`WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA` defaults to ON. Runtime packaging scripts
set it explicitly. OFF remains available for a deliberately media-free build.
The browser interfaces then are absent, rather than successful placeholders.
Production Avalonia 11 remains supported; Frameforge can opt into Avalonia 12.

## Architecture and ownership

- The existing host loader admits bytes. `WebSceneTextResource.BinaryContent`
  preserves binary data through Avalonia and Uno, archives, HTTP, files, data
  resources and the native ABI. Fetch/Response/Blob retain byte fidelity and
  enforce body consumption/revocation. Decoders never fetch application URLs.
- HTML audio/video wrappers inherit the correct media/HTMLElement prototypes.
  A small native DOM media registry discovers markup/source changes at task
  boundaries, including parser-created nodes. It does not depend on the
  currently unsupported MutationObserver and does not traverse the full DOM.
- A persistent `media_session` worker owns the source and decoder. Load generations
  cancel stale work. Rapid seek requests coalesce; completed current-generation
  frames can still present while a newer request waits, avoiding starvation.
  Metadata/readiness/seek events are driven by actual decode results. Play
  promises reject on interruption/error. Media task events are generation scoped.
- Playback time is independent of presentation. Vsync requests the next video
  position; audio processes fixed quanta on its device callback. Source control
  changes anchor the audio cursor, which then advances continuously across
  callback/quantum boundaries. Gain automation uses the audio context clock.
- macOS retains decoded CVPixelBuffers and adopts their IOSurfaces into the
  **same versioned image lease and retained scene composition** used by WebGPU.
  No child window, AVPlayerLayer or overlay exists. No steady-state video readback
  or CPU pixel upload is used. Retaining the CVPixelBuffer prevents decoder pool
  reuse until scene/GPU consumers finish. Three image slots bound presentation;
  completion wakes retry the latest pending image after backpressure. Native
  allocation metadata and image contents stay immutable for existing consumers.
- `audio_graph` uses miniaudio for native device output, with a 128-frame requested
  period and three periods. Device negotiation can choose a different hardware
  period. The callback uses fixed storage; it does not invoke JS, allocate,
  lock a mutex, decode files or perform I/O. Stereo mixing, mono/quad/5.1 speaker
  conversion, interpolation/resampling, gain/target automation and time-domain
  metering are implemented for Frameforge. Explicit media source routing removes
  the default element output to avoid doubled sound.
- Each stream destination writes real stereo PCM to a bounded native capture
  ring. `audio_track` readers have independent cursors, clone/stop/enabled state,
  absolute frame timestamps and explicit overrun counts. Context close ends the
  producer. `resolve_audio_track` validates a private V8 brand and returns the
  native lease for recording adapters; no application-supplied numeric field is
  trusted. The encoder/MediaRecorder consumer belongs to #58.
- `decodeAudioData` detaches input, decodes off-thread, resamples to the context
  rate and returns actual planar PCM. The original Frameforge waveform worker
  transfers and reduces these arrays without application changes.

## Dependency decision and platform scope

| Option | Assessment / decision |
| --- | --- |
| miniaudio 0.11.23 | Adopted for portable WAV/MP3/FLAC decode and device I/O. Pinned commit `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`, verified archive SHA256, static compilation. MIT-0/public-domain license choice. |
| AVFoundation/AVAssetReader | Implemented macOS provider: persistent H.264 native video surfaces and container audio/AAC decoding; OS frameworks add no shipped codec dylib. |
| FFmpeg/libav | Credible portable video provider with hardware backends. Not bundled. Static distribution needs reviewed configuration/licenses and LGPL obligations. Evaluate for platform parity before adding a second codec stack. |
| GStreamer | Viable portable pipelines, with more plugin/deployment machinery. Not selected for this subset. |
| Native Windows/Linux providers | Common session/lease contracts isolate platform code. Windows can use Media Foundation/DXGI; Linux may use FFmpeg/VAAPI/dma-buf. Selection and hardware qualification tracked in #61/#62. |
| SDL audio/mixer | Does not replace general video demux/decode. No additional dependency adopted. |

Miniaudio is built into the native engine, not shipped as another DLL/dylib.
FetchContent caches the pinned source archive. Native NuGet packaging includes
`licenses/Miniaudio-LICENSE.txt` and rejects a media-enabled package missing it.
Existing engine/Dawn/ICU/snapshot packaging is unchanged; this is not a claim
that the whole application is one executable. AVFoundation/CoreVideo/IOSurface
are OS frameworks, not statically redistributed libraries.

Windows/Linux use the common miniaudio/audio graph code and portable CTests;
video support currently reports unsupported. Hardware qualification is tracked
explicitly in #61 and #62. Do not mark those platforms' video tests passed.

Primary sources used for the decision:
- https://miniaud.io/index.html
- https://miniaud.io/docs/manual/
- https://github.com/mackron/miniaudio/tree/f40cf03f80cdb7e741d43e53b7e706e8c1394bcf
- https://www.ffmpeg.org/legal.html
- https://ffmpeg.org/ffmpeg.html
- https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html
- https://developer.apple.com/documentation/avfoundation/avassetreader
- https://wiki.libsdl.org/SDL3/Libraries

## Bounds and subset limitations

32 media sessions; 256 MiB encoded source and decoded PCM limits per decode;
64 megapixel decode limit; 128 MiB retained image pool per video (oversize native
allocations fail explicitly). One pending seek/current decoded frame per session,
three presentation slots, eight queued standalone audio decodes. Audio contexts
are limited to eight, with 64 nodes, 1,024 pending graph commands, 32 outstanding
scheduled gain events per node and 512 MiB retained PCM per context. Capture
stores 16,384 stereo frames; lagging readers receive a discontinuity count.

The current admitted source is fully buffered; `preload` is a hint, not streaming
or a network-byte guarantee. Streaming/adaptive media, DRM, media controls UI,
negative native playback, frequency-domain analyser APIs, arbitrary AudioWorklet
processing and comprehensive Web Audio nodes are outside this Frameforge subset.
Video rotation/color-space qualification beyond the original unrotated sRGB demo
clips is not claimed. Stored display transforms are available to #40 consumers.
Device/display physical latency and long-duration A/V drift still require the
full playback qualification in #59; the included flash/click test measures
pipeline timestamps through the real mixer/capture renderer.

## Reproducible verification

Build the usual native V8 engine with media enabled, then:

```sh
cmake --build artifacts/checkpoint-native -j 6
ctest --test-dir artifacts/checkpoint-native --output-on-failure
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --manifest tests/WebPlatformSubset/webscene-media-runtime-profile.json \
  --selection required --native-library "$PWD/artifacts/checkpoint-native/libwebscene_native_engine.dylib" \
  --output artifacts/wpt-media-runtime
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --manifest tests/WebPlatformSubset/webscene-macos-video-runtime-profile.json \
  --selection required --native-library "$PWD/artifacts/checkpoint-native/libwebscene_native_engine.dylib" \
  --output artifacts/wpt-media-video
```

Both manifests are labeled project-owned contracts, not imported WPT conformance.
The video manifest is macOS-only. Native tests cover actual PCM, invalid data,
limits, cancellation/future settlement, source replacement, end/backward/rapid
seek, frame ownership through decoder/pool teardown, backpressure/completion,
gain/rate/seek/quantum continuity, capture timestamps/stereo/clone/stop/overrun,
and a generated H.264/AAC flash/click fixture. Its maximum measured pipeline
A/V difference was 0.0417 ms, with a 10 ms limit; physical output latency excluded.

Obtain the original public app without relying on an agent-local artifact:

```sh
git clone https://github.com/wieslawsoltes/Frameforge artifacts/Frameforge
git -C artifacts/Frameforge checkout f414cd44896b659a5c6da2e4fc596a2ded055f2a
dotnet publish experiments/WebScene.Frameforge -c Release -r osx-arm64 \
  -p:PublishAot=true -p:JsonSerializerIsReflectionEnabledByDefault=false \
  -p:WebSceneAvalonia12Sample=true -o artifacts/frameforge-media-aot/publish
FRAMEFORGE_ASSETS="$PWD/artifacts/Frameforge" \
WEBSCENE_TEST_NATIVE_LIBRARY="$PWD/artifacts/checkpoint-native/libwebscene_native_engine.dylib" \
  artifacts/frameforge-media-aot/publish/Frameforge --media-verify
```

`--media-verify` imports the original MediaEngine/core/worker modules. It prepares
nine cut positions, checks playback/pause/nonzero RMS, exercises volume/mute and
native track cloning, then compares the original worker's waveform against an
independent PCM reduction. The observed maximum waveform error was 1.49e-8.
The diagnostic page also displays an actual retained video under rounded clipping.
It is deliberately separate from full-editor `--verify`, which still requires
#40 and the remaining #53 issues. No original application source is patched.

The sample's `verify-media.py` also verifies the actual server's MIME, HEAD,
closed/open/suffix byte ranges and 416 responses against original file bytes.
A local native NuGet inventory check confirmed the exact pinned miniaudio license,
engine/Dawn/ICU/snapshot payloads and no extra miniaudio shared library.

Managed regression coverage includes raw binary file/resource envelope and archive
round trips. The Avalonia suite passes on net8.0/net10.0 (305 passed, 15 existing
skips per framework); Uno builds. The AOT media test passes with real decode and
audio device output. Existing unrelated interop library IL warnings may still be
emitted during compilation; media bindings add no reflection activation path.
