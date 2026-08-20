using Avalonia;
using Avalonia.Headless;
using Avalonia.Skia;

namespace NativeTradingViewTerminal;

internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--headless-proof", StringComparer.Ordinal)
            || args.Contains("--sandwich-layout-proof", StringComparer.Ordinal))
        {
            BuildHeadlessApp().SetupWithoutStarting();
            return args.Contains("--sandwich-layout-proof", StringComparer.Ordinal)
                ? SandwichLayoutProof.Run(args)
                : HeadlessProof.Run(args);
        }

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        return 0;
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();

    private static AppBuilder BuildHeadlessApp()
        => AppBuilder.Configure<App>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = false
            })
            .LogToTrace();
}
