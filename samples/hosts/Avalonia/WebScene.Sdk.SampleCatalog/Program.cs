using Avalonia;

namespace WebScene.Sdk.SampleCatalog;

internal static class Program
{
    public static string? InitialSampleId { get; private set; }
    public static bool SmokeMode { get; private set; }

    [STAThread]
    public static void Main(string[] args)
    {
        SmokeMode = args.Contains("--webscene-smoke", StringComparer.Ordinal);
        var initialSampleIndex = Array.FindIndex(
            args,
            static value => !value.StartsWith("-", StringComparison.Ordinal));
        InitialSampleId = initialSampleIndex >= 0 ? args[initialSampleIndex] : null;
        var avaloniaArgs = args
            .Where((value, index) => index != initialSampleIndex && !string.Equals(value, "--webscene-smoke", StringComparison.Ordinal))
            .ToArray();
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(avaloniaArgs);
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace();
}
