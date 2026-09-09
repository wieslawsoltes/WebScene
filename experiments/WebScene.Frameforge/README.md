# Frameforge Native AOT compatibility preview

Runs unmodified Frameforge assets at upstream commit
`f414cd44896b659a5c6da2e4fc596a2ded055f2a` using WebScene and the sample-only
Avalonia 12 opt-in. Production WebScene retains Avalonia 11 defaults.

This is not a working video editor yet. The WebGPU compositor initializes and
loads the seven-clip demo project. Native HTML media decoding, retained video
composition and Web Audio now pass the separate `--media-verify` AOT contract.
GPUDevice.importExternalTexture and GPUQueue.copyExternalImageToTexture remain
tracked in #40. IndexedDB persistence and
MediaRecorder export are also unavailable. Do not interpret successful AOT
compilation or a painted shell as playback acceptance.

## Build

```sh
git clone https://github.com/wieslawsoltes/Frameforge.git artifacts/Frameforge
git -C artifacts/Frameforge checkout f414cd44896b659a5c6da2e4fc596a2ded055f2a
dotnet publish experiments/WebScene.Frameforge -c Release -r osx-arm64 \
  -p:PublishAot=true -p:WebSceneAvalonia12Sample=true \
  -o artifacts/frameforge-aot/publish
python3 experiments/WebScene.Frameforge/package-macos.py \
  --publish artifacts/frameforge-aot/publish --source artifacts/Frameforge \
  --runtime /absolute/path/to/matching/native/runtime \
  --output /absolute/output/Frameforge-WebScene-AOT.app
```

Use a matching native engine, Dawn, ICU and generated snapshot pair in runtime.
The app includes its assets and a loopback server; no .NET or Node installation
is needed. The executable/process name is Frameforge. Packaging uses local
ad-hoc signing, not notarization.

`Contents/MacOS/Frameforge --verify` logs original startup status and fails when
required media capabilities are absent. It currently fails as expected. The AOT
publish passes the reachable production trim/AOT warning check.

For media/audio acceptance, publish with `-p:JsonSerializerIsReflectionEnabledByDefault=false`
and run:

```sh
python3 experiments/WebScene.Frameforge/verify-media.py \
  --executable artifacts/frameforge-aot/publish/Frameforge \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --assets artifacts/Frameforge
```

This verifies actual MIME/HEAD/range responses plus unchanged MediaEngine cut
preparation, playback/pause, native meter/track behavior and worker waveforms.
[Media design and coverage](../WebScene.NativeEngine.Probe/native/media/README.md)
describes bounds and platform scope. Windows/Linux parity is #61/#62; full editor
rendering, persistent storage, recording and release qualification remain in #53.
No application shims simulate these APIs.
