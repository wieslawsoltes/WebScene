using System.Runtime.InteropServices;

namespace NativeRuntimeShowcase.Interop;

public static class ShowcasePaths
{
    public const string TradingViewUrl =
        "https://trading-terminal.tradingview-widget.com/?theme=dark";

    public static string ResolveNativeLibraryPath(
        IReadOnlyList<string> arguments)
    {
        for (var index = 0; index + 1 < arguments.Count; index++)
        {
            if (arguments[index] == "--native-library")
            {
                return Path.GetFullPath(arguments[index + 1]);
            }
        }

        var configured = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_LIBRARY");
        if (!string.IsNullOrWhiteSpace(configured))
        {
            return Path.GetFullPath(configured);
        }

        var packaged = Path.Combine(
            AppContext.BaseDirectory,
            NativeLibraryFileName());
        if (File.Exists(packaged))
        {
            return packaged;
        }

        throw new FileNotFoundException(
            "The WebScene native engine was not found. Pass --native-library "
            + $"/absolute/path/to/{NativeLibraryFileName()} or set "
            + "WEBSCENE_NATIVE_ENGINE_LIBRARY.",
            packaged);
    }

    public static string NativeLibraryFileName()
        => RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? "webscene_native_engine.dll"
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libwebscene_native_engine.dylib"
                : "libwebscene_native_engine.so";

    public static string CacheDirectory(string host, string document)
    {
        var directory = Path.Combine(
            Environment.GetFolderPath(
                Environment.SpecialFolder.LocalApplicationData),
            "WebScene",
            "NativeRuntimeShowcase",
            host,
            document);
        Directory.CreateDirectory(directory);
        return directory;
    }
}
