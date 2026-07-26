using System.Runtime.InteropServices;

namespace NativeTradingViewTerminal;

internal sealed record SamplePaths(
    string NativeLibraryPath,
    string CompilationCacheDirectory)
{
    internal const string TerminalUrl =
        "https://trading-terminal.tradingview-widget.com/";

    internal static SamplePaths Resolve(IReadOnlyList<string> arguments)
    {
        string? configuredLibrary = null;
        string? configuredCache = null;
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
            }
        }

        configuredLibrary ??=
            Environment.GetEnvironmentVariable("HTMLML_NATIVE_ENGINE_LIBRARY");
        var nativeLibrary = !string.IsNullOrWhiteSpace(configuredLibrary)
            ? Path.GetFullPath(configuredLibrary)
            : Path.Combine(AppContext.BaseDirectory, NativeLibraryFileName());
        if (!File.Exists(nativeLibrary))
        {
            throw new FileNotFoundException(
                "The HtmlML native engine was not found. Pass "
                + $"--native-library /absolute/path/to/{NativeLibraryFileName()} "
                + "or set HTMLML_NATIVE_ENGINE_LIBRARY.",
                nativeLibrary);
        }

        var cache = configuredCache ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "HtmlML",
            "NativeTradingViewTerminal",
            "v8-cache");
        Directory.CreateDirectory(cache);
        return new SamplePaths(nativeLibrary, cache);
    }

    internal static string NativeLibraryFileName()
        => RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? "htmlml_native_engine.dll"
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libhtmlml_native_engine.dylib"
                : "libhtmlml_native_engine.so";
}
