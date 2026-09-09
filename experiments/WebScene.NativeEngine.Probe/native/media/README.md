# Media service foundation (#54 / #55)

This is the first implementation slice, not completed browser media support.
Enable `WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA=ON` to build the static media library
and its tests. Browser media/Web Audio interfaces are deliberately not exposed
until their lifecycle and actual playback implementations exist. The Frameforge
preview remains blocked; this change does not claim to fix its startup.

## Dependency decision

| Option | Fit | Decision |
| --- | --- | --- |
| miniaudio 0.11.23 | Cross-platform C audio decoding/output, MIT-0/public-domain choice, compiled from one implementation unit | Adopt for audio; this slice enables decoding only. Device I/O and graph integration follow. |
| FFmpeg/libav | Cross-platform demux/decode, VideoToolbox/D3D11VA/VAAPI support; static builds supported, LGPL obligations and codec/configuration-dependent licensing | Credible video provider for all platforms; evaluate before adding Windows/Linux providers. Not bundled in this slice. |
| GStreamer | Cross-platform media pipelines, plugin/deployment and licensing configuration | Viable alternative; more deployment machinery than required for this initial decode seam. |
| Native OS video APIs | Direct native surfaces, system codecs, no extra shipped codec binaries; requires OS providers | First provider uses AVFoundation on macOS behind the common service. Windows/Linux currently fail honestly. |
| SDL audio/mixer | Portable audio facilities; not a general video demux/codec implementation | Does not remove the video-provider requirement. |

Miniaudio is pinned by commit and SHA256 in CMake, built as static object code
inside `webscene_media`, and accompanied by its license in the build directory.
The archive is cached by FetchContent. No additional miniaudio dylib is shipped.
AVFoundation/CoreVideo are system framework dependencies; they are not statically
redistributed. This is not a claim that the entire application is one executable;
existing WebScene/Dawn packaging remains unchanged. With browser consumers not
connected yet, release dead stripping can discard unused decoder symbols.

Primary references:
- https://miniaud.io/index.html
- https://miniaud.io/docs/manual/
- https://github.com/mackron/miniaudio/tree/f40cf03f80cdb7e741d43e53b7e706e8c1394bcf
- https://www.ffmpeg.org/legal.html
- https://ffmpeg.org/ffmpeg.html
- https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html
- https://developer.apple.com/documentation/avfoundation/avplayeritemvideooutput
- https://wiki.libsdl.org/SDL3/Libraries

## Implemented

- Shared byte-source, immutable decoded PCM and retained native video frame
  contracts. Sources contain host-admitted bytes, not decoder-fetchable URLs.
- Bounded background job queue with success/error futures, cancellation during
  shutdown and settlement of pending work. No V8, UI or platform window dependency.
- Miniaudio WAV/MP3/FLAC-capable PCM decoding with input/output memory limits,
  channel validation and cancellation checks. Current checked fixtures cover WAV;
  MP3/FLAC codec qualification remains outstanding. AAC is not provided by this
  decoder configuration and must not be advertised.
- macOS AVAssetReader frame decoding/seek into retained IOSurface-backed
  CVPixelBuffers, dimensions, presentation timestamp, asset duration and display
  transform. Frames survive destruction of the reader, spool and worker service.
- Cross-platform PCM/queue tests and macOS actual H.264 decode tests. CI enables
  the foundation for Windows, Linux and macOS; only macOS can qualify video yet.

The macOS provider currently creates an asset reader and private spool for each
requested frame. This is a correctness/decode seam, **not a playback scheduler**.
Before playback, introduce a persistent media session with reusable source/reader,
metadata loading, seek generations and bounded decode-ahead. Never invoke this
one-shot function for every presentation frame. Large-source streaming and
interruptible async metadata loading also remain to implement.

## No airspace / rendering contract

No native child video view, AVPlayerLayer, HWND overlay or separate compositor
window is permitted. Video contributes an ordinary image/texture to WebScene's
retained scene; HTML, Canvas2D and GPU content must share stacking, clipping,
transforms, opacity and scroll behavior. The same frame source must serve direct
video element painting and GPUDevice.importExternalTexture (#40).

The tagged native-surface lease carries a particular decoded frame, independent
of DOM/JS object lifetime. Consumers retain it until GPU completion. Replacement
or resize may supersede pending presentation but cannot mutate an already leased
frame. Integrate with the existing mailbox/vsync and versioned GPU image system;
do not add a second UI/render loop or synchronously wait on decode from the UI.

Tests read pixels only to verify actual decode. Production decode does not copy
pixel buffers into CPU arrays. BGRA output can involve platform conversion: this
is not a claim of zero-copy decoding. Future native-plane import/conversion and
copy counters must distinguish decode, GPU conversion, upload and explicit export
readback. Frame color attachments are retained with the native buffer; consumers
must honor them and the display transform rather than assume identity/sRGB.

## Next implementation slices

1. Persistent video/audio media sessions, metadata and playback clock, resource
   streaming, generation-safe seek/load cancellation and native frame callbacks.
2. Generated HTMLMediaElement/Video/Audio bindings and standards-based events,
   play promises, real readiness, errors and source/DOM disposal; WPT contracts.
3. Frame leases integrated into GPU composition (#40), with overlap/clip/opacity,
   resize, device-loss and delayed-consumer tests. No overlay workaround.
4. Shared realtime audio graph/output service using miniaudio, resampling, gain
   automation, analyser, media-element input and AudioBuffer/AudioContext bindings.
   Keep V8/memory allocation/disk I/O off device callbacks. Add numeric signal tests.
5. Native AOT Frameforge playback/seek/audio acceptance. Neither #54 nor #55 may
   be closed based on the foundation tests alone.

## Local verification

```
cmake -S experiments/WebScene.NativeEngine.Probe -B artifacts/checkpoint-native \
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA=ON
cmake --build artifacts/checkpoint-native --target webscene_media_decode_tests
ctest --test-dir artifacts/checkpoint-native -R webscene_media_decode_tests --output-on-failure
artifacts/checkpoint-native/native/media/webscene_media_decode_tests \
  artifacts/Frameforge/assets/weightless.wav artifacts/Frameforge/assets/earthrise.mp4
```

Original Frameforge revision f414cd44896b659a5c6da2e4fc596a2ded055f2a:
576000 stereo PCM frames at 24000 Hz; 960x540 video frames at 0 and 1 second
with different pixel contents and retained IOSurfaces after decoder teardown.
The test also covers exact synthetic PCM values, invalid input, memory limits,
cancellation and pending-job settlement. No audio device is opened by tests.
