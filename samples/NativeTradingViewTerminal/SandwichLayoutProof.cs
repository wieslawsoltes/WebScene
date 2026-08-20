using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Threading;
using WebScene.Backends.Avalonia.Native;

namespace NativeTradingViewTerminal;

internal static class SandwichLayoutProof
{
    internal static int Run(string[] arguments)
    {
        var paths = SamplePaths.Resolve(arguments);
        var output = ReadOutput(arguments);
        var width = ReadDimension(arguments, "--width", 1100);
        var height = ReadDimension(arguments, "--height", 900);
        var useCompositionVisual = arguments.Contains(
            "--composition",
            StringComparer.Ordinal);
        Directory.CreateDirectory(output);
        var view = new NativeWebSceneView(useCompositionVisual);
        var window = new Window
        {
            Width = width,
            Height = height,
            Content = view,
            Background = Avalonia.Media.Brushes.Black
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            PumpUntil(
                view.LoadAsync(
                    paths.DocumentUrl,
                    paths.NativeLibraryPath,
                    paths.CompilationCacheDirectory),
                TimeSpan.FromSeconds(90));
            Evaluate(view, InstallDeterministicBridgeScript);
            Evaluate(view, InitializeInstrumentScript);

            WaitUntil(
                view,
                window,
                """
                (() => {
                  const frame = document.querySelector('#chart_container iframe');
                  return Boolean(
                    window.tvWidget
                    && frame?.contentDocument?.querySelectorAll('canvas').length >= 6);
                })()
                """,
                TimeSpan.FromSeconds(60));
            PumpFrames(view, window, TimeSpan.FromSeconds(3));

            var initial = Evaluate(view, DiagnosticScript);
            File.WriteAllText(Path.Combine(output, "initial-layout.json"), initial);
            SaveFrame(view, Path.Combine(output, "initial-layout.png"));
            ClickElement(view, window, """
                (() => {
                  const chartDocument = document.querySelector(
                    '#chart_container iframe')?.contentDocument;
                  return Array.from(
                    chartDocument?.querySelectorAll('button') ?? [])
                    .find(candidate =>
                      candidate.getAttribute('aria-label') === 'Layout setup'
                      && getComputedStyle(candidate).visibility === 'visible');
                })()
                """);
            PumpFrames(view, window, TimeSpan.FromSeconds(1));
            var popup = Evaluate(view, DiagnosticScript);
            File.WriteAllText(Path.Combine(output, "layout-popup.json"), popup);
            SaveFrame(view, Path.Combine(output, "layout-popup.png"));

            ClickElement(view, window, LayoutButtonExpression("4"));
            WaitUntil(
                view,
                window,
                """
                (() => document.querySelector('#chart_container iframe')
                  ?.contentDocument?.querySelectorAll(
                    'canvas[data-name="pane-canvas"]').length === 4)()
                """,
                TimeSpan.FromSeconds(20));
            PumpFrames(view, window, TimeSpan.FromSeconds(2));
            var fourChart = Evaluate(view, DiagnosticScript);
            File.WriteAllText(Path.Combine(output, "four-chart-layout.json"), fourChart);
            SaveFrame(view, Path.Combine(output, "four-chart-layout.png"));
            ValidateFourChartGeometry(fourChart);

            ClickElement(view, window, LayoutSetupButtonExpression);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(500));
            ClickElement(view, window, LayoutButtonExpression("s"));
            WaitUntil(
                view,
                window,
                """
                (() => document.querySelector('#chart_container iframe')
                  ?.contentDocument?.querySelectorAll(
                    'canvas[data-name="pane-canvas"]').length === 1)()
                """,
                TimeSpan.FromSeconds(20));
            PumpFrames(view, window, TimeSpan.FromSeconds(2));
            var singleChart = Evaluate(view, DiagnosticScript);
            File.WriteAllText(Path.Combine(output, "single-chart-restored.json"), singleChart);
            SaveFrame(view, Path.Combine(output, "single-chart-restored.png"));
            ValidateSingleChartRestored(singleChart);

            Console.WriteLine(singleChart);
            Console.WriteLine($"Evidence: {output}");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
        finally
        {
            PumpUntil(view.DisposeAsync().AsTask(), TimeSpan.FromSeconds(10));
            window.Close();
        }
    }

