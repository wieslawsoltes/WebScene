# Synthetic video fixture

`numbered-motion.mp4` is a two-second 64x48, 24 fps synthetic moving test pattern,
created for WebScene tests (no third-party footage). Generated with:

```
ffmpeg -f lavfi -i testsrc2=size=64x48:rate=24 -t 2 -c:v libx264 \
  -pix_fmt yuv420p -movflags +faststart numbered-motion.mp4
```

The name denotes a frame-ordered motion fixture; it has no burned-in timecode.
FFmpeg is a fixture-generation tool only, not a WebScene runtime dependency.
Tests decode different timestamps and verify changed pixels and native frame
ownership. Generated stereo PCM fixtures in the test verify exact channel values.

`flash-click.mp4` is a generated three-second 64x48/30fps H.264/AAC fixture: a white frame and 10ms/1kHz tone at each integer second, black/silence otherwise. It contains no third-party content. Native tests compare decoded frame timestamps with actual mixed/recorded PCM onset, with a 10ms tolerance; this measures pipeline timestamps, not physical speaker/display latency.
