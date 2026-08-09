using System.Runtime.InteropServices;
using WebScene.Backends.Uno.Native;
using WebScene.Sdk.Uno;

if (typeof(UnoNativeWebSceneView).Assembly.GetName().Name != "WebScene.Backend.Uno")
{
    return Fail("The Uno presenter did not come from WebScene.Backend.Uno.");
}
if (typeof(WebSceneComponentHost).Assembly.GetName().Name != "WebScene.Sdk.Uno"
    || typeof(WebSceneComponentHost).GetMethod(
        nameof(WebSceneComponentHost.MountAsync)) is null)
{
    return Fail("The Uno component host package is missing its lifecycle API.");
}

var nativeFileName = OperatingSystem.IsWindows()
    ? "webscene_native_engine.dll"
    : OperatingSystem.IsMacOS()
        ? "libwebscene_native_engine.dylib"
        : "libwebscene_native_engine.so";
var nativePath = Path.Combine(AppContext.BaseDirectory, nativeFileName);
if (!File.Exists(nativePath))
{
    return Fail($"The runtime package did not copy '{nativeFileName}'.");
}

var library = NativeLibrary.Load(nativePath);
try
{
    var export = NativeLibrary.GetExport(library, "webscene_engine_get_abi_version");
    var getAbiVersion = Marshal.GetDelegateForFunctionPointer<GetAbiVersion>(export);
    var abiVersion = getAbiVersion();
    if (abiVersion != 3)
    {
        return Fail($"The native runtime reported ABI {abiVersion}; expected 3.");
    }
    Console.WriteLine(
        $"WebScene Uno package smoke: pass; presenter={typeof(UnoNativeWebSceneView).Assembly.GetName().Name}; "
        + $"componentHost={typeof(WebSceneComponentHost).Assembly.GetName().Name}; abi={abiVersion}");
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