    private const string DiagnosticScript = """
        (() => {
          const frame = document.querySelector('#chart_container iframe');
          const chartDocument = frame?.contentDocument;
          if (!chartDocument) return { error: 'chart frame unavailable' };
          const rect = node => {
            const bounds = node.getBoundingClientRect();
            const style = getComputedStyle(node);
            return {
              tag: node.tagName,
              className: String(node.className || ''),
              ariaLabel: node.getAttribute('aria-label'),
              title: node.getAttribute('title'),
              dataName: node.getAttribute('data-name'),
              text: String(node.textContent || '').trim().slice(0, 80),
              x: bounds.x,
              y: bounds.y,
              width: bounds.width,
              height: bounds.height,
              display: style.display,
              position: style.position,
              visibility: style.visibility
            };
          };
          return {
            url: location.href,
            frameUrl: frame.contentWindow.location.href,
            frameRect: rect(frame),
            bodyRect: rect(chartDocument.body),
            canvasCount: chartDocument.querySelectorAll('canvas').length,
            layoutMenuCount: Array.from(chartDocument.querySelectorAll(
                '[data-name="layouts-list"]')).filter(node => {
              const bounds = node.getBoundingClientRect();
              const style = getComputedStyle(node);
              return bounds.width > 0 && bounds.height > 0
                && style.display !== 'none' && style.visibility !== 'hidden';
            }).length,
            layoutSetupOpened: Array.from(
                chartDocument.querySelectorAll('button'))
              .some(node => node.getAttribute('aria-label') === 'Layout setup'
                && getComputedStyle(node).visibility === 'visible'
                && node.className.includes('isOpened-')),
            paneCanvases: Array.from(chartDocument.querySelectorAll(
                'canvas[data-name="pane-canvas"]')).map(canvas => {
              const ancestors = [];
              for (let node = canvas.parentElement;
                   node && ancestors.length < 8;
                   node = node.parentElement) {
                ancestors.push(rect(node));
              }
              return { canvas: rect(canvas), ancestors };
            }),
            canvases: Array.from(chartDocument.querySelectorAll('canvas'))
              .map(rect),
            buttons: Array.from(chartDocument.querySelectorAll('button'))
              .map(rect)
              .filter(item => item.width > 0 && item.height > 0),
            dialogs: Array.from(chartDocument.querySelectorAll(
                '[role="dialog"], [role="menu"], [data-name*="layout"]'))
              .map(rect)
              .filter(item => item.width > 0 && item.height > 0)
          };
        })()
        """;

    private static void ValidateFourChartGeometry(string evidence)
    {
        using var document = JsonDocument.Parse(evidence);
        var root = document.RootElement;
        var panes = root.GetProperty("paneCanvases")
            .EnumerateArray()
            .Select(item => new
            {
                Canvas = item.GetProperty("canvas"),
                Container = item.GetProperty("ancestors")
                    .EnumerateArray()
                    .First(ancestor =>
                        ancestor.GetProperty("className").GetString()
                            ?.Split(' ', StringSplitOptions.RemoveEmptyEntries)
                            .Contains("chart-container") == true)
            })
            .OrderBy(item => item.Container.GetProperty("y").GetDouble())
            .ThenBy(item => item.Container.GetProperty("x").GetDouble())
            .ToArray();
        if (panes.Length != 4)
        {
            throw new InvalidOperationException(
                $"TradingView did not materialize four pane canvases: {evidence}");
        }

        var left = panes[0];
        var right = panes[1];
        var leftContainerRight = left.Container.GetProperty("x").GetDouble()
            + left.Container.GetProperty("width").GetDouble();
        var separator = right.Container.GetProperty("x").GetDouble()
            - leftContainerRight;
        var priceScaleWidth = left.Container.GetProperty("width").GetDouble()
            - left.Canvas.GetProperty("width").GetDouble();
        if (separator is < 0 or > 2.1
            || priceScaleWidth is < 60 or > 90
            || root.GetProperty("layoutMenuCount").GetInt32() != 0)
        {
            throw new InvalidOperationException(
                "TradingView four-pane geometry did not match the expected "
                + $"2px separator and retained price scale: {evidence}");
        }
    }

    private static void ValidateSingleChartRestored(string evidence)
    {
        using var document = JsonDocument.Parse(evidence);
        var root = document.RootElement;
        if (root.GetProperty("paneCanvases").GetArrayLength() != 1
            || root.GetProperty("layoutMenuCount").GetInt32() != 0
            || root.GetProperty("layoutSetupOpened").GetBoolean())
        {
            throw new InvalidOperationException(
                "TradingView did not close the layout menu while restoring "
                + $"the single-chart layout: {evidence}");
        }
    }

    private const string LayoutSetupButtonExpression = """
        (() => {
          const chartDocument = document.querySelector(
            '#chart_container iframe')?.contentDocument;
          return Array.from(chartDocument?.querySelectorAll('button') ?? [])
            .find(candidate =>
              candidate.getAttribute('aria-label') === 'Layout setup'
              && getComputedStyle(candidate).visibility === 'visible');
        })()
        """;

    private static string LayoutButtonExpression(string layout) => $$"""
        (() => {
          const chartDocument = document.querySelector(
            '#chart_container iframe')?.contentDocument;
          return Array.from(chartDocument?.querySelectorAll(
              '[data-name="layouts-list"] button') ?? [])
            .find(candidate => candidate.getAttribute('aria-label') === '{{layout}}');
        })()
        """;

