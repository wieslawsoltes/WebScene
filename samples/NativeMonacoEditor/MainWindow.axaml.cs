using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

namespace NativeMonacoEditor;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Opened += OnOpened;
        Closed += OnClosed;
    }

    private async void OnOpened(object? sender, EventArgs args)
    {
        try
        {
            var libraryPath = ResolveNativeLibraryPath(Environment.GetCommandLineArgs());
            var documentPath = Path.Combine(AppContext.BaseDirectory, "index.html");
            var cachePath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "WebScene",
                "NativeMonacoEditor",
                "v8-cache");
            await EditorHost.LoadAsync(
                new Uri(documentPath).AbsoluteUri,
                libraryPath,
                cachePath);
        }
        catch (Exception error)
        {
            LoadFailureText.Text =
                "The native Monaco sample could not start.\n\n"
                + error.Message
                + "\n\nPass --native-library /absolute/path/to/"
                + NativeLibraryFileName();
            LoadFailure.IsVisible = true;
        }
    }

    private async void OnClosed(object? sender, EventArgs args)
    {
        await EditorHost.DisposeAsync();
    }

    private static string ResolveNativeLibraryPath(IReadOnlyList<string> arguments)
    {
        for (var index = 0; index + 1 < arguments.Count; index++)
        {
            if (string.Equals(
                    arguments[index],
                    "--native-library",
                    StringComparison.Ordinal))
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

        var packaged = Path.Combine(AppContext.BaseDirectory, NativeLibraryFileName());
        if (File.Exists(packaged)) return packaged;

        throw new FileNotFoundException(
            "No WebScene native engine was configured.",
            packaged);
    }

    private static string NativeLibraryFileName()
        => OperatingSystem.IsWindows()
            ? "webscene_native_engine.dll"
            : OperatingSystem.IsMacOS()
                ? "libwebscene_native_engine.dylib"
                : "libwebscene_native_engine.so";
}
