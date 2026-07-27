# WebScene R5 Avalonia samples

These are checked-in, runnable .NET projects over the shared packages in
`samples/components`:

| Project | What it demonstrates |
| --- | --- |
| `WebScene.Sdk.SampleCatalog` | Browse and mount all 12 R5 packages from one app. |
| `ComponentHost.Basic` | A single `WebSceneComponentHost` in a native window. |
| `Hybrid.ReactIslands` | A native shell with two isolated component instances and host capabilities. |
| `TypeScriptDesktop` | A component-owned application surface with settings and notification services. |

Run the complete catalog:

```bash
dotnet run --project samples/hosts/Avalonia/WebScene.Sdk.SampleCatalog
```

Or run a standalone product shape:

```bash
dotnet run --project samples/hosts/Avalonia/ComponentHost.Basic
dotnet run --project samples/hosts/Avalonia/Hybrid.ReactIslands
dotnet run --project samples/hosts/Avalonia/TypeScriptDesktop
```

The catalog is not a collection of blank placeholders. Each entry mounts a production
React/TypeScript bundle with visible state and controls. Hybrid React Islands creates
two live component hosts, Multi-instance Workstation creates four and supports peer
recycling, and Web-to-native Migration composes the React order list beside native
Avalonia editor controls.

To mount the complete catalog non-interactively and return a failing exit code if any
real V8/Avalonia mount fails:

```bash
scripts/run-r5-catalog-runtime-smoke.sh
```

The V8-backed hosts require WebScene's reviewed native ClearScript library. Ordinary
builds automatically resolve the current RID from the stable repository cache at
`artifacts/v8-native/runtimes/<rid>/native` and copy it into the application output.

On a fresh checkout, initialize the submodules and populate that cache once:

```bash
git submodule update --init --recursive
scripts/build-clearscript-v8-native.sh --rid osx-arm64 --download-v8
```

Use the RID for your platform (`osx-arm64`, `osx-x64`, `linux-x64`, or `linux-arm64`).
Windows native builds follow `third-party/clearscript-patches/README.md`.
`WEBSCENE_CLEARSCRIPT_NATIVE` and `WEBSCENE_CLEARSCRIPT_RID` remain explicit overrides
when testing a reviewed binary outside the repository cache.
