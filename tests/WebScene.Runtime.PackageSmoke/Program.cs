using System.Runtime.InteropServices;
using System.Text.Json;
using WebScene.Testing;
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
    // Check the packaged binary against the public header, not a hand-maintained
    // subset: macOS release dead-stripping can hide newly added ABI functions.
    using var headerStream = typeof(GetAbiVersion).Assembly.GetManifestResourceStream("WebScene.NativeAbiHeader")
        ?? throw new InvalidOperationException("The public native ABI header was not embedded.");
    using var headerReader = new StreamReader(headerStream);
    var publicFunctions = NativeAbiContract.PublicFunctions(headerReader.ReadToEnd());
    if (publicFunctions.Length == 0)
        return Fail("No public native ABI functions were found in the embedded header.");
    foreach (var name in publicFunctions)
    {
        if (!NativeLibrary.TryGetExport(library, name, out _))
            return Fail($"The packaged native runtime does not export public ABI function '{name}'.");
    }
    Console.WriteLine($"Public native ABI exports verified: {publicFunctions.Length}.");

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
    if (buildFeatures != inspectorBuildFeature)
    {
        return Fail(
            $"The packaged native runtime has build features 0x{buildFeatures:X}; " +
            $"expected Inspector-only feature 0x{inspectorBuildFeature:X}.");
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
