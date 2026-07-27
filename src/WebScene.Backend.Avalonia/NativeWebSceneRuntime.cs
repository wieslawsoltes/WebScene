using System.Runtime.InteropServices;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

/// <summary>
/// Process-wide lifecycle operations for the ABI 2 native WebScene engine.
/// </summary>
public static class NativeWebSceneRuntime
{
    public const uint RequiredAbiVersion = 2;

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

            return new NativeWebSceneRuntimeInfo(fullPath, abiVersion);
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

public sealed record NativeWebSceneRuntimeInfo(string LibraryPath, uint AbiVersion);
