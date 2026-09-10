# Video presentation timing and remaining qualification

Research, implementation and qualification: 2026-09-10. Scheduling measurements
below describe selected frames, not independently measured physical scanout.

## Initial audit (before this change)

`media_platform.js.inc::tick` runs on JS RAF, reads the playback clock, then calls
native `seek`. `media_session` keeps the latest request and publishes a completed
frame. The macOS reader keeps one decoded lease and reads forward until its PTS
reaches the requested time. `drain_media` publishes completed frames immediately
through the retained GPU image pool. This is not a presentation-deadline selector.
Repeated requests can also re-publish the same decoded sample.

`playback_control::time` derives media time from a steady-clock epoch. The audio
graph has a sample counter, but video scheduling does not use an output-device
clock with measured latency. That distinction matters for physical A/V sync.

The existing retained image pool and GPU completion lifetime are useful: preserve
them. Native video overlays, CPU readback and a second independent swapchain would
break the intended scene integration and are not part of this design.

## Cadence and tearing are separate

For exact rates, 24fps on 120Hz repeats each frame five refreshes; 60fps on 120Hz
repeats twice; 30fps on 60Hz repeats twice. 24fps on fixed 60Hz needs alternating
2/3-refresh holds. Its resulting judder cannot be eliminated by scheduling alone.
An appropriate display rate can eliminate that mismatch; interpolation is a
separate feature that changes imagery and is outside this work.

Fractional rates (24000/1001, 30000/1001, 60000/1001), variable refresh and variable
frame rate require timestamp-based selection and bounded drift correction. Do not
round everything to 24/30/60 or globally slow the UI to the video frame rate.

Tearing means parts of different frames appear in one scanout. Preventing it
requires complete immutable images, producer completion, atomic scene selection
and compositor-synchronized presentation. Cadence selection alone cannot prove it.
A 60fps desktop recording can resample the compositor output; it is insufficient
to distinguish all physical scanout artifacts from capture artifacts.

## Implemented in this change

- Persistent decoder now fills a queue of up to four complete frames, constrained
  to 128 MiB of queued BGRA storage (at least one frame for oversized sources).
  Queue consumption, source replacement and explicit seek are synchronized;
  obsolete requests cannot publish. Sustained overload coalesces a catch-up seek.
- Normal playback no longer calls native seek from JS RAF. The native host frame
  opportunity selects by PTS coverage, retains the current image across repeats,
  and does not publish merely because a decode completed.
- Active media keeps host frame demand asserted while JS callbacks are admitted.
  The previous transient zero-demand window skipped otherwise available refreshes.
- Host timestamps map into the steady-clock domain. The median of recent refresh
  intervals supplies a midpoint for coverage selection, avoiding decisions on a
  jittery frame boundary. This is an estimated presentation interval, not DAC or
  physical scanout feedback.
- Device-rendered audio sample position anchors the media clock when available;
  stale/revision-mismatched feedback falls back to steady time. The device callback
  uses atomic feedback without allocation or locks. Actual DAC latency remains a
  qualification gap, so this is not a claim of sample-accurate physical A/V sync.
- The existing immutable IOSurface/image lease path remains intact: no CPU
  readback or video overlay. GPU image drawing now requests bilinear sampling on
  macOS and Windows, replacing nearest-neighbour enlargement. No mip chain or
  per-frame CPU resize is generated. Production Avalonia 11 and sample-only
  Avalonia 12 use their respective Skia sampling APIs.
- Responsive video height, auto block height and scroll extents follow decoded
  aspect ratio. Explicit CSS height remains authoritative.

## Local qualification

- Native scheduler simulations cover ten minutes for each pairing of 24, 30, 60,
  24000/1001 and 60000/1001 fps with 60, 120 and 60000/1001 Hz. VFR, obsolete
  frame dropping, starvation holds, audio clock anchoring and pause reset are tested.
- Real decoder regression verifies bounded decode-ahead, no publication on decode
  completion, timestamp selection and seek invalidation.
- AOT host trace with Big Buck Bunny on the current 60Hz setup includes 3,599
  consecutive one-refresh intervals over 59.98 seconds. An uninterrupted Sintel
  segment records 88 two-refresh and 88 three-refresh holds over 7.33 seconds.
  A subsequent run after the bilinear/AOT rebuild recorded four dropped decoded
  frames and one repeat over about 54 seconds while development/tests were also
  running. Smooth segments are not a guarantee of zero misses under load.
  Startup, manual seeks/source switches and loop boundaries are excluded from
  those steady-state segments. These are scheduler traces, not display feedback.
- `WEBSCENE_MEDIA_TRACE=1` logs selected PTS, mapped media time, selection/drop/
  repeat counters and reader restarts. Repeated unchanged images do not produce
  another selection line. The diagnostic is opt-in.
- All 18 native tests and the required macOS video contract pass. Managed sampling tests cover
  layout grow/shrink/scroll range, source replacement, opacity and Retina scaling.
  The Skia 3 branch also passes a standalone three-case pixel check and the
  Avalonia 12 Native AOT build. Running the complete existing xUnit project with
  the sample-only Avalonia 12 flag exposes an xUnit v2/v3 dependency conflict;
  that matrix is not claimed as passing. A 120Hz physical display and measured
  speaker-to-screen latency remain unqualified.

