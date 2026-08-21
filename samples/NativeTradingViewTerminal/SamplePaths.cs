using System.Runtime.InteropServices;

namespace NativeTradingViewTerminal;

internal sealed record SamplePaths(
    string NativeLibraryPath,
    string CompilationCacheDirectory,
    string DocumentUrl,
    string? ResourceCaptureDirectory,
    string? ResourceReplayDirectory)
{
    internal const string TerminalUrl =
        "https://trading-terminal.tradingview-widget.com/";

    internal static SamplePaths Resolve(IReadOnlyList<string> arguments)
    {
        string? configuredLibrary = null;
        string? configuredCache = null;
        string? configuredUrl = null;
        string? resourceCaptureDirectory = null;
        string? resourceReplayDirectory = null;
        for (var index = 0; index < arguments.Count; index++)
        {
            switch (arguments[index])
            {
                case "--native-library" when index + 1 < arguments.Count:
                    configuredLibrary = Path.GetFullPath(arguments[++index]);
                    break;
                case "--cache" when index + 1 < arguments.Count:
                    configuredCache = Path.GetFullPath(arguments[++index]);
                    break;
                case "--url" when index + 1 < arguments.Count:
                    configuredUrl = arguments[++index];
                    break;
                case "--capture-resources" when index + 1 < arguments.Count:
                    resourceCaptureDirectory = Path.GetFullPath(arguments[++index]);
                    break;
                case "--replay-resources" when index + 1 < arguments.Count:
                    resourceReplayDirectory = Path.GetFullPath(arguments[++index]);
                    break;
            }
        }

        if (resourceCaptureDirectory is not null && resourceReplayDirectory is not null)
        {
            throw new ArgumentException(
                "--capture-resources and --replay-resources are mutually exclusive.");
        }

        configuredLibrary ??=
            Environment.GetEnvironmentVariable("WEBSCENE_NATIVE_ENGINE_LIBRARY");
        var nativeLibrary = !string.IsNullOrWhiteSpace(configuredLibrary)
            ? Path.GetFullPath(configuredLibrary)
            : Path.Combine(AppContext.BaseDirectory, NativeLibraryFileName());
        if (!File.Exists(nativeLibrary))
        {
            throw new FileNotFoundException(
                "The WebScene native engine was not found. Pass "
                + $"--native-library /absolute/path/to/{NativeLibraryFileName()} "
                + "or set WEBSCENE_NATIVE_ENGINE_LIBRARY.",
                nativeLibrary);
        }

        var cache = configuredCache ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WebScene",
            "NativeTradingViewTerminal",
            "v8-cache");
        Directory.CreateDirectory(cache);
        return new SamplePaths(
            nativeLibrary,
            cache,
            string.IsNullOrWhiteSpace(configuredUrl)
                ? TerminalUrl
                : configuredUrl,
            resourceCaptureDirectory,
            resourceReplayDirectory);
    }

    internal static string NativeLibraryFileName()
        => RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? "webscene_native_engine.dll"
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libwebscene_native_engine.dylib"
                : "libwebscene_native_engine.so";
}
