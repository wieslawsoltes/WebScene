using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using WebScene.Backends.Avalonia.Native;
using SkiaSharp;

namespace NativeTradingViewTerminal;

internal static class HeadlessProof
{
    internal static int Run(string[] arguments)
    {
        var paths = SamplePaths.Resolve(arguments);
        var output = ReadOutput(arguments);
        var width = ReadDimension(arguments, "--width", 1440);
        var height = ReadDimension(arguments, "--height", 900);
        var overlay = ReadArgument(arguments, "--open-overlay");
        var drawTrendline = arguments.Contains(
            "--draw-trendline",
            StringComparer.Ordinal);
        var certifyPopovers = arguments.Contains(
            "--popover-proof",
            StringComparer.Ordinal);
        Directory.CreateDirectory(output);
        var view = new NativeWebSceneView(useCompositionVisual: false);
        view.CaptureLegacyConsoleMessages = true;
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

            var initialEvidence = WaitForWebSocketEvidence(view, window);
            Console.WriteLine($"TradingView initial evidence: {initialEvidence}");
            Console.WriteLine($"TradingView last error: {view.LastError}");
            Console.WriteLine($"TradingView scene diagnostics: {view.SceneDiagnostics}");
            Console.WriteLine($"TradingView feature use: {view.FeatureUseReport}");
            Console.WriteLine(
                "TradingView console: "
                + JsonSerializer.Serialize(view.DrainConsoleMessages()));
            File.WriteAllText(
                Path.Combine(output, "first-iframe.html"),
                view.FirstIframeHtml);
            if (arguments.Contains("--toolbar-overflow-proof", StringComparer.Ordinal))
            {
                return CaptureToolbarOverflowEvidence(
                    view,
                    window,
                    output,
                    width,
                    height);
            }
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
            if (certifyPopovers)
            {
                surface.SubmitWheel(700, 350, -100);
                PumpFrames(view, window, TimeSpan.FromMilliseconds(500));
            }
            var evidence = WaitForWebSocketEvidence(view, window);
            TrendlineGeometry? trendlineGeometry = null;
            if (drawTrendline)
            {
                trendlineGeometry = DrawTrendline(view, window, surface);
                evidence = WaitForWebSocketEvidence(view, window);
                File.WriteAllText(
                    Path.Combine(output, "trendline-scene-diagnostics.json"),
                    view.SceneDiagnostics);
                File.WriteAllText(
                    Path.Combine(output, "trendline-feature-use.json"),
                    view.FeatureUseReport);
            }
            PumpFrames(view, window, TimeSpan.FromSeconds(3));
            var rendererMetrics = surface.GetRendererMemoryMetrics();
            if (rendererMetrics.RetainedCommandCount < 100
                || rendererMetrics.DomCommandCount < 100
                || rendererMetrics.SvgPictureCount < 8)
            {
                throw new InvalidOperationException(
                    "TradingView did not publish a substantial native scene: "
                    + rendererMetrics);
            }
            var screenshotPath = Path.Combine(
                output,
                "native-tradingview-terminal.png");
            SaveNativeFrame(surface, screenshotPath, width, height);
            var evidencePath = Path.Combine(
                output,
                "native-tradingview-terminal-evidence.json");
            File.WriteAllText(evidencePath, evidence);
            if (trendlineGeometry is not null)
            {
                ValidateTrendlineHandles(screenshotPath, trendlineGeometry.Value);
            }
            if (overlay is not null)
            {
                CaptureOverlay(
                    view,
                    window,
                    surface,
                    output,
                    overlay,
                    width,
                    height);
            }

            using var document = JsonDocument.Parse(evidence);
            var root = document.RootElement;
            var preferredColorScheme = root.GetProperty("preferredColorScheme");
            var websocket = root.GetProperty("websocket");
            if (!preferredColorScheme.GetProperty("dark").GetBoolean()
                || preferredColorScheme.GetProperty("light").GetBoolean()
                || root.GetProperty("webSocketType").GetString() != "function"
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
            var loadingIndicator = layout.GetProperty("loadingIndicator");
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
            var chartValuesCoach = visual.GetProperty("chartValuesCoach");
            var chartValuesCoachRect = chartValuesCoach.GetProperty("rect");
            var zoomCoach = visual.GetProperty("zoomCoach");
            var chartCanvasRect = pointerTarget.GetProperty("rect");
            var rightEdge = rightRect.GetProperty("x").GetDouble()
                + rightRect.GetProperty("width").GetDouble();
            var toolbarEdge = toolbarRect.GetProperty("x").GetDouble()
                + toolbarRect.GetProperty("width").GetDouble();
            static double Bottom(JsonElement rect) =>
                rect.GetProperty("y").GetDouble()
                + rect.GetProperty("height").GetDouble();
            static double Right(JsonElement rect) =>
                rect.GetProperty("x").GetDouble()
                + rect.GetProperty("width").GetDouble();
            var popoversMatch = !certifyPopovers;
            if (certifyPopovers && zoomCoach.ValueKind == JsonValueKind.Object)
            {
                var zoomChain = zoomCoach.GetProperty("chain");
                if (zoomChain.GetArrayLength() >= 3)
                {
                    var textRect = zoomChain[0].GetProperty("rect");
                    var toastRect = zoomChain[2].GetProperty("rect");
                    popoversMatch =
                        zoomCoach.GetProperty("message").GetString()?.Contains(
                            "while zooming to maintain the chart position",
                            StringComparison.Ordinal) == true
                        && textRect.GetProperty("height").GetDouble() <= 21.5
                        && textRect.GetProperty("width").GetDouble() > 380
                        && toastRect.GetProperty("height").GetDouble() <= 45.5
                        && toastRect.GetProperty("width").GetDouble()
                            >= textRect.GetProperty("width").GetDouble() + 49;
                }
            }
            if (!layout.GetProperty("ready").GetBoolean()
                || loadingIndicator.GetProperty("count").GetInt32() != 1
                || loadingIndicator.GetProperty("display").GetString() != "none"
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
                || logos.GetProperty("sourced").GetInt32()
                    != logos.GetProperty("count").GetInt32()
                || logos.GetProperty("visible").GetInt32()
                    != logos.GetProperty("count").GetInt32()
                || documentationRect.GetProperty("height").GetDouble() is < 27.5 or > 28.5
                || documentationRect.GetProperty("width").GetDouble() < 110
                || documentation.GetProperty("paddingLeft").GetString() != "12px"
                || documentation.GetProperty("paddingRight").GetString() != "12px"
                || documentation.GetProperty("paddingTop").GetString() != "5px"
                || documentation.GetProperty("paddingBottom").GetString() != "5px"
                || statusPill.GetProperty("display").GetString() != "flex"
                || statusPillRect.GetProperty("width").GetDouble() < 54
                || marketStatus.GetProperty("itemCount").GetInt32() != 3
                || topSeparators.GetProperty("count").GetInt32() < 7
                || !activeRailClass.Contains("isActive-", StringComparison.Ordinal)
                || chartValuesCoach.GetProperty("message").GetString()
                    != "Press and hold to see detailed chart values"
                || chartValuesCoach.GetProperty("position").GetString() != "fixed"
                || chartValuesCoach.GetProperty("transform").GetString()
                    is "none" or "matrix(1, 0, 0, 1, 0, 0)"
                || chartValuesCoachRect.GetProperty("x").GetDouble()
                    < chartCanvasRect.GetProperty("x").GetDouble()
                || chartValuesCoachRect.GetProperty("y").GetDouble()
                    < chartCanvasRect.GetProperty("y").GetDouble()
                || Right(chartValuesCoachRect) > Right(chartCanvasRect)
                || Bottom(chartValuesCoachRect) > Bottom(chartCanvasRect)
                || !popoversMatch
                || pointerTarget.GetProperty("tag").GetString() != "CANVAS"
                || (!drawTrendline
                    && pointerTarget.GetProperty("cursor").GetString() != "crosshair")
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

    private static void InstallPointerCertification(NativeWebSceneView view)
    {
        var task = view.EvaluateTextAsync("""
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
              chartWindow.__webScenePointerCertification = state;
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

    private static int CaptureToolbarOverflowEvidence(
        NativeWebSceneView view,
        Window window,
        string output,
        int width,
        int height)
    {
        var surface = (NativeSceneSurface)view.Content!;
        var before = CaptureToolbarOverflowState(view);
        surface.SubmitAvaloniaPointerMove(width / 2f, 20);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(500));
        var after = CaptureToolbarOverflowState(view);
        surface.SubmitPointerButton(
            kind: 2,
            x: width - 12,
            y: 19,
            button: 0,
            pressed: true);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(50));
        surface.SubmitPointerButton(
            kind: 3,
            x: width - 12,
            y: 19,
            button: 0,
            pressed: false);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(500));
        var afterClick = CaptureToolbarOverflowState(view);
        File.WriteAllText(
            Path.Combine(output, "toolbar-overflow-before.json"),
            before);
        File.WriteAllText(
            Path.Combine(output, "toolbar-overflow-after.json"),
            after);
        File.WriteAllText(
            Path.Combine(output, "toolbar-overflow-after-click.json"),
            afterClick);
        SaveNativeFrame(
            surface,
            Path.Combine(output, "toolbar-overflow-after.png"),
            width,
            height);
        using var serializedEvidence = JsonDocument.Parse(afterClick);
        var evidenceJson = serializedEvidence.RootElement.ValueKind
            == JsonValueKind.String
                ? serializedEvidence.RootElement.GetString() ?? "{}"
                : afterClick;
        using var evidence = JsonDocument.Parse(evidenceJson);
        var root = evidence.RootElement;
        if (root.TryGetProperty("error", out var error))
        {
            throw new InvalidOperationException(
                $"TradingView toolbar certification failed: {error.GetString()}");
        }

        var chartWidth = root.GetProperty("chartWidth").GetDouble();
        var wrapper = root.GetProperty("wrapper");
        var wrapperWidth = wrapper.GetProperty("clientWidth").GetDouble();
        var scrollWidth = wrapper.GetProperty("scrollWidth").GetDouble();
        var scrollLeft = wrapper.GetProperty("scrollLeft").GetDouble();
        var contentWidth = root.GetProperty("contentWidth").GetDouble();
        var rightArrow = root.GetProperty("rightArrow");
        var rightClass = rightArrow.GetProperty("className").GetString() ?? "";
        var rightX = rightArrow.GetProperty("x").GetDouble();
        var rightWidth = rightArrow.GetProperty("width").GetDouble();
        if (scrollWidth <= wrapperWidth + 1
            || contentWidth <= wrapperWidth + 1
            || !rightClass.Contains("isVisible-", StringComparison.Ordinal)
            || rightX < -0.5
            || rightX + rightWidth > chartWidth + 0.5
            || scrollLeft <= 1)
        {
            throw new InvalidOperationException(
                "TradingView toolbar overflow navigation was not exposed after hover. "
                + $"chart={chartWidth:F1}, wrapper={wrapperWidth:F1}, "
                + $"scroll={scrollWidth:F1}, content={contentWidth:F1}, "
                + $"scrollLeft={scrollLeft:F1}, "
                + $"right=({rightX:F1},{rightWidth:F1},'{rightClass}').");
        }

        Console.WriteLine(
            "TradingView toolbar overflow certified: "
            + $"wrapper={wrapperWidth:F1}, scroll={scrollWidth:F1}, "
            + $"content={contentWidth:F1}, scrollLeft={scrollLeft:F1}, "
            + $"right-arrow-x={rightX:F1}.");
        return 0;
    }

    private static string CaptureToolbarOverflowState(NativeWebSceneView view)
    {
        var evaluation = view.EvaluateTextAsync("""
            (() => {
              const chart = Array.from(document.querySelectorAll('iframe'))
                .map(frame => frame.contentWindow)
                .find(candidate =>
                  candidate?.document?.querySelectorAll('canvas').length >= 8);
              if (!chart) return JSON.stringify({ error: 'chart-frame-missing' });
              const doc = chart.document;
              const elements = Array.from(doc.querySelectorAll('*'));
              const wrapper = elements.find(element => {
                const className = String(element.className || '');
                const rect = element.getBoundingClientRect();
                return className.includes('scrollWrap-')
                  && rect.y >= -1
                  && rect.y < 50
                  && rect.width > 0
                  && rect.width <= chart.innerWidth + 1
                  && element.scrollWidth > element.clientWidth + 1;
              });
              if (!wrapper) return JSON.stringify({ error: 'overflow-wrapper-missing' });
              const content = Array.from(wrapper.children).find(element =>
                String(element.className || '').includes('content-'));
              const rightArrow = elements.find(element => {
                const className = String(element.className || '');
                const rect = element.getBoundingClientRect();
                return className.includes('scrollRight-')
                  && rect.height > 0
                  && rect.y >= -1
                  && rect.y < 50;
              });
              const leftArrow = elements.find(element => {
                const className = String(element.className || '');
                const rect = element.getBoundingClientRect();
                return className.includes('scrollLeft-')
                  && rect.height > 0
                  && rect.y >= -1
                  && rect.y < 50;
              });
              const arrow = element => {
                if (!element) return null;
                const rect = element.getBoundingClientRect();
                return {
                  className: String(element.className || ''),
                  x: rect.x,
                  width: rect.width
                };
              };
              return JSON.stringify({
                chartWidth: chart.innerWidth,
                wrapper: {
                  clientWidth: wrapper.clientWidth,
                  scrollWidth: wrapper.scrollWidth,
                  scrollLeft: wrapper.scrollLeft
                },
                contentWidth: content?.getBoundingClientRect().width ?? 0,
                leftArrow: arrow(leftArrow),
                rightArrow: arrow(rightArrow)
              });
            })()
            """);
        PumpUntil(evaluation, TimeSpan.FromSeconds(10));
        return evaluation.GetAwaiter().GetResult();
    }

    private static string WaitForWebSocketEvidence(
        NativeWebSceneView view,
        Window window)
    {
        var timer = Stopwatch.StartNew();
        string evidence = "{}";
        while (timer.Elapsed < TimeSpan.FromSeconds(45))
        {
            PumpFrames(view, window, TimeSpan.FromMilliseconds(250));
            var evaluation = view.EvaluateTextAsync("""
                ({
                  url: location.href,
                  title: document.title,
                  readyState: document.readyState,
                  preferredColorScheme: {
                    dark:
                      matchMedia('(prefers-color-scheme: dark)').matches,
                    light:
                      matchMedia('(prefers-color-scheme: light)').matches
                  },
                  webSocketType: typeof WebSocket,
                  websocket: [
                    globalThis,
                    ...Array.from(document.querySelectorAll('iframe'))
                      .map(frame => frame.contentWindow)
                  ]
                    .map(realm =>
                      realm?.__webSceneWebSocketDiagnostics?.() ?? null)
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
                      frame.contentWindow?.__webScenePointerCertification ?? null)
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
                    const loadingIndicator = chartDocument.querySelector(
                      '.loading-indicator');
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
                    const chartValuesMessage = Array.from(
                      chartDocument.querySelectorAll('*'))
                      .filter(node => {
                        const rect = node.getBoundingClientRect();
                        return rect.width > 0 && rect.height > 0
                          && node.textContent?.includes(
                            'Press and hold to see detailed chart values');
                      })
                      .sort((left, right) => {
                        const leftRect = left.getBoundingClientRect();
                        const rightRect = right.getBoundingClientRect();
                        return leftRect.width * leftRect.height
                          - rightRect.width * rightRect.height;
                      })[0];
                    const chartValuesCoach = (() => {
                      for (let node = chartValuesMessage;
                           node;
                           node = node.parentElement) {
                        const rect = node.getBoundingClientRect();
                        if (getComputedStyle(node).position === 'fixed'
                            && rect.width > 0 && rect.height > 0) {
                          return node;
                        }
                      }
                      return null;
                    })();
                    const zoomMessage = Array.from(
                      chartDocument.querySelectorAll('*'))
                      .filter(node => {
                        const rect = node.getBoundingClientRect();
                        return rect.width > 0 && rect.height > 0
                          && node.children.length === 0
                          && node.textContent?.includes(
                            'while zooming to maintain the chart position');
                      })
                      .sort((left, right) =>
                        left.getBoundingClientRect().width
                        - right.getBoundingClientRect().width)[0];
                    const describePopoverChain = node => {
                      const result = [];
                      for (let current = node;
                           current && result.length < 8;
                           current = current.parentElement) {
                        const rect = current.getBoundingClientRect();
                        const style = getComputedStyle(current);
                        result.push({
                          tag: current.tagName,
                          className: current.className,
                          styleAttribute: current.getAttribute('style'),
                          text: current.children.length === 0
                            ? current.textContent?.trim() : null,
                          rect: {
                            x: rect.x, y: rect.y,
                            width: rect.width, height: rect.height
                          },
                          position: style.position,
                          display: style.display,
                          transform: style.transform,
                          width: style.width,
                          minWidth: style.minWidth,
                          maxWidth: style.maxWidth,
                          whiteSpace: style.whiteSpace,
                          overflowWrap: style.overflowWrap,
                          wordBreak: style.wordBreak,
                          flex: style.flex,
                          flexGrow: style.flexGrow,
                          flexShrink: style.flexShrink,
                          flexBasis: style.flexBasis,
                          padding: style.padding,
                          margin: style.margin,
                          lineHeight: style.lineHeight,
                          fontSize: style.fontSize,
                          backgroundColor: style.backgroundColor
                        });
                      }
                      return result;
                    };
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
                          sourced: logos.filter(node =>
                            Boolean(node.src)).length,
                          visible: logos.filter(node => {
                            const rect = node.getBoundingClientRect();
                            const style = getComputedStyle(node);
                            return Boolean(node.src)
                              && rect.width > 0 && rect.height > 0
                              && style.display !== 'none'
                              && style.visibility !== 'hidden';
                          }).length,
                          sources: logos.map(node => node.src),
                          images: logos.map(node => ({
                            src: node.src,
                            visual: describeVisual(node)
                          }))
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
                        },
                        chartValuesCoach: chartValuesCoach ? {
                          ...describeVisual(chartValuesCoach),
                          message: chartValuesMessage?.textContent?.trim(),
                          position:
                            getComputedStyle(chartValuesCoach).position,
                          transform:
                            getComputedStyle(chartValuesCoach).transform,
                          chain: describePopoverChain(chartValuesMessage)
                        } : null,
                        zoomCoach: zoomMessage ? {
                          message: zoomMessage.textContent?.trim(),
                          chain: describePopoverChain(zoomMessage)
                        } : null
                      },
                      loadingIndicator: loadingIndicator ? {
                        count: chartDocument.querySelectorAll(
                          '.loading-indicator').length,
                        html: loadingIndicator.outerHTML,
                        parentClass:
                          loadingIndicator.parentElement?.className ?? null,
                        styleAttribute:
                          loadingIndicator.getAttribute('style'),
                        display:
                          getComputedStyle(loadingIndicator).display,
                        position:
                          getComputedStyle(loadingIndicator).position,
                        zIndex:
                          getComputedStyle(loadingIndicator).zIndex,
                        background:
                          getComputedStyle(loadingIndicator).backgroundColor
                      } : null,
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
                            display: getComputedStyle(node).display,
                            background:
                              getComputedStyle(node).backgroundColor,
                            styleAttribute: node.getAttribute('style'),
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
                      frame.getAttribute('data-webscene-remote-result'),
                    frameError:
                      frame.getAttribute('data-webscene-frame-error'),
                    websocket:
                      frame.contentWindow
                        ?.__webSceneWebSocketDiagnostics?.() ?? null,
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
                    bodyText:
                      frame.contentDocument?.body?.innerText?.slice(0, 500) ?? null,
                    bodyClass:
                      frame.contentDocument?.body?.className ?? null,
                    scripts: Array.from(
                      frame.contentDocument?.querySelectorAll('script') ?? [])
                      .map(script => ({
                        src: script.src,
                        textLength: script.textContent?.length ?? 0
                      })),
                    realm: frame.contentWindow ? {
                      url: frame.contentWindow.location?.href ?? null,
                      tradingViewType:
                        typeof frame.contentWindow.TradingView,
                      requireType: typeof frame.contentWindow.require,
                      webpackType:
                        typeof frame.contentWindow.webpackChunktradingview
                    } : null,
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
                throw new TimeoutException(
                    $"The TradingView proof exceeded {timeout}.");
            }
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }
        task.GetAwaiter().GetResult();
    }

    private static void SaveNativeFrame(
        NativeSceneSurface surface,
        string path,
        int width,
        int height)
    {
        var png = surface.CaptureRetainedScenePng();
        File.WriteAllBytes(path, png);
        using var stream = new MemoryStream(png);
        using var frame = new Bitmap(stream);
        if (frame.PixelSize != new PixelSize(width, height))
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

    private static void CaptureOverlay(
        NativeWebSceneView view,
        Window window,
        NativeSceneSurface surface,
        string output,
        string overlay,
        int width,
        int height)
    {
        var isOrderMenu = overlay == "order-menu";
        if (isOrderMenu)
        {
            var openTicket = view.EvaluateTextAsync("""
                (() => {
                  const chartDocument = Array.from(
                    document.querySelectorAll('iframe'))
                    .map(frame => frame.contentDocument)
                    .find(candidate =>
                      candidate?.querySelectorAll('canvas').length >= 8);
                  const sell = Array.from(
                    chartDocument?.querySelectorAll('*') ?? [])
                    .find(candidate => {
                      const rect = candidate.getBoundingClientRect();
                      return candidate.children.length === 0
                        && candidate.textContent?.trim().toLowerCase() === 'sell'
                        && rect.width > 0 && rect.height > 0
                        && rect.y < 150;
                    });
                  if (!sell) return null;
                  const rect = sell.getBoundingClientRect();
                  return { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };
                })()
                """);
            PumpUntil(openTicket, TimeSpan.FromSeconds(10));
            using var ticketGeometry = JsonDocument.Parse(openTicket.Result);
            if (ticketGeometry.RootElement.ValueKind != JsonValueKind.Object)
            {
                throw new InvalidOperationException(
                    "TradingView Sell control was unavailable.");
            }
            var ticketX = ticketGeometry.RootElement.GetProperty("x").GetDouble();
            var ticketY = ticketGeometry.RootElement.GetProperty("y").GetDouble();
            surface.SubmitAvaloniaPointerMove(ticketX, ticketY);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
            surface.SubmitPointerButton(2, ticketX, ticketY, 0, pressed: true);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(50));
            surface.SubmitPointerButton(3, ticketX, ticketY, 0, pressed: false);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(750));
        }

        var selector = overlay switch
        {
            "layout" => """
                candidate.getAttribute('aria-label') === 'Layout setup'
                """,
            "indicators" => """
                candidate.textContent?.trim() === 'Indicators'
                """,
            "interval" => """
                candidate.textContent?.trim() === '1h'
                """,
            "right-toolbar" => """
                candidate === chartDocument.querySelector(
                  '.layout__area--right [class^="toolbar-"]')
                  ?.querySelectorAll('button')[1]
                """,
            "order-menu" => """
                candidate.getAttribute('data-qa-id') === 'header-settings'
                """,
            _ => throw new ArgumentException(
                $"Unknown TradingView overlay '{overlay}'.")
        };
        var evaluation = view.EvaluateTextAsync($$"""
            (() => {
              const chartDocument = Array.from(
                document.querySelectorAll('iframe'))
                .map(frame => frame.contentDocument)
                .find(candidate =>
                  candidate?.querySelectorAll('canvas').length >= 8);
              const candidate = Array.from(
                chartDocument?.querySelectorAll('button') ?? [])
                .find(candidate => {
                  const rect = candidate.getBoundingClientRect();
                  return rect.width > 0 && rect.height > 0
                    && getComputedStyle(candidate).visibility === 'visible'
                    && ({{selector}});
                });
              if (!candidate) return null;
              const rect = candidate.getBoundingClientRect();
              return {
                x: rect.x + rect.width / 2,
                y: rect.y + rect.height / 2
              };
            })()
            """);
        PumpUntil(evaluation, TimeSpan.FromSeconds(10));
        using var geometry = JsonDocument.Parse(evaluation.Result);
        if (geometry.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException(
                $"TradingView overlay control '{overlay}' was unavailable.");
        }
        var x = geometry.RootElement.GetProperty("x").GetDouble();
        var y = geometry.RootElement.GetProperty("y").GetDouble();
        surface.SubmitAvaloniaPointerMove(x, y);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
        surface.SubmitPointerButton(2, x, y, 0, pressed: true);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(50));
        surface.SubmitPointerButton(3, x, y, 0, pressed: false);
        PumpFrames(view, window, TimeSpan.FromSeconds(1));
        if (isOrderMenu)
        {
            var menuEvidence = view.EvaluateTextAsync("""
                (() => {
                  const chartDocument = Array.from(
                    document.querySelectorAll('iframe'))
                    .map(frame => frame.contentDocument)
                    .find(candidate =>
                      candidate?.querySelectorAll('canvas').length >= 8);
                  const undock = Array.from(
                    chartDocument?.querySelectorAll('*') ?? [])
                    .find(candidate => candidate.children.length === 0
                      && candidate.textContent?.trim() === 'Undock order panel');
                  const describe = node => {
                    const result = [];
                    for (let current = node;
                         current && result.length < 18;
                         current = current.parentElement) {
                      const rect = current.getBoundingClientRect();
                      const style = getComputedStyle(current);
                      result.push({
                        tag: current.tagName,
                        className: current.className,
                        role: current.getAttribute('role'),
                        styleAttribute: current.getAttribute('style'),
                        rect: {
                          x: rect.x, y: rect.y,
                          width: rect.width, height: rect.height
                        },
                        display: style.display,
                        position: style.position,
                        transform: style.transform,
                        width: style.width,
                        height: style.height,
                        minWidth: style.minWidth,
                        maxWidth: style.maxWidth,
                        overflow: style.overflow,
                        contain: style.contain,
                        gridTemplateColumns: style.gridTemplateColumns,
                        flex: style.flex,
                        zIndex: style.zIndex,
                        backgroundColor: style.backgroundColor
                      });
                    }
                    return result;
                  };
                  return {
                    found: Boolean(undock),
                    bodyTextIncludesMenu:
                      chartDocument?.body?.innerText?.includes(
                        'Undock order panel') ?? false,
                    headerSettingsCount: chartDocument?.querySelectorAll(
                      'button[data-qa-id="header-settings"]').length ?? 0,
                    closeButtonCount: chartDocument?.querySelectorAll(
                      'button[data-qa-id="button-close"]').length ?? 0,
                    chain: describe(undock)
                  };
                })()
                """);
            PumpUntil(menuEvidence, TimeSpan.FromSeconds(10));
            File.WriteAllText(
                Path.Combine(output, "native-tradingview-order-menu-evidence.json"),
                menuEvidence.Result);
            using var menuDocument = JsonDocument.Parse(menuEvidence.Result);
            var menuRoot = menuDocument.RootElement;
            var positionerMatches = false;
            foreach (var item in menuRoot.GetProperty("chain").EnumerateArray())
            {
                var className = item.GetProperty("className").GetString() ?? "";
                if (!className.Contains("positioner-", StringComparison.Ordinal)) continue;
                var rect = item.GetProperty("rect");
                var menuRight = rect.GetProperty("x").GetDouble()
                    + rect.GetProperty("width").GetDouble();
                positionerMatches = item.GetProperty("position").GetString() == "fixed"
                    && rect.GetProperty("x").GetDouble() >= 0
                    && rect.GetProperty("y").GetDouble() >= 0
                    && rect.GetProperty("width").GetDouble() >= 200
                    && rect.GetProperty("height").GetDouble() >= 110
                    && menuRight <= width;
                break;
            }
            if (!menuRoot.GetProperty("found").GetBoolean()
                || !menuRoot.GetProperty("bodyTextIncludesMenu").GetBoolean()
                || menuRoot.GetProperty("headerSettingsCount").GetInt32() != 1
                || menuRoot.GetProperty("closeButtonCount").GetInt32() != 1
                || !positionerMatches)
            {
                throw new InvalidOperationException(
                    "TradingView order menu did not remain open with a visible, "
                    + $"fixed-position portal: {menuEvidence.Result}");
            }
        }
        SaveNativeFrame(
            surface,
            Path.Combine(output, $"native-tradingview-{overlay}.png"),
            width,
            height);
    }

    private static TrendlineGeometry DrawTrendline(
        NativeWebSceneView view,
        Window window,
        NativeSceneSurface surface)
    {
        var evaluation = view.EvaluateTextAsync("""
            (() => {
              const chartDocument = Array.from(
                document.querySelectorAll('iframe'))
                .map(frame => frame.contentDocument)
                .find(candidate =>
                  candidate?.querySelectorAll('canvas').length >= 8);
              const tool = Array.from(
                chartDocument?.querySelectorAll('button') ?? [])
                .find(node => node.getAttribute('aria-label') === 'Trendline');
              const canvas = chartDocument?.elementFromPoint(700, 350);
              if (!tool || canvas?.tagName !== 'CANVAS') return null;
              const toolRect = tool.getBoundingClientRect();
              const canvasRect = canvas.getBoundingClientRect();
              return {
                tool: {
                  x: toolRect.x + toolRect.width / 2,
                  y: toolRect.y + toolRect.height / 2
                },
                start: {
                  x: canvasRect.x + canvasRect.width * 0.25,
                  y: canvasRect.y + canvasRect.height * 0.72
                },
                end: {
                  x: canvasRect.x + canvasRect.width * 0.68,
                  y: canvasRect.y + canvasRect.height * 0.28
                }
              };
            })()
            """);
        PumpUntil(evaluation, TimeSpan.FromSeconds(10));
        using var geometry = JsonDocument.Parse(evaluation.Result);
        if (geometry.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException(
                "TradingView trendline tool or chart canvas was unavailable.");
        }

        static (double X, double Y) Point(JsonElement root, string name)
        {
            var point = root.GetProperty(name);
            return (
                point.GetProperty("x").GetDouble(),
                point.GetProperty("y").GetDouble());
        }

        void Click((double X, double Y) point)
        {
            surface.SubmitAvaloniaPointerMove(point.X, point.Y);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
            surface.SubmitPointerButton(2, point.X, point.Y, 0, pressed: true);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(50));
            surface.SubmitPointerButton(3, point.X, point.Y, 0, pressed: false);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(250));
        }

        Click(Point(geometry.RootElement, "tool"));
        Click(Point(geometry.RootElement, "start"));
        Click(Point(geometry.RootElement, "end"));
        PumpFrames(view, window, TimeSpan.FromSeconds(1));
        return new TrendlineGeometry(
            Point(geometry.RootElement, "start"),
            Point(geometry.RootElement, "end"));
    }

    private static void ValidateTrendlineHandles(
        string screenshotPath,
        TrendlineGeometry geometry)
    {
        using var bitmap = SKBitmap.Decode(File.ReadAllBytes(screenshotPath))
            ?? throw new InvalidOperationException(
                "TradingView trendline capture could not be decoded.");
        static int HandleColorDistance(SKColor color) =>
            Math.Abs(color.Red - 0x1e)
            + Math.Abs(color.Green - 0x53)
            + Math.Abs(color.Blue - 0xe5);
        int CountHandlePixels((double X, double Y) point)
        {
            var centerX = (int)Math.Round(point.X);
            var centerY = (int)Math.Round(point.Y);
            var count = 0;
            for (var y = centerY - 8; y <= centerY + 8; ++y)
            {
                for (var x = centerX - 8; x <= centerX + 8; ++x)
                {
                    var radiusSquared = (x - point.X) * (x - point.X)
                        + (y - point.Y) * (y - point.Y);
                    if (radiusSquared is < 9 or > 64) continue;
                    if (HandleColorDistance(bitmap.GetPixel(x, y)) <= 30) ++count;
                }
            }
            return count;
        }
        var startPixels = CountHandlePixels(geometry.Start);
        var endPixels = CountHandlePixels(geometry.End);
        if (startPixels < 12 || endPixels < 12)
        {
            throw new InvalidOperationException(
                "TradingView selected trendline endpoint handles were not painted "
                + $"(start={startPixels}, end={endPixels}).");
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

    private static string? ReadArgument(
        IReadOnlyList<string> arguments,
        string name)
    {
        for (var index = 0; index + 1 < arguments.Count; ++index)
        {
            if (arguments[index] == name)
            {
                return arguments[index + 1];
            }
        }
        return null;
    }

    private readonly record struct TrendlineGeometry(
        (double X, double Y) Start,
        (double X, double Y) End);
}