## Reproducing the sample comparison

Run `experiments/WebScene.Frameforge/prepare-video-cadence-samples.py --output
artifacts/media-cadence-samples` with Python 3, ffmpeg and ffprobe installed.
It obtains official Blender/Microsoft sources and writes codec/rate/hash metadata.
Copy the three generated MP4s into the AOT host's `Assets/assets` directory and
`media-cadence-demo.html` to its `media-demo.html`; launch with `--media-demo`.

Sintel is an original 1080p 24fps H.264/AAC trailer. BBB preserves its original
1080p 60fps H.264 frames for a 60-second excerpt; MP3 audio is converted to AAC.
The HEVC source is remuxed from hev1 to hvc1 without re-encoding. Compare each
**same file** at the same viewport size/display mode in Chrome and QuickTime.

## Pipeline design and further qualification

1. **Decode ahead independently of JS RAF.** Add bounded sequential frame delivery
   with PTS, duration, generation and immutable GPU lease. Start with a small queue
   constrained by both bytes and buffered duration; account for 4K surface costs.
   Seek flushes/reset generations; normal playback must not be implemented as a
   seek on every display callback. Decode and resource IO remain off render threads.
2. **Publish host presentation opportunities.** Carry a monotonic sequence,
   predicted presentation time, refresh interval and timing quality through the
   existing Avalonia/Uno vsync boundary. Never create a competing display loop.
   For macOS, evaluate the host's Metal display-link target presentation timestamp;
   keep the cross-platform interface usable by Windows/Linux timing providers.
   Where the host exposes only an estimated timestamp, label that limitation.
3. **Map the audio clock to host time.** Track played sample position, queued device
   latency, pause/rate changes and device resets. Use audio as master when audible
   playback exists; use a monotonic clock for silent media. Do not derive the video
   deadline from when a JS callback happened to run.
4. **Select a frame for each presentation interval.** Prefer stable cadence when
   source/display rates allow it; otherwise choose using timestamp coverage and
   drift bounds. Retain the last complete frame on decode starvation. Drop only
   obsolete frames, recording the reason. Reset on seek/discontinuity; use
   hysteresis for refresh changes. Inform the selector when a selected frame was
   superseded before submission, so selection counts do not masquerade as display.
5. **Commit with the scene.** Choose one immutable lease per video for the scene's
   presentation opportunity. Retain through GPU consumer completion, including
   resize/source replacement. Coalesce redundant PTS publications. Never mutate an
   image still in use; preserve ordinary clipping, scrolling, opacity and stacking.
6. **Instrument and qualify.** Record source PTS, chosen deadline, decode readiness,
   scene revision, GPU completion, actual presentation feedback when available,
   repeat/drop reason, audio position/latency and queue occupancy. Distinguish
   decode drops, scene supersession and missed display deadlines.

## Acceptance and tests

- Pure scheduler tests at 24/30/60 against 60/120Hz: expected integer/2:3 holds,
  monotonic PTS, no unintended skips under ideal delivery and bounded drift.
- Fractional and VFR fixtures, injected decode stalls, deadline jitter, refresh
  transitions, pause/seek/rate changes, looping and end-of-stream.
- At least ten minutes of synthetic clock simulation to detect accumulated drift;
  bounded queues under overload; no allocations/IO in the device callback.
- Contrasting numbered/barcoded frames validate one complete frame per submitted
  scene and GPU lease lifetime through delayed completion/resize/disposal.
- Timestamped flash/click measurements include actual audio output latency and
  presentation feedback, not just decoded timestamps.
- Compare Chrome and WebScene on the same monitor/mode and original sample.
  For physical tearing/cadence claims, use suitable high-speed external capture or
  trustworthy presentation instrumentation; document capture limitations.
- Re-run Native AOT tests and DOM layout/media contracts. Keep video cadence
  scheduling native; DOM event dispatch remains on the JS realm thread.

## References

- [Chromium VideoRendererAlgorithm](https://chromium.googlesource.com/chromium/src/+/HEAD/media/filters/video_renderer_algorithm.h): selects frames for a presentation interval and accounts for unusable or unrendered frames.
- [Chromium cadence estimator, pinned historical design](https://chromium.googlesource.com/chromium/src/+/30f71a78/media/filters/video_cadence_estimator.h): rational hold patterns, drift limits and cadence-change hysteresis.
- [Chromium media overview](https://www.chromium.org/developers/design-documents/video/): pipeline/thread boundaries; the page explicitly notes historical material, so current implementation must be checked before porting behavior.
- [Apple CAMetalDisplayLink targetPresentationTimestamp](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/update/targetpresentationtimestamp) and [preferredFrameRateRange](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/preferredframeraterange): platform timing inputs to evaluate through the existing host.
- [Microsoft clear HEVC/AAC test content](https://learn.microsoft.com/en-us/playready/advanced/testcontent/playready-3x-test-content): Tears of Steel 4K/24fps HEVC, stereo AAC. Local sample was remuxed from hev1 to hvc1 without re-encoding after the original packaging was rejected by the macOS decoder. This is not broad HEVC/container conformance.

- [Skia sampling defaults](https://api.skia.org/SkSamplingOptions_8h_source.html): default nearest sampling versus explicit linear filtering.
