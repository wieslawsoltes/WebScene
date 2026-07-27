using Avalonia;
using Avalonia.Headless;
using Avalonia.Skia;

namespace WebScene.WebPlatformSubset.Runner;

internal sealed class RunnerApp : Application
{
    private static bool s_initialized;

    internal static void EnsureInitialized()
    {
        if (s_initialized)
        {
            return;
        }

        AppBuilder.Configure<RunnerApp>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = false
            })
            .SetupWithoutStarting();
        s_initialized = true;
    }
}