    private const string InstallDeterministicBridgeScript = """
        (() => {
          const resolutionSeconds = resolution => {
            const value = String(resolution || '60').toUpperCase();
            if (value === 'D') return 86400;
            if (value === 'W') return 604800;
            if (value === 'M') return 2592000;
            return Math.max(60, Number(value) * 60 || 3600);
          };
          window.dotnetBridge = {
            ExecuteTradingViewPersistenceCommand(id, operation) {
              let result = null;
              if (operation === 'bootstrap') {
                result = {
                  settings: {},
                  lastOpenedLayoutId: null,
                  layoutCount: 0
                };
              } else if (operation.startsWith('getAll')
                  || operation === 'getDrawingTemplates') {
                result = [];
              } else if (operation.startsWith('save')) {
                result = 'webscene-proof';
              }
              setTimeout(() => window.onTradingViewPersistenceResult(
                id, true, result, null), 0);
            },
            GetBars(_symbol, resolution, from, to, requestId) {
              const step = resolutionSeconds(resolution);
              const end = Math.floor(Number(to) / step) * step;
              const start = Math.max(Number(from), end - step * 280);
              const bars = [];
              for (let time = start, index = 0; time <= end; time += step, index++) {
                const center = 63750
                  + Math.sin(index / 11) * 780
                  + Math.cos(index / 29) * 320;
                const open = center + Math.sin(index * 1.7) * 90;
                const close = center + Math.cos(index * 1.3) * 90;
                bars.push({
                  time: time * 1000,
                  open,
                  high: Math.max(open, close) + 140,
                  low: Math.min(open, close) - 140,
                  close,
                  volume: 1000 + (index % 23) * 137
                });
              }
              setTimeout(() => window.onHistoryResponse(requestId, bars), 0);
            },
            SubscribeBars() {},
            UnsubscribeBars() {},
            RequestTradingState() {}
          };
          return true;
        })()
        """;

    private const string InitializeInstrumentScript = """
        window.onInstrumentChanged(
          'BTC-USDT',
          'BTC-USDT Perp USDT',
          'OKX',
          0.1,
          ['1', '5', '15', '60', '240', 'D'],
          null,
          {
            quantityStep: 0.001,
            minimumQuantity: 0.001,
            maximumQuantity: 1000,
            quantityUnit: 'BTC',
            baseCurrency: 'BTC',
            quoteCurrency: 'USDT',
            lastPrice: 63750
          });
        true
        """;

    private static void WaitUntil(
        NativeWebSceneView view,
        Window window,
        string predicate,
        TimeSpan timeout)
    {
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < timeout)
        {
            PumpFrames(view, window, TimeSpan.FromMilliseconds(200));
            if (Evaluate(view, predicate) == "true")
            {
                return;
            }
        }
        throw new TimeoutException(
            $"The Sandwich TradingView layout proof exceeded {timeout}.");
    }

    private static string Evaluate(NativeWebSceneView view, string script)
    {
        var task = view.EvaluateTextAsync(script);
        PumpUntil(task, TimeSpan.FromSeconds(15));
        return task.Result;
    }

    private static void ClickElement(
        NativeWebSceneView view,
        Window window,
        string elementExpression)
    {
        var geometry = Evaluate(view, $$"""
            (() => {
              const node = {{elementExpression}};
              if (!node) return null;
              const rect = node.getBoundingClientRect();
              return {
                x: rect.x + rect.width / 2,
                y: rect.y + rect.height / 2
              };
            })()
            """);
        using var document = JsonDocument.Parse(geometry);
        if (document.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException(
                $"The requested TradingView element was unavailable: {geometry}");
        }
        var x = document.RootElement.GetProperty("x").GetDouble();
        var y = document.RootElement.GetProperty("y").GetDouble();
        var surface = (NativeSceneSurface)view.Content!;
        surface.SubmitAvaloniaPointerMove(x, y);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
        surface.SubmitPointerButton(2, x, y, 0, pressed: true);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(50));
        surface.SubmitPointerButton(3, x, y, 0, pressed: false);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(250));
    }

    private static void PumpFrames(
        NativeWebSceneView view,
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
                throw new TimeoutException($"The task exceeded {timeout}.");
            }
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }
        task.GetAwaiter().GetResult();
    }

    private static void SaveFrame(NativeWebSceneView view, string path)
    {
        var surface = (NativeSceneSurface)view.Content!;
        File.WriteAllBytes(path, surface.CaptureRetainedScenePng());
    }

    private static string ReadOutput(IReadOnlyList<string> arguments)
    {
        for (var index = 0; index + 1 < arguments.Count; ++index)
        {
            if (arguments[index] == "--output")
            {
                return Path.GetFullPath(arguments[index + 1]);
            }
        }
        return Path.GetFullPath("artifacts/sandwich-layout-proof");
    }

    private static int ReadDimension(
        IReadOnlyList<string> arguments,
        string name,
        int fallback)
    {
        for (var index = 0; index + 1 < arguments.Count; ++index)
        {
            if (arguments[index] == name
                && int.TryParse(arguments[index + 1], out var value)
                && value > 0)
            {
                return value;
            }
        }
        return fallback;
    }
}
