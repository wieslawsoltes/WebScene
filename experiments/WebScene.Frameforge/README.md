# Frameforge Native AOT compatibility preview

Runs unmodified Frameforge assets at upstream commit
`f414cd44896b659a5c6da2e4fc596a2ded055f2a` using WebScene and the sample-only
Avalonia 12 opt-in. Production WebScene retains Avalonia 11 defaults.

This is not a working video editor yet. The WebGPU compositor initializes and
loads the seven-clip demo project, but HTMLVideoElement decoding, Web Audio and
GPUDevice.importExternalTexture are missing. IndexedDB persistence and
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

Next compatibility work must implement real media decode/seek/playback events,
frame lifetime and GPU external textures, audio graph/synchronization, and export
with platform regression coverage. No application shims simulate these APIs.
