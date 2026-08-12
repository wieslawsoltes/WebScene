using System.Runtime.InteropServices;
using System.Text;
using WebScene.Core;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

/// <summary>
/// Process-wide lifecycle operations for the ABI 3 native WebScene engine.
/// </summary>
public static class NativeWebSceneRuntime
{
    public const uint RequiredAbiVersion = 3;

    public const WebSceneBackendCapabilities BaselineCapabilities =
        WebSceneBackendCapabilities.DomProjection
        | WebSceneBackendCapabilities.CssLayout
        | WebSceneBackendCapabilities.Canvas2D
        | WebSceneBackendCapabilities.Svg
        | WebSceneBackendCapabilities.Images
        | WebSceneBackendCapabilities.PointerInput
        | WebSceneBackendCapabilities.KeyboardInput
        | WebSceneBackendCapabilities.TextInput
        | WebSceneBackendCapabilities.Focus;

    private static readonly SemaphoreSlim PrewarmGate = new(1, 1);
    private static string? s_prewarmedLibraryPath;
    private static NativeWebSceneRuntimeInfo? s_runtimeInfo;

    /// <summary>
    /// Loads a native engine only far enough to verify the stable product ABI.
    /// </summary>
    public static NativeWebSceneRuntimeInfo InspectLibrary(string nativeLibraryPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeLibraryPath);
        var fullPath = Path.GetFullPath(nativeLibraryPath);
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException(
                "The WebScene native engine was not found.",
                fullPath);
        }

        IntPtr library = IntPtr.Zero;
        try
        {
            library = NativeLibrary.Load(fullPath);
            if (!NativeLibrary.TryGetExport(
                    library,
                    "webscene_engine_get_abi_version",
                    out var versionAddress))
            {
                throw new InvalidOperationException(
                    $"'{fullPath}' is not a compatible WebScene native engine: " +
                    "the ABI version export is missing.");
            }

            var getAbiVersion =
                Marshal.GetDelegateForFunctionPointer<GetAbiVersion>(versionAddress);
            var abiVersion = getAbiVersion();
            if (abiVersion != RequiredAbiVersion)
            {
                throw new InvalidOperationException(
                    $"The WebScene native engine ABI is {abiVersion}, but this " +
                    $"managed runtime requires ABI {RequiredAbiVersion}. " +
                    $"Library: '{fullPath}'.");
            }

            if (!NativeLibrary.TryGetExport(
                    library,
                    "webscene_engine_get_build_features",
                    out var featureAddress))
            {
                throw new InvalidOperationException(
                    $"'{fullPath}' is not a compatible WebScene native engine: " +
                    "the build-features export is missing.");
            }
            var getBuildFeatures =
                Marshal.GetDelegateForFunctionPointer<GetAbiVersion>(featureAddress);
            return new NativeWebSceneRuntimeInfo(fullPath, abiVersion)
            {
                BuildFeatures = (NativeWebSceneBuildFeatures)getBuildFeatures()
            };
        }
        catch (Exception error) when (
            error is BadImageFormatException or DllNotFoundException)
        {
            throw new InvalidOperationException(
                $"The WebScene native engine could not be loaded from '{fullPath}'. " +
                "Verify that the file matches the operating system and process architecture.",
                error);
        }
        finally
        {
            if (library != IntPtr.Zero)
            {
                NativeLibrary.Free(library);
            }
        }
    }

    /// <summary>
    /// Initializes the shared V8 platform without creating a document or isolate.
    /// Repeated calls for the same library are inexpensive.
    /// </summary>
    public static async Task PrewarmAsync(
        string nativeLibraryPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeLibraryPath);
        var fullPath = Path.GetFullPath(nativeLibraryPath);
        await PrewarmGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (string.Equals(
                    Volatile.Read(ref s_prewarmedLibraryPath),
                    fullPath,
                    StringComparison.Ordinal))
            {
                return;
            }

            var runtimeInfo = InspectLibrary(fullPath);
            NativeWebSceneApi.ConfigureLibraryPath(fullPath);
            var accepted = await Task.Run(
                    () => NativeWebSceneApi.EnginePrewarm() != 0,
                    cancellationToken)
                .ConfigureAwait(false);
            if (!accepted)
            {
                throw new InvalidOperationException(
                    "The WebScene native engine could not prewarm its V8 process runtime.");
            }

            Volatile.Write(ref s_prewarmedLibraryPath, fullPath);
            Volatile.Write(ref s_runtimeInfo, runtimeInfo);
        }
        finally
        {
            PrewarmGate.Release();
        }
    }

    public static NativeWebSceneRuntimeInfo? RuntimeInfo
        => Volatile.Read(ref s_runtimeInfo);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint GetAbiVersion();
}

