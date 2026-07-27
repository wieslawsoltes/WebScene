# ClearScript experimental patches

These files preserve small, reviewable experiments against the exact
ClearScript **7.5.1** tag. They are not applied by the normal build and no
patched native binary is committed.

`ClearScript-7.5.1-SharedContextSecurityToken.patch` gives every V8 context in
one `V8Runtime` the same V8 security token. This proved that WebScene's owner and
same-origin virtual-iframe contexts can exchange direct V8 objects while still
using distinct globals and intrinsics. Stock ClearScript rejects that access
with `TypeError: no access`.

`ClearScript-7.5.1-TypedManagedAbi.patch` adds the deliberately narrow v1 ABI
probe. The native library accepts typed managed function pointers and installs
numeric-property and DOM-rectangle functions directly in each V8 context. JavaScript
passes a stable integer DOM identity plus compact operation IDs; C++ either returns
the primitive `double` or gives .NET direct access to an eight-double V8 buffer,
without ClearScript's generic host-object/member/argument/result dispatch. Enable routing with
`WEBSCENE_ENABLE_V8_TYPED_MANAGED_ABI=1`. The normal path remains the control.

This ABI is experimental, process-wide, and intentionally limited to eight numeric
DOM properties plus bounding/client rectangles. It demonstrates the cost and correctness of direct V8-to-managed
calls; it is not yet a general plugin contract. Production expansion requires
per-runtime registration/lifetime isolation, generated versioned tables, bounded
blittable buffers, explicit error codes, and disposal/concurrency tests.

On the pinned Advanced Charts resize workload, the clean rectangle microprobe is
14.87x faster across the boundary and the layout-bearing numeric read is 1.30x faster.
The warmed whole-resize result is only about 1.5% faster at the mean/p50, has no p95
improvement, and reduces managed allocation by about 0.8%. This validates the ABI
mechanism but shows that CSS/layout/chart work, not generic dispatch alone, dominates
resize. See `docs/architecture/native-v8-scene-engine-spike.md` for the full A/B.

The security-token patch is deliberately only a proof. Applied as written, it weakens context
isolation for every context in the runtime. A production fork must make token
sharing explicit and opt-in for a trusted same-origin context group; unrelated
contexts must retain distinct default security tokens. It also needs upstream
tests and builds for every supported native runtime identifier.

The matching WebScene probe is `probe v8dom`. With the experimental native build
replacing the package binary under the output's
`runtimes/<rid>/native/` directory, it must print:

```text
Shared V8 runtime owner/frame object bridge: pass
```

The real-chart `probe v8chart` now reaches visible output and its generic typed-array
Canvas2D boundary has demonstrated large interaction allocation and latency wins.
Generic state-write deduplication and retained same-state `fillRect` coalescing reduce
the remaining managed boundary/replay allocation further; the latter preserves exact
expanded replay pixels (0/1,008,401 changed) and resize geometry. These optimizations
do not depend on product behavior.
Direct and nested owner/frame Promise probes preserve expected task ordering, so a
native microtask patch is neither required nor desirable. Seven React #327 diagnostics
remain during chart load, and production still needs an opt-in trusted same-origin
context-group design, per-RID native builds, native-memory/disposal validation, and
the full parity matrix. V8 is the repository Playground's default engineering lane,
while the reusable runtime remains an explicit application choice.

The managed runtime now lives in the optional `JavaScript.Avalonia.ClearScript`
project. It creates one dedicated `V8Runtime` per WebScene host, leaves iframe sharing
disabled unless `EnableTrustedSameOriginContextSharing` is explicitly enabled, and
detaches all installed host adapters/window references on disposal. This narrows the
managed trust boundary, but it does not replace review and per-RID validation of the
native patch itself.

## Reproducible native build and package

WebScene deliberately references `Microsoft.ClearScript.V8`, not
`Microsoft.ClearScript.Complete`. The latter restores Microsoft's stock native package
for every RID and could silently bypass this patch outside the one local output that
was overwritten. Every native binary must now be supplied explicitly.

The source dependencies are registered as `third-party/clearscript` and
`third-party/v8` submodules. The WebScene-owned ClearScript fork publishes the
`webscene/7.5.1` branch containing the trusted-context and optional typed-ABI commits;
Microsoft's repository remains its upstream. V8 stays pinned to upstream revision
`14.7.173.23`, with ClearScript's matching compatibility patchset applied by the build.

For macOS and Linux, initialize the submodules and build from the ClearScript branch.
The first RID downloads the tested V8 build dependencies; later RIDs reuse that
checkout:

```sh
git submodule update --init --recursive

./scripts/build-clearscript-v8-native.sh \
  --rid osx-arm64 \
  --download-v8

./scripts/build-clearscript-v8-native.sh \
  --rid osx-x64
```

Linux supports `linux-x64`, `linux-arm`, and `linux-arm64` through the same script on a
Linux build host. The script verifies the source tag, applies or recognizes both exact
patches, builds ClearScript's pinned V8 revision `14.7.173.23`, checks the output file's
RID/architecture shape, and produces a RID-specific NuGet package under
`artifacts/v8-native`. Packing also copies the reviewed asset to
`artifacts/v8-native/runtimes/<rid>/native`; the JavaScript Playground discovers that
cache automatically, so later `dotnet run -c Release` commands need no native-path or
RID variables.

Windows uses the same fresh tag and patch. The PowerShell source-to-package script
locates Visual Studio, builds the pinned V8 revision and selected native project,
validates the PE machine type, and invokes the same package project:

```powershell
.\scripts\build-clearscript-v8-native.ps1 `
  -Source C:\src\ClearScript-7.5.1 `
  -Rid win-x64
```

Use `-ReuseV8` for later RIDs from the same checkout. The underlying matching projects
are:

```text
ClearScriptV8\win-x86\ClearScriptV8.win-x86.vcxproj    (Platform=Win32)
ClearScriptV8\win-x64\ClearScriptV8.win-x64.vcxproj    (Platform=x64)
ClearScriptV8\win-arm64\ClearScriptV8.win-arm64.vcxproj (Platform=ARM64)
```

The Windows script packs each output automatically. To pack an already reviewed output
directly, use
`packaging/JavaScript.Avalonia.ClearScript.Native/JavaScript.Avalonia.ClearScript.Native.csproj`,
passing `WebSceneClearScriptNativeRid` and `WebSceneClearScriptNativePath`. The supported
package RIDs are `win-x86`, `win-x64`, `win-arm64`, `linux-x64`, `linux-arm`,
`linux-arm64`, `osx-x64`, and `osx-arm64`.

Every package contains exactly one native runtime asset, both exact patches, source/V8
provenance, and SHA-256 files. Before publication, build the WebScene benchmark with that
native path/RID and run `probe v8dom`; the owner/frame object bridge must pass. Run the
full responsive chart matrix and ten-cycle concurrent plateau on each executable target
RID before declaring it reviewed.

Validate the package structure itself with the product-independent package probe:

```sh
dotnet benchmarks/JavaScript.Avalonia.Benchmarks/bin/Release/net10.0/JavaScript.Avalonia.Benchmarks.dll \
  probe v8nativepackage \
  --package artifacts/v8-native/JavaScript.Avalonia.ClearScript.Native.osx-arm64.11.3.4.nupkg \
  --rid osx-arm64
```

Then prove that a clean consumer restores and loads the native asset from the local
package feed rather than a loose output overwrite:

```sh
./scripts/test-clearscript-v8-native-package.sh \
  --rid osx-arm64 \
  --feed artifacts/v8-native
```
