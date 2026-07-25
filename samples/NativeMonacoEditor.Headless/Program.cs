using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media.Imaging;
using Avalonia.Skia;
using Avalonia.Threading;
using HtmlML.Backends.Avalonia.Native;

namespace NativeMonacoEditor.Headless;

internal sealed class CaptureApp : Application;

internal static class Program
{
    private const int Width = 1100;
    private const int Height = 720;

    [STAThread]
    public static int Main(string[] args)
    {
        var options = CaptureOptions.Parse(args);
        Directory.CreateDirectory(options.OutputDirectory);

        AppBuilder.Configure<CaptureApp>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = false
            })
            .SetupWithoutStarting();

        var view = new NativeHtmlMlView(useCompositionVisual: false);
        var window = new Window
        {
            Width = Width,
            Height = Height,
            Content = view,
            Background = Avalonia.Media.Brushes.Black
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var source = new Uri(Path.Combine(AppContext.BaseDirectory, "index.html"))
                .AbsoluteUri;
            var cacheDirectory = Path.Combine(
                options.OutputDirectory,
                "native-cache");
            PumpUntil(
                view.LoadAsync(source, options.NativeLibraryPath, cacheDirectory),
                TimeSpan.FromSeconds(40));
            PumpFrames(view, window, TimeSpan.FromSeconds(4));
            var stateTask = view.EvaluateJsonAsync("""
                ({
                  ready: globalThis.__htmlMlComponentReady === true,
                  hasEditor: Boolean(globalThis.__htmlMlMonacoEditor),
                  value: globalThis.__htmlMlMonacoEditor?.getValue() ?? null,
                  viewLines: document.querySelectorAll('.view-line').length,
                  tokenSpans: document.querySelectorAll('.view-line span[class*="mtk"]').length,
                  activeTag: document.activeElement?.tagName ?? null,
                  status: document.getElementById('status')?.textContent ?? null,
                  layout: globalThis.__htmlMlMonacoEditor?.getLayoutInfo() ?? null,
                  geometry: Object.fromEntries(
                    ['.monaco-editor', '.margin', '.lines-content', '.view-lines',
                     '.view-line', 'textarea.inputarea'].map(selector => {
                      const element = document.querySelector(selector);
                      const rect = element?.getBoundingClientRect();
                      const style = element ? getComputedStyle(element) : null;
                      return [selector, element ? {
                        rect: [rect.x, rect.y, rect.width, rect.height],
                        left: style.left,
                        width: style.width,
                        transform: style.transform
                      } : null];
                    }))
                })
                """);
            PumpUntil(stateTask, TimeSpan.FromSeconds(10));
            Console.WriteLine($"Runtime state: {stateTask.Result}");
            var codiconFamily = NativeTextShaping
                .ResolveTypeface("codicon", 400)
                .FamilyName;
            if (!string.Equals(codiconFamily, "codicon", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException(
                    $"Monaco's downloadable Codicon font resolved to '{codiconFamily}'.");
            }
            Console.WriteLine($"Web font: {codiconFamily}");
            Console.WriteLine(
                $"Bounds: window={window.ClientSize}, view={view.Bounds}, "
                + $"surface={((NativeSceneSurface)view.Content!).Bounds}");
            Console.WriteLine(
                $"Before input: published={view.RenderDiagnostics.PublishedSceneCount}, "
                + $"rendered={view.RenderDiagnostics.RenderedSceneCount}");

            var initialPath = Path.Combine(
                options.OutputDirectory,
                "monaco-native-headless-initial.png");
            var surface = (NativeSceneSurface)view.Content!;
            SaveNativeFrame(surface, initialPath);

            var inputSequence = SubmitTextWithRetry(
                surface,
                "// typed through Avalonia native input\n");
            PumpFrames(view, window, TimeSpan.FromSeconds(3));
            var editedStateTask = view.EvaluateJsonAsync("""
                ({
                  value: globalThis.__htmlMlMonacoEditor?.getValue() ?? null,
                  lines: globalThis.__htmlMlMonacoEditor?.getModel()?.getLineCount() ?? 0,
                  status: document.getElementById('status')?.textContent ?? null
                })
                """);
            PumpUntil(editedStateTask, TimeSpan.FromSeconds(10));
            Console.WriteLine($"Edited state: {editedStateTask.Result}");

            var editedPath = Path.Combine(
                options.OutputDirectory,
                "monaco-native-headless-edited.png");
            SaveNativeFrame(surface, editedPath);

            var foldTask = view.EvaluateJsonAsync("""
                (() => {
                  const editor = globalThis.__htmlMlMonacoEditor;
                  editor.setPosition({ lineNumber: 2, column: 1 });
                  editor.trigger('htmlml-headless', 'editor.fold', {});
                  return true;
                })()
                """);
            PumpUntil(foldTask, TimeSpan.FromSeconds(10));
            PumpFrames(view, window, TimeSpan.FromSeconds(2));
            var foldedStateTask = view.EvaluateJsonAsync("""
                ({
                  viewLines: document.querySelectorAll('.view-line').length,
                  modelLines:
                    globalThis.__htmlMlMonacoEditor?.getModel()?.getLineCount() ?? 0,
                  foldIconFont: getComputedStyle(
                    document.querySelector('.codicon-folding-collapsed')
                  ).fontFamily
                })
                """);
            PumpUntil(foldedStateTask, TimeSpan.FromSeconds(10));
            Console.WriteLine($"Folded state: {foldedStateTask.Result}");
            var foldedPath = Path.Combine(
                options.OutputDirectory,
                "monaco-native-headless-folded.png");
            SaveNativeFrame(surface, foldedPath);

            Console.WriteLine($"Initial screenshot: {initialPath}");
            Console.WriteLine($"Edited screenshot:  {editedPath}");
            Console.WriteLine($"Folded screenshot:  {foldedPath}");
            Console.WriteLine(
                $"Scenes: published={view.RenderDiagnostics.PublishedSceneCount}, "
                + $"rendered={view.RenderDiagnostics.RenderedSceneCount}, "
                + $"input-sequence={inputSequence}");
            return 0;
        }
        finally
        {
            PumpUntil(view.DisposeAsync().AsTask(), TimeSpan.FromSeconds(10));
            window.Close();
        }
    }

    private static ulong SubmitTextWithRetry(
        NativeSceneSurface surface,
        string text)
    {
        ulong latestSequence = 0;
        foreach (var rune in text.EnumerateRunes())
        {
            if (rune.Value == '\n')
            {
                latestSequence = SubmitKeyWithRetry(surface, 7, 13);
                SubmitKeyWithRetry(surface, 8, 13);
                continue;
            }
            var timer = Stopwatch.StartNew();
            while ((latestSequence = surface.SubmitText(rune.ToString())) == 0)
            {
                if (timer.Elapsed >= TimeSpan.FromSeconds(5))
                {
                    throw new InvalidOperationException(
                        $"The native Monaco input target rejected '{rune}'.");
                }
                Dispatcher.UIThread.RunJobs();
                Thread.Sleep(10);
            }
        }
        return latestSequence;
    }

    private static ulong SubmitKeyWithRetry(
        NativeSceneSurface surface,
        uint kind,
        int keyCode)
    {
        var timer = Stopwatch.StartNew();
        ulong sequence;
        while ((sequence = surface.SubmitKey(kind, keyCode)) == 0)
        {
            if (timer.Elapsed >= TimeSpan.FromSeconds(5))
            {
                throw new InvalidOperationException(
                    $"The native Monaco input target rejected key code {keyCode}.");
            }
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }
        return sequence;
    }

    private static void PumpFrames(
        NativeHtmlMlView view,
        Window window,
        TimeSpan duration)
    {
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < duration)
        {
            view.RenderDiagnostics.SubmitAnimationFrame(
                timer.Elapsed.TotalMilliseconds);
            view.RenderDiagnostics.RequestRender();
            Dispatcher.UIThread.RunJobs();
            using var frame = window.CaptureRenderedFrame();
            Thread.Sleep(10);
        }
    }

    private static void PumpUntil(Task task, TimeSpan timeout)
    {
        var timer = Stopwatch.StartNew();
        while (!task.IsCompleted)
        {
            if (timer.Elapsed >= timeout)
            {
                throw new TimeoutException(
                    $"The headless native Monaco operation exceeded {timeout}.");
            }
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }
        task.GetAwaiter().GetResult();
    }

    private static void SaveNativeFrame(
        NativeSceneSurface surface,
        string path)
    {
        var png = surface.CaptureRetainedScenePng();
        File.WriteAllBytes(path, png);
        using var stream = new MemoryStream(png);
        using var frame = new Bitmap(stream);
        if (frame.PixelSize != new PixelSize(Width, Height))
        {
            throw new InvalidOperationException(
                $"Unexpected capture size {frame.PixelSize}.");
        }
    }

    private sealed record CaptureOptions(
        string NativeLibraryPath,
        string OutputDirectory)
    {
        internal static CaptureOptions Parse(IReadOnlyList<string> args)
        {
            string? nativeLibrary = null;
            var output = Path.GetFullPath("artifacts/monaco-headless");
            for (var index = 0; index < args.Count; index++)
            {
                switch (args[index])
                {
                    case "--native-library" when index + 1 < args.Count:
                        nativeLibrary = Path.GetFullPath(args[++index]);
                        break;
                    case "--output" when index + 1 < args.Count:
                        output = Path.GetFullPath(args[++index]);
                        break;
                    default:
                        throw new ArgumentException(
                            $"Unknown or incomplete argument '{args[index]}'.");
                }
            }

            if (nativeLibrary is null)
            {
                throw new ArgumentException("--native-library is required.");
            }
            return new CaptureOptions(nativeLibrary, output);
        }
    }
}
