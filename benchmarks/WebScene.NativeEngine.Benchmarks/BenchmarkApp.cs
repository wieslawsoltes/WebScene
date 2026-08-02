using Avalonia;
using Avalonia.Headless;
using Avalonia.Skia;

namespace WebScene.NativeEngine.Benchmarks;

internal sealed class BenchmarkApp : Application
{
    private static bool s_initialized;

    internal static void EnsureInitialized()
    {
        if (s_initialized)
        {
            return;
        }

        AppBuilder.Configure<BenchmarkApp>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = false
            })
            .SetupWithoutStarting();
        s_initialized = true;
    }
}
