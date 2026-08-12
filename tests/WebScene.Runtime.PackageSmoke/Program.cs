using System.Runtime.InteropServices;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Sdk.Avalonia;

if (typeof(NativeWebSceneView).Assembly.GetName().Name != "WebScene.Backend.Avalonia")
{
    return Fail("The native Avalonia presenter did not come from WebScene.Backend.Avalonia.");
}
if (typeof(WebSceneComponentHost).Assembly.GetName().Name != "WebScene.Sdk.Avalonia"
    || typeof(WebSceneComponentHost).GetMethod(
        nameof(WebSceneComponentHost.MountAsync)) is null)
{
    return Fail("The reusable Avalonia component host package is missing its lifecycle API.");
}

var nativeFileName = OperatingSystem.IsWindows()
    ? "webscene_native_engine.dll"
    : OperatingSystem.IsMacOS()
        ? "libwebscene_native_engine.dylib"
        : "libwebscene_native_engine.so";
var nativePath = Path.Combine(AppContext.BaseDirectory, nativeFileName);
var icuPath = Path.Combine(AppContext.BaseDirectory, "icudtl.dat");
var manifestPath = Path.Combine(AppContext.BaseDirectory, "webscene-native-runtime.json");
foreach (var required in new[] { nativePath, icuPath, manifestPath })
{
    if (!File.Exists(required))
    {
        return Fail($"The runtime package did not copy '{Path.GetFileName(required)}'.");
    }
}

var library = NativeLibrary.Load(nativePath);
try
{
    using var manifest = JsonDocument.Parse(File.ReadAllText(manifestPath));
    foreach (var (name, expected) in new[]
             {
                 ("htmlParser", "html5ever"),
                 ("cssParser", "cssparser"),
                 ("selectorParser", "servo"),
                 ("domBindings", "generated"),
                 ("v8Snapshot", "bootstrap")
             })
    {
        if (!manifest.RootElement.TryGetProperty(name, out var value)
            || value.GetString() != expected)
        {
            return Fail($"The native runtime manifest must declare {name}={expected}.");
        }
    }
    if (!manifest.RootElement.TryGetProperty("webGpu", out var webGpuProperty)
        || webGpuProperty.GetBoolean() != OperatingSystem.IsMacOS())
    {
        return Fail("The native runtime manifest has an incorrect WebGPU build declaration.");
    }
    foreach (var snapshotFile in new[]
             {
                 "webscene_bootstrap_snapshot.bin",
                 "webscene_bootstrap_snapshot.meta"
             })
    {
        if (!File.Exists(Path.Combine(AppContext.BaseDirectory, snapshotFile)))
        {
            return Fail($"The runtime package did not copy '{snapshotFile}'.");
        }
    }
    if (!manifest.RootElement.TryGetProperty("abiVersion", out var manifestAbiProperty)
        || !manifestAbiProperty.TryGetUInt32(out var manifestAbiVersion))
    {
        return Fail("The native runtime manifest does not contain a valid numeric 'abiVersion'.");
    }

    var export = NativeLibrary.GetExport(library, "webscene_engine_get_abi_version");
    var getAbiVersion = Marshal.GetDelegateForFunctionPointer<GetAbiVersion>(export);
    var abiVersion = getAbiVersion();
    if (abiVersion != 3)
    {
        return Fail($"The native runtime reported ABI {abiVersion}; expected 3.");
    }
    if (manifestAbiVersion != abiVersion)
    {
        return Fail(
            $"The native runtime manifest declares ABI {manifestAbiVersion}, " +
            $"but the library exports ABI {abiVersion}.");
    }
    foreach (var inspectorExport in new[]
             {
                 "webscene_engine_inspector_connect_v3",
                 "webscene_engine_inspector_take_message"
             })
    {
        if (!NativeLibrary.TryGetExport(library, inspectorExport, out _))
        {
            return Fail(
                $"The native runtime does not export required Inspector ABI symbol '{inspectorExport}'.");
        }
    }
    if (!NativeLibrary.TryGetExport(
            library,
            "webscene_engine_load_url_with_options",
            out _))
    {
        return Fail(
            "The ABI 3 runtime is missing document-start navigation support.");
    }
    if (NativeLibrary.TryGetExport(
            library,
            "webscene_engine_evaluate_json",
            out _))
    {
        return Fail(
            "The removed synchronous JSON evaluation export is still present.");
    }
    var buildFeaturesExport = NativeLibrary.GetExport(
        library,
        "webscene_engine_get_build_features");
    var getBuildFeatures = Marshal.GetDelegateForFunctionPointer<GetBuildFeatures>(
        buildFeaturesExport);
    var buildFeatures = getBuildFeatures();
    const uint inspectorBuildFeature = 1U << 1;
    const uint gpuProviderAbiBuildFeature = 1U << 2;
    const uint webGpuBindingsBuildFeature = 1U << 3;
    var expectedBuildFeatures = OperatingSystem.IsMacOS()
        ? inspectorBuildFeature | gpuProviderAbiBuildFeature | webGpuBindingsBuildFeature
        : inspectorBuildFeature;
    if (buildFeatures != expectedBuildFeatures)
    {
        return Fail(
            $"The packaged native runtime has build features 0x{buildFeatures:X}; " +
            $"expected 0x{expectedBuildFeatures:X}.");
    }
    if (OperatingSystem.IsMacOS())
    {
        var gpuPath = Path.Combine(
            AppContext.BaseDirectory,
            "libwebscene_native_gpu.dylib");
        var gpuManifestPath = Path.Combine(
            AppContext.BaseDirectory,
            "webscene-native-gpu-runtime.json");
        if (!File.Exists(gpuPath) || !File.Exists(gpuManifestPath))
        {
            return Fail("The macOS package consumer is missing the WebGPU sidecar assets.");
        }
        using var gpuManifest = JsonDocument.Parse(File.ReadAllText(gpuManifestPath));
        if (!gpuManifest.RootElement.TryGetProperty("abiVersion", out var gpuAbiProperty)
            || gpuAbiProperty.GetUInt32() != 2
            || !gpuManifest.RootElement.TryGetProperty("dawnRevision", out var dawnProperty)
            || dawnProperty.GetString() != "710c33013c53ab2700d332c25ff51430251a8cc4")
        {
            return Fail("The WebGPU sidecar manifest does not identify ABI 2 and the pinned Dawn revision.");
        }
        var gpuLibrary = NativeLibrary.Load(gpuPath);
        try
        {
            var gpuAbiExport = NativeLibrary.GetExport(
                gpuLibrary,
                "webscene_gpu_provider_get_abi_version");
            var getGpuAbi = Marshal.GetDelegateForFunctionPointer<GetAbiVersion>(
                gpuAbiExport);
            if (getGpuAbi() != 2)
            {
                return Fail("The WebGPU sidecar did not export provider ABI 2.");
            }
        }
        finally
        {
            NativeLibrary.Free(gpuLibrary);
        }
    }
    Console.WriteLine(
        $"WebScene package smoke: pass; presenter={typeof(NativeWebSceneView).Assembly.GetName().Name}; " +
        $"runtime={Path.GetFileName(nativePath)}; abi={abiVersion}; " +
        $"manifestAbi={manifestAbiVersion}; buildFeatures={buildFeatures}");
}
finally
{
    NativeLibrary.Free(library);
}

return 0;

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 1;
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate uint GetAbiVersion();

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate uint GetBuildFeatures();
