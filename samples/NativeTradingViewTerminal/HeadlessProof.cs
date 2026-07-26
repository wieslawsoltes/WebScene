using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using HtmlML.Backends.Avalonia.Native;
using SkiaSharp;

namespace NativeTradingViewTerminal;

internal static class HeadlessProof
{
    private const int Width = 1440;
    private const int Height = 900;

    internal static int Run(string[] arguments)
    {
        var paths = SamplePaths.Resolve(arguments);
        var output = ReadOutput(arguments);
        Directory.CreateDirectory(output);
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
            PumpUntil(
                view.LoadAsync(
                    SamplePaths.TerminalUrl,
                    paths.NativeLibraryPath,
                    paths.CompilationCacheDirectory),
                TimeSpan.FromSeconds(90));

            _ = WaitForWebSocketEvidence(view, window);
            InstallPointerCertification(view);
            var surface = (NativeSceneSurface)view.Content!;
            surface.SubmitAvaloniaPointerMove(700, 350);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(250));
            surface.SubmitPointerButton(
                kind: 2,
                x: 700,
                y: 350,
                button: 0,
                pressed: true);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
            surface.SubmitPointerButton(
                kind: 3,
                x: 700,
                y: 350,
                button: 0,
                pressed: false);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(650));
            var evidence = WaitForWebSocketEvidence(view, window);
            PumpFrames(view, window, TimeSpan.FromSeconds(3));
            var rendererMetrics = surface.GetRendererMemoryMetrics();
            if (rendererMetrics.RetainedCommandCount < 100
                || rendererMetrics.DomCommandCount < 100)
            {
                throw new InvalidOperationException(
                    "TradingView did not publish a substantial native scene: "
                    + rendererMetrics);
            }
            var screenshotPath = Path.Combine(
                output,
                "native-tradingview-terminal.png");
            SaveNativeFrame(surface, screenshotPath);
            var evidencePath = Path.Combine(
                output,
                "native-tradingview-terminal-evidence.json");
            File.WriteAllText(evidencePath, evidence);

            using var document = JsonDocument.Parse(evidence);
            var root = document.RootElement;
            var websocket = root.GetProperty("websocket");
            if (root.GetProperty("webSocketType").GetString() != "function"
                || !root.GetProperty("hasWidget").GetBoolean()
                || websocket.GetProperty("created").GetInt32() < 1
                || websocket.GetProperty("opened").GetInt32() < 1
                || websocket.GetProperty("messages").GetInt32() < 1
                || websocket.GetProperty("bytesReceived").GetInt64() < 1)
            {
                throw new InvalidOperationException(
                    "TradingView did not produce a live native WebSocket data flow: "
                    + evidence);
            }
            var layout = root.GetProperty("layoutProbe");
            var pointerInput = root.GetProperty("pointerInput");
            var pointerTarget =
                layout.GetProperty("pointerTarget")[0];
            var rightRect = layout.GetProperty("right").GetProperty("rect");
            var toolbarRect = layout.GetProperty("toolbar").GetProperty("rect");
            var watchlistRowRect = layout.GetProperty("watchlistRow").GetProperty("rect");
            var visual = layout.GetProperty("visualCertification");
            var sidebar = visual.GetProperty("sidebar");
            var pagesRect = sidebar.GetProperty("pages").GetProperty("rect");
            var activePageRect = sidebar.GetProperty("activePage").GetProperty("rect");
            var detailRect = sidebar.GetProperty("detail").GetProperty("rect");
            var summaryBodyRect = sidebar.GetProperty("body").GetProperty("rect");
            var divider = sidebar.GetProperty("divider");
            var logos = visual.GetProperty("logos");
            var documentation = visual.GetProperty("documentation");
            var documentationRect = documentation.GetProperty("rect");
            var marketStatus = visual.GetProperty("marketStatus");
            var statusPill = marketStatus.GetProperty("pill");
            var statusPillRect = statusPill.GetProperty("rect");
            var topSeparators = visual.GetProperty("topSeparators");
            var rightToolbar = visual.GetProperty("rightToolbar");
            var activeRail = rightToolbar.GetProperty("active");
            var inactiveRail = rightToolbar.GetProperty("inactive");
            var activeRailClass =
                activeRail.GetProperty("className").GetString() ?? "";
            var rightEdge = rightRect.GetProperty("x").GetDouble()
                + rightRect.GetProperty("width").GetDouble();
            var toolbarEdge = toolbarRect.GetProperty("x").GetDouble()
                + toolbarRect.GetProperty("width").GetDouble();
            static double Bottom(JsonElement rect) =>
                rect.GetProperty("y").GetDouble()
                + rect.GetProperty("height").GetDouble();
            if (!layout.GetProperty("ready").GetBoolean()
                || layout.GetProperty("widgetbarWrap").GetProperty("position").GetString()
                    != "absolute"
                || layout.GetProperty("widgetbarTabs").GetProperty("position").GetString()
                    != "absolute"
                || Math.Abs(rightEdge - toolbarEdge) > 1
                || Math.Abs(
                    rightRect.GetProperty("height").GetDouble()
                    - toolbarRect.GetProperty("height").GetDouble()) > 1
                || watchlistRowRect.GetProperty("height").GetDouble() is < 29 or > 32
                || layout.GetProperty("symbol").GetString() != "AAPL"
                || layout.GetProperty("hiddenVolumeDisplay").GetString() != "none"
                || sidebar.GetProperty("summarySymbol").GetString() != "AAPL"
                || Math.Abs(Bottom(detailRect) - Bottom(activePageRect)) > 1
                || Math.Abs(Bottom(summaryBodyRect) - Bottom(activePageRect)) > 1
                || Math.Abs(
                    detailRect.GetProperty("width").GetDouble()
                    - activePageRect.GetProperty("width").GetDouble()) > 1
                || Math.Abs(
                    pagesRect.GetProperty("width").GetDouble()
                    - activePageRect.GetProperty("width").GetDouble() - 2) > 1
                || divider.GetProperty("leftWidth").GetString() != "1px"
                || divider.GetProperty("rightWidth").GetString() != "1px"
                || logos.GetProperty("count").GetInt32() < 12
                || logos.GetProperty("loaded").GetInt32()
                    != logos.GetProperty("count").GetInt32()
                || documentationRect.GetProperty("height").GetDouble() is < 27.5 or > 28.5
                || documentationRect.GetProperty("width").GetDouble() < 110
                || documentation.GetProperty("paddingLeft").GetString() != "12px"
                || documentation.GetProperty("paddingRight").GetString() != "12px"
                || documentation.GetProperty("paddingTop").GetString() != "5px"
                || documentation.GetProperty("paddingBottom").GetString() != "5px"
                || statusPill.GetProperty("display").GetString() != "inline-flex"
                || statusPillRect.GetProperty("width").GetDouble() < 54
                || marketStatus.GetProperty("itemCount").GetInt32() != 3
                || topSeparators.GetProperty("count").GetInt32() < 7
                || !activeRailClass.Contains("isActive-", StringComparison.Ordinal)
                || pointerTarget.GetProperty("tag").GetString() != "CANVAS"
                || pointerTarget.GetProperty("cursor").GetString() != "crosshair"
                || pointerInput.GetProperty("pointermove").GetInt32() < 1
                || pointerInput.GetProperty("mousemove").GetInt32() < 1
                || pointerInput.GetProperty("pointerdown").GetInt32() < 1
                || pointerInput.GetProperty("mousedown").GetInt32() < 1
                || pointerInput.GetProperty("pointerup").GetInt32() < 1
                || pointerInput.GetProperty("mouseup").GetInt32() < 1
                || pointerInput.GetProperty("click").GetInt32() < 1
                || pointerInput.GetProperty("lastTarget").GetString() != "CANVAS")
            {
                throw new InvalidOperationException(
                    "TradingView layout or nested-frame pointer input did not "
                    + $"match the certified structure: {layout}; {pointerInput}");
            }
            ValidateSelectedToolbarBackground(
                screenshotPath,
                activeRail.GetProperty("rect"),
                inactiveRail.GetProperty("rect"));

            Console.WriteLine($"TradingView evidence: {evidence}");
            Console.WriteLine($"Evidence JSON: {evidencePath}");
            Console.WriteLine($"Screenshot: {screenshotPath}");
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

    private static void InstallPointerCertification(NativeHtmlMlView view)
    {
        var task = view.EvaluateJsonAsync("""
            (() => {
              const chartWindow = Array.from(
                document.querySelectorAll('iframe'))
                .map(frame => frame.contentWindow)
                .find(candidate =>
                  candidate?.document?.querySelectorAll('canvas').length >= 8);
              if (!chartWindow) return false;
              const state = {
                pointermove: 0,
                mousemove: 0,
                pointerdown: 0,
                mousedown: 0,
                pointerup: 0,
                mouseup: 0,
                click: 0,
                lastTarget: null
              };
              for (const type of [
                'pointermove', 'mousemove',
                'pointerdown', 'mousedown',
                'pointerup', 'mouseup', 'click'
              ]) {
                chartWindow.document.addEventListener(type, event => {
                  state[type]++;
                  state.lastTarget = event.target?.tagName ?? null;
                }, true);
              }
              chartWindow.__htmlMlPointerCertification = state;
              return true;
            })()
            """);
        PumpUntil(task, TimeSpan.FromSeconds(10));
        if (task.Result != "true")
        {
            throw new InvalidOperationException(
                "TradingView chart frame was unavailable for pointer certification.");
        }
    }

    private static string WaitForWebSocketEvidence(
        NativeHtmlMlView view,
        Window window)
    {
        var timer = Stopwatch.StartNew();
        string evidence = "{}";
        while (timer.Elapsed < TimeSpan.FromSeconds(45))
        {
            PumpFrames(view, window, TimeSpan.FromMilliseconds(250));
            var evaluation = view.EvaluateJsonAsync("""
                ({
                  url: location.href,
                  title: document.title,
                  readyState: document.readyState,
                  webSocketType: typeof WebSocket,
                  websocket: [
                    globalThis,
                    ...Array.from(document.querySelectorAll('iframe'))
                      .map(frame => frame.contentWindow)
                  ]
                    .map(realm =>
                      realm?.__htmlMlWebSocketDiagnostics?.() ?? null)
                    .filter(Boolean)
                    .reduce((total, current) => ({
                      created: total.created + current.created,
                      opened: total.opened + current.opened,
                      messages: total.messages + current.messages,
                      bytesReceived:
                        total.bytesReceived + current.bytesReceived,
                      errors: total.errors + current.errors,
                      closed: total.closed + current.closed
                    }), {
                      created: 0, opened: 0, messages: 0,
                      bytesReceived: 0, errors: 0, closed: 0
                    }),
                  tradingViewType: typeof globalThis.TradingView,
                  widgetType: typeof globalThis.TradingView?.widget,
                  brokersType: typeof globalThis.Brokers,
                  initType: typeof initOnReady,
                  hasWidget: Boolean(globalThis.tvWidget),
                  pointerInput: Array.from(
                    document.querySelectorAll('iframe'))
                    .map(frame =>
                      frame.contentWindow?.__htmlMlPointerCertification ?? null)
                    .find(Boolean) ?? null,
                  bodyTextLength: document.body?.innerText?.length ?? 0,
                  elementCount: document.querySelectorAll('*').length,
                  canvasCount: document.querySelectorAll('canvas').length,
                  layoutProbe: (() => {
                    const chartDocument = Array.from(
                      document.querySelectorAll('iframe'))
                      .map(frame => frame.contentDocument)
                      .find(candidate =>
                        candidate?.querySelectorAll('canvas').length >= 8);
                    if (!chartDocument) return null;
                    const right = chartDocument.querySelector(
                      '.layout__area--right');
                    const watchlistSymbol = Array.from(
                      right?.querySelectorAll('*') ?? [])
                      .find(node => node.children.length === 0
                        && node.textContent?.trim() === 'AAPL'
                        && node.closest('[class^="symbol-"]'));
                    const watchlistRow = watchlistSymbol?.closest(
                      '[class^="symbol-"]');
                    const summarize = node => {
                      if (!node) return null;
                      const rect = node.getBoundingClientRect();
                      const style = getComputedStyle(node);
                      return {
                        className: node.className,
                        rect: {
                          x: rect.x, y: rect.y,
                          width: rect.width, height: rect.height
                        },
                        display: style.display,
                        position: style.position
                      };
                    };
                    const widgetbarWrap = right?.querySelector(
                      '.widgetbar-wrap');
                    const widgetbarTabs = right?.querySelector(
                      '.widgetbar-tabs');
                    const toolbar = right?.querySelector(
                      '[class^="toolbar-"]');
                    const volume = watchlistRow?.querySelector(
                      '[class*="volume-"]');
                    const pointerTarget = chartDocument.elementFromPoint(
                      700, 350);
                    const describeVisual = node => {
                      if (!node) return null;
                      const rect = node.getBoundingClientRect();
                      const style = getComputedStyle(node);
                      return {
                        tag: node.tagName,
                        className: node.className,
                        rect: {
                          x: rect.x, y: rect.y,
                          width: rect.width, height: rect.height
                        },
                        display: style.display,
                        backgroundColor: style.backgroundColor
                      };
                    };
                    const rightAapls = Array.from(
                      right?.querySelectorAll('*') ?? [])
                      .filter(node =>
                        node.children.length === 0
                        && node.textContent?.trim() === 'AAPL');
                    const summaryAapl = rightAapls.find(node =>
                      node.getBoundingClientRect().y > 400);
                    const documentation = Array.from(
                      chartDocument.querySelectorAll('*'))
                      .filter(node =>
                        node.textContent?.trim() === 'Documentation')
                      .sort((left, right) =>
                        left.getBoundingClientRect().width
                        - right.getBoundingClientRect().width)[0];
                    const cboeOneNodes = Array.from(
                      chartDocument.querySelectorAll('*'))
                      .filter(node =>
                        node.textContent?.trim() === 'Cboe One');
                    const cboeOne = cboeOneNodes.find(node =>
                      node.getBoundingClientRect().y < 100);
                    const chartStatus = Array.from(
                      chartDocument.querySelectorAll(
                        '[class*="statusesWrapper"]'))
                      .find(node =>
                        node.getBoundingClientRect().y < 100);
                    const widgetbarPages = right?.querySelector(
                      '.widgetbar-pages');
                    const activePage = right?.querySelector(
                      '.widgetbar-page.active');
                    const detailWidget = right?.querySelector(
                      '.widgetbar-widget-detail');
                    const summaryBody = detailWidget?.querySelector(
                      '.widgetbar-widgetbody');
                    const logos = Array.from(
                      right?.querySelectorAll('img') ?? []);
                    const statusPill = chartStatus?.querySelector(
                      '[data-role="statuses-pill"]');
                    const statusItems = Array.from(
                      statusPill?.querySelectorAll('[class*="statusItem"]')
                        ?? []);
                    const railButtons = Array.from(
                      toolbar?.querySelectorAll('button') ?? []);
                    const activeRailButton = railButtons.find(node =>
                      node.className.includes('isActive-'));
                    const inactiveRailButton = railButtons.find(node =>
                      node !== activeRailButton);
                    const separators = Array.from(
                      chartDocument.querySelectorAll(
                        '[class*="separator-xVh"]'))
                      .filter(node => {
                        const rect = node.getBoundingClientRect();
                        return rect.y < 42 && rect.width >= 0.9
                          && rect.height >= 20;
                      });
                    return {
                      ready: Boolean(
                        right && widgetbarWrap && widgetbarTabs
                        && toolbar && watchlistRow && volume),
                      right: summarize(right),
                      widgetbarWrap: summarize(widgetbarWrap),
                      widgetbarTabs: summarize(widgetbarTabs),
                      toolbar: summarize(toolbar),
                      watchlistRow: summarize(watchlistRow),
                      symbol: watchlistSymbol?.textContent?.trim() ?? null,
                      hiddenVolumeDisplay: volume
                        ? getComputedStyle(volume).display : null,
                      visualCertification: {
                        sidebar: {
                          pages: describeVisual(widgetbarPages),
                          activePage: describeVisual(activePage),
                          detail: describeVisual(detailWidget),
                          body: describeVisual(summaryBody),
                          summarySymbol:
                            summaryAapl?.textContent?.trim() ?? null,
                          divider: widgetbarPages ? (() => {
                            const style = getComputedStyle(widgetbarPages);
                            return {
                              leftWidth: style.borderLeftWidth,
                              rightWidth: style.borderRightWidth,
                              leftColor: style.borderLeftColor,
                              rightColor: style.borderRightColor
                            };
                          })() : null
                        },
                        logos: {
                          count: logos.length,
                          loaded: logos.filter(node =>
                            Boolean(node.currentSrc)
                            && node.naturalWidth > 0
                            && node.naturalHeight > 0).length,
                          sources: logos.map(node => node.currentSrc)
                        },
                        documentation: documentation ? (() => {
                          const style = getComputedStyle(documentation);
                          return {
                            ...describeVisual(documentation),
                            paddingLeft: style.paddingLeft,
                            paddingRight: style.paddingRight,
                            paddingTop: style.paddingTop,
                            paddingBottom: style.paddingBottom,
                            lineHeight: style.lineHeight,
                            borderRadius: style.borderRadius
                          };
                        })() : null,
                        marketStatus: {
                          cboe: describeVisual(cboeOne),
                          wrapper: describeVisual(chartStatus),
                          pill: describeVisual(statusPill),
                          itemCount: statusItems.length
                        },
                        topSeparators: {
                          count: separators.length,
                          items: separators.map(describeVisual)
                        },
                        rightToolbar: {
                          active: describeVisual(activeRailButton),
                          inactive: describeVisual(inactiveRailButton)
                        }
                      },
                      pointerTarget: (() => {
                        const result = [];
                        for (let node = pointerTarget;
                             node && result.length < 8;
                             node = node.parentElement) {
                          const rect = node.getBoundingClientRect();
                          result.push({
                            tag: node.tagName,
                            className: node.className,
                            cursor: getComputedStyle(node).cursor,
                            pointerEvents:
                              getComputedStyle(node).pointerEvents,
                            rect: {
                              x: rect.x, y: rect.y,
                              width: rect.width, height: rect.height
                            }
                          });
                        }
                        return result;
                      })()
                    };
                  })(),
                  frames: Array.from(document.querySelectorAll('iframe')).map(frame => ({
                    src: frame.src,
                    remoteResult:
                      frame.getAttribute('data-htmlml-remote-result'),
                    frameError:
                      frame.getAttribute('data-htmlml-frame-error'),
                    websocket:
                      frame.contentWindow
                        ?.__htmlMlWebSocketDiagnostics?.() ?? null,
                    rect: (() => {
                      const rect = frame.getBoundingClientRect();
                      return {
                        x: rect.x, y: rect.y,
                        width: rect.width, height: rect.height
                      };
                    })(),
                    elementCount:
                      frame.contentDocument?.querySelectorAll('*').length ?? 0,
                    canvasCount:
                      frame.contentDocument?.querySelectorAll('canvas').length ?? 0,
                    readyState:
                      frame.contentDocument?.readyState ?? null
                  }))
                })
                """);
            PumpUntil(evaluation, TimeSpan.FromSeconds(10));
            evidence = evaluation.Result;
            using var document = JsonDocument.Parse(evidence);
            var websocket = document.RootElement.GetProperty("websocket");
            var layout = document.RootElement.GetProperty("layoutProbe");
            if (websocket.ValueKind == JsonValueKind.Object
                && websocket.GetProperty("opened").GetInt32() > 0
                && websocket.GetProperty("messages").GetInt32() >= 5
                && websocket.GetProperty("bytesReceived").GetInt64() >= 500
                && document.RootElement.GetProperty("elementCount").GetInt32() >= 1000
                && document.RootElement.GetProperty("canvasCount").GetInt32() >= 8
                && layout.ValueKind == JsonValueKind.Object
                && layout.GetProperty("ready").GetBoolean())
            {
                return evidence;
            }
        }
        return evidence;
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
                    $"The TradingView proof exceeded {timeout}.");
            }
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }
        task.GetAwaiter().GetResult();
    }

    private static void SaveNativeFrame(NativeSceneSurface surface, string path)
    {
        var png = surface.CaptureRetainedScenePng();
        File.WriteAllBytes(path, png);
        using var stream = new MemoryStream(png);
        using var frame = new Bitmap(stream);
        if (frame.PixelSize != new PixelSize(Width, Height))
        {
            throw new InvalidOperationException(
                $"Unexpected TradingView capture size {frame.PixelSize}.");
        }
        using var bitmap = SKBitmap.Decode(png)
            ?? throw new InvalidOperationException(
                "TradingView capture was not a valid PNG.");
        var colors = new HashSet<uint>();
        for (var y = 0; y < bitmap.Height; y += 8)
        {
            for (var x = 0; x < bitmap.Width; x += 8)
            {
                colors.Add((uint)bitmap.GetPixel(x, y));
            }
        }
        if (colors.Count < 64)
        {
            throw new InvalidOperationException(
                $"TradingView capture was blank or uniform ({colors.Count} sampled colors).");
        }
    }

    private static void ValidateSelectedToolbarBackground(
        string screenshotPath,
        JsonElement activeRect,
        JsonElement inactiveRect)
    {
        using var bitmap = SKBitmap.Decode(File.ReadAllBytes(screenshotPath))
            ?? throw new InvalidOperationException(
                "TradingView capture could not be decoded for toolbar certification.");
        static int SampleCoordinate(JsonElement rect, string axis) =>
            (int)Math.Round(rect.GetProperty(axis).GetDouble() + 7);
        var x = SampleCoordinate(activeRect, "x");
        var active = bitmap.GetPixel(x, SampleCoordinate(activeRect, "y"));
        var inactive = bitmap.GetPixel(x, SampleCoordinate(inactiveRect, "y"));
        var distance = Math.Abs(active.Red - inactive.Red)
            + Math.Abs(active.Green - inactive.Green)
            + Math.Abs(active.Blue - inactive.Blue);
        if (distance < 12)
        {
            throw new InvalidOperationException(
                "The active right-toolbar button did not paint a selected "
                + $"background (active={active}, inactive={inactive}).");
        }
    }

    private static string ReadOutput(IReadOnlyList<string> arguments)
    {
        for (var index = 0; index + 1 < arguments.Count; ++index)
        {
            if (arguments[index] == "--output") {
                return Path.GetFullPath(arguments[index + 1]);
            }
        }
        return Path.GetFullPath("artifacts/native-tradingview-terminal");
    }
}