[Flags]
public enum NativeWebSceneBuildFeatures : uint
{
    None = 0,
    Certification = 1U << 0,
    V8Inspector = 1U << 1,
    GpuProviderAbi = 1U << 2,
    WebGpuBindings = 1U << 3,
    WebGlBindings = 1U << 4
}

public sealed record NativeWebSceneRuntimeInfo(string LibraryPath, uint AbiVersion)
{
    public NativeWebSceneBuildFeatures BuildFeatures { get; init; }
}

/// <summary>Discovers and validates the optional zero-copy GPU sidecar.</summary>
public static unsafe class NativeWebSceneGpuRuntime
{
    public const uint RequiredAbiVersion = 2;

    public const WebSceneBackendCapabilities GpuCapabilities =
        WebSceneBackendCapabilities.WebGl1
        | WebSceneBackendCapabilities.WebGpu
        | WebSceneBackendCapabilities.WebGl2;

    public static string LibraryFileName
        => OperatingSystem.IsWindows()
            ? "webscene_native_gpu.dll"
            : OperatingSystem.IsMacOS()
                ? "libwebscene_native_gpu.dylib"
                : "libwebscene_native_gpu.so";

    public static string? ResolveLibraryPath(
        string nativeEnginePath,
        string? configuredGpuLibraryPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeEnginePath);
        if (!string.IsNullOrWhiteSpace(configuredGpuLibraryPath))
        {
            return Path.GetFullPath(configuredGpuLibraryPath);
        }
        var engineDirectory = Path.GetDirectoryName(Path.GetFullPath(nativeEnginePath))
            ?? throw new ArgumentException(
                "The native engine path has no containing directory.",
                nameof(nativeEnginePath));
        var adjacent = Path.Combine(engineDirectory, LibraryFileName);
        return File.Exists(adjacent) ? adjacent : null;
    }

    public static NativeWebSceneGpuRuntimeInfo InspectLibrary(string nativeLibraryPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeLibraryPath);
        var fullPath = Path.GetFullPath(nativeLibraryPath);
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException(
                "The WebScene native GPU provider was not found.",
                fullPath);
        }

        IntPtr library = IntPtr.Zero;
        try
        {
            library = NativeLibrary.Load(fullPath);
            var getAbi = GetExport<GetAbiVersion>(
                library,
                fullPath,
                "webscene_gpu_provider_get_abi_version");
            var abiVersion = getAbi();
            if (abiVersion != RequiredAbiVersion)
            {
                throw new InvalidOperationException(
                    $"The WebScene GPU provider ABI is {abiVersion}, but this runtime " +
                    $"requires ABI {RequiredAbiVersion}. Library: '{fullPath}'.");
            }

            var getInfo = GetExport<GetInfo>(
                library,
                fullPath,
                "webscene_gpu_provider_get_info");
            var info = new GpuProviderInfo
            {
                StructSize = (uint)sizeof(GpuProviderInfo)
            };
            if (getInfo(ref info) == 0 || info.AbiVersion != RequiredAbiVersion)
            {
                throw new InvalidOperationException(
                    $"The WebScene GPU provider returned invalid metadata. Library: '{fullPath}'.");
            }
            var capabilities = (WebSceneBackendCapabilities)info.Capabilities
                & GpuCapabilities;
            return new NativeWebSceneGpuRuntimeInfo(
                fullPath,
                abiVersion,
                capabilities,
                Decode(info.Name, 64),
                Decode(info.Adapter, 128));
        }
        catch (Exception error) when (
            error is BadImageFormatException or DllNotFoundException)
        {
            throw new InvalidOperationException(
                $"The WebScene GPU provider could not be loaded from '{fullPath}'. " +
                "Verify that the file matches the operating system and process architecture.",
                error);
        }
        finally
        {
            if (library != IntPtr.Zero) NativeLibrary.Free(library);
        }
    }

    private static T GetExport<T>(IntPtr library, string path, string name)
        where T : Delegate
    {
        if (!NativeLibrary.TryGetExport(library, name, out var address))
        {
            throw new InvalidOperationException(
                $"'{path}' is not a compatible WebScene GPU provider: " +
                $"the '{name}' export is missing.");
        }
        return Marshal.GetDelegateForFunctionPointer<T>(address);
    }

    private static string Decode(byte* value, int length)
    {
        var used = 0;
        while (used < length && value[used] != 0) used++;
        return Encoding.UTF8.GetString(value, used);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct GpuProviderInfo
    {
        public uint StructSize;
        public uint AbiVersion;
        public ulong Capabilities;
        public ulong Flags;
        public fixed byte Name[64];
        public fixed byte Adapter[128];
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint GetAbiVersion();

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte GetInfo(ref GpuProviderInfo info);
}

public sealed record NativeWebSceneGpuRuntimeInfo(
    string LibraryPath,
    uint AbiVersion,
    WebSceneBackendCapabilities Capabilities,
    string Name,
    string Adapter);
