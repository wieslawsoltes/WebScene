using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using WebScene.Core;
using WebScene.JavaScript;
using WebScene.Backends.Avalonia;

namespace JavaScript.Avalonia;

public sealed record JavaScriptExecutionMetric(
    string Kind,
    string Source,
    int SourceLength,
    bool UsedPreparedScript,
    bool PreparedCacheHit,
    TimeSpan PreparationDuration,
    TimeSpan ExecutionDuration);

public enum UiThreadWorkKind
{
    JavaScript,
    Css,
    Layout
}

public readonly record struct UiThreadWorkBudgetMetrics(
    TimeSpan Budget,
    long JavaScriptSamples,
    long JavaScriptOverruns,
    TimeSpan MaximumJavaScriptDuration,
    long CssSamples,
    long CssOverruns,
    TimeSpan MaximumCssDuration,
    long LayoutSamples,
    long LayoutOverruns,
    TimeSpan MaximumLayoutDuration);

public readonly record struct UserTaskPerformanceMetric(
    string Kind,
    long Count,
    TimeSpan TotalDuration,
    TimeSpan MaximumDuration)
{
    public long TotalAllocatedBytes { get; init; }

    public long MaximumAllocatedBytes { get; init; }
}

public readonly record struct ResizeCallbackPerformanceMetrics(
    long TopLevelCount,
    TimeSpan TopLevelDuration,
    long TopLevelAllocatedBytes,
    long OwnerWindowCount,
    TimeSpan OwnerWindowDuration,
    long OwnerWindowAllocatedBytes,
    long FrameWindowCount,
    TimeSpan FrameWindowDuration,
    long FrameWindowAllocatedBytes,
    long ObserverCount,
    TimeSpan ObserverDuration,
    long ObserverAllocatedBytes);

public sealed class WebSceneDownloadRequestedEventArgs : EventArgs
{
    internal WebSceneDownloadRequestedEventArgs(string fileName, string contentType, byte[] data)
    {
        FileName = fileName;
        ContentType = contentType;
        Data = data;
    }

    public string FileName { get; }

    public string ContentType { get; }

    public byte[] Data { get; }

    public bool Handled { get; set; }
}

internal enum ResizeCallbackKind
{
    OwnerWindow,
    FrameWindow,
    Observer
}

/// <summary>
/// Engine-neutral browser services shared by the DOM and the V8 adapter.
/// JavaScript values and compilation remain owned by the engine adapter.
/// </summary>
public class AvaloniaBrowserHost :
    WebSceneJavaScriptTaskSchedulerHost<AvaloniaBrowserHost>,
    IDisposable,
    IWebSceneJavaScriptHost
{
    private readonly AvaloniaHostServices _hostServices;
    private readonly AvaloniaBackendHost _backend;
    private readonly List<JavaScriptExecutionMetric> _javaScriptExecutionMetrics = new();
    private readonly Dictionary<string, EventListenerMetric> _eventListenerMetrics = new(StringComparer.Ordinal);
    private readonly bool _traceEventListeners =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_TRACE_EVENT_LISTENERS"), "1", StringComparison.Ordinal);
    private readonly bool _traceRafCallbacks =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_TRACE_RAF"), "1", StringComparison.Ordinal);
    private readonly bool _disablePositionedLayoutReapply =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_POSITIONED_LAYOUT_REAPPLY"), "1", StringComparison.Ordinal);
    private readonly Func<AvaloniaBrowserHost, AvaloniaDomDocument>? _documentFactory;
    private WindowJs? _windowObject;
    private object? _currentScript;
    private bool _disposed;
    private int _jsCallDepth;
    private bool _viewportResizeSubscriptionSetup;
    private Control? _subscribedDocumentViewport;
    private Size _lastObservedViewportSize;
    private Size _lastDispatchedViewportSize;
    private bool _windowResizeDispatchScheduled;
    private int _resizeStyleReconciliationGeneration;
    private CancellationTokenSource? _resizeStyleReconciliationCancellation;
    private bool _positionedLayoutReapplyScheduled;
    private bool _targetOnlyInlineStylesArmed;
    private long _positionedLayoutReapplyCount;
    private long _positionedLayoutReapplyTicks;
    private long _javaScriptWorkSamples;
    private long _javaScriptWorkOverruns;
    private long _maximumJavaScriptWorkTicks;
    private long _cssWorkSamples;
    private long _cssWorkOverruns;
    private long _maximumCssWorkTicks;
    private long _layoutWorkSamples;
    private long _layoutWorkOverruns;
    private long _maximumLayoutWorkTicks;
    private int _resizeCallbackDepth;
    private long _topLevelResizeCallbackCount;
    private long _topLevelResizeCallbackTicks;
    private long _topLevelResizeCallbackAllocatedBytes;
    private long _ownerWindowResizeCallbackCount;
    private long _ownerWindowResizeCallbackTicks;
    private long _ownerWindowResizeCallbackAllocatedBytes;
    private long _frameWindowResizeCallbackCount;
    private long _frameWindowResizeCallbackTicks;
    private long _frameWindowResizeCallbackAllocatedBytes;
    private long _resizeObserverCallbackCount;
    private long _resizeObserverCallbackTicks;
    private long _resizeObserverCallbackAllocatedBytes;

    public AvaloniaBrowserHost(
        TopLevel topLevel,
        Func<AvaloniaBrowserHost, AvaloniaDomDocument>? documentFactory = null,
        bool enableTargetOnlyInlineStyles = false,
        bool enableInheritedCursorRebase = true,
        bool enableComputedStyleSnapshotStateReuse = true,
        bool enableIndexedAppendStylesheetMatching = true)
    {
        TopLevel = topLevel ?? throw new ArgumentNullException(nameof(topLevel));
        _hostServices = new AvaloniaHostServices(topLevel);
        _backend = new AvaloniaBackendHost(_hostServices, ownsServices: false);
        _backend.Mount();
        _documentFactory = documentFactory;
        var targetOnlyFromEnvironment = string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_TARGET_ONLY_INLINE_STYLES"),
            "1",
            StringComparison.Ordinal);
        EnableTargetOnlyInlineStyles = enableTargetOnlyInlineStyles || targetOnlyFromEnvironment;
        EnableInheritedCursorRebase = enableInheritedCursorRebase;
        EnableComputedStyleSnapshotStateReuse = enableComputedStyleSnapshotStateReuse;
        EnableIndexedAppendStylesheetMatching = enableIndexedAppendStylesheetMatching;
        _targetOnlyInlineStylesArmed = targetOnlyFromEnvironment;
        Location = new LocationJs(this);
        Document = documentFactory?.Invoke(this) ?? CreateDocument();
        Document.EnableNativeLayoutHotPath();
        _hostServices.SetDocumentViewportProvider(Document.GetDocumentViewport);
        _windowObject = CreateWindowObject();
        Document.ScheduleReadyStateCompletion();
        TopLevel.Closed += OnTopLevelClosed;
    }

    public TopLevel TopLevel { get; }
    public AvaloniaDomDocument Document { get; }
    public IWebSceneBackendHost Backend => _backend;
    public IWebSceneHostServices Services => _hostServices;
    public WindowJs BrowserWindow => _windowObject ?? throw new ObjectDisposedException(nameof(AvaloniaBrowserHost));
    public LocationJs Location { get; }
    public string ScriptBaseDirectory
    {
        get => _hostServices.ResourceLoader.ScriptBaseDirectory;
        set => _hostServices.ResourceLoader.ScriptBaseDirectory = value ?? throw new ArgumentNullException(nameof(value));
    }

    /// <summary>
    /// Maps an absolute browser URL prefix to a local resource directory. This
    /// lets embedded same-origin documents load their normal relative scripts,
    /// styles, fonts and images without running an HTTP server.
    /// </summary>
    public void MountResourceDirectory(string addressPrefix, string directory)
        => _hostServices.ResourceLoader.MountDirectory(addressPrefix, directory);

    public IExternalJavaScriptCallbackAdapter? ExternalCallbackAdapter { get; set; }
    public IExternalVirtualBrowsingContextFactory? ExternalVirtualBrowsingContextFactory { get; set; }
    public bool EnableDiagnosticLogging { get; set; } =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_JS_CONSOLE"), "1", StringComparison.Ordinal);
    public bool CollectPerformanceMetrics
    {
        get => CollectJavaScriptTaskPerformanceMetrics;
        set => CollectJavaScriptTaskPerformanceMetrics = value;
    }
    public TimeSpan UiThreadWorkBudget { get; set; } = TimeSpan.FromMilliseconds(8);
    public bool EnableTargetOnlyInlineStyles { get; }
    public bool EnableInheritedCursorRebase { get; }
    public bool EnableComputedStyleSnapshotStateReuse { get; }
    public bool EnableIndexedAppendStylesheetMatching { get; }
    public bool TargetOnlyInlineStylesArmed => _targetOnlyInlineStylesArmed;
    public List<string> JavaScriptExceptionDiagnostics { get; } = new();
    public event EventHandler<WebSceneDownloadRequestedEventArgs>? DownloadRequested;
    public IReadOnlyList<JavaScriptExecutionMetric> JavaScriptExecutionMetrics => _javaScriptExecutionMetrics;
    internal object? CurrentScript => _currentScript;
    internal bool TraceEventListeners => _traceEventListeners;
    internal bool IsDisposed => _disposed;
    protected override bool IsJavaScriptEngineExecuting => _jsCallDepth > 0;
    protected override bool IsJavaScriptTaskHostDisposed => _disposed;
    protected override bool TraceAnimationFrameCallbacks => _traceRafCallbacks;
    protected override IWebSceneDispatcher JavaScriptDispatcher => Services.Dispatcher;
    protected override IWebSceneFrameScheduler JavaScriptFrames => Services.Frames;
    internal bool IsExecutingJavaScript => _jsCallDepth > 0 || IsProcessingTasks;
    internal long UserTasksEnqueued => TasksEnqueued;
    internal long UserTasksExecuted => TasksExecuted;
    internal IReadOnlyList<string> UserTaskExceptionDiagnostics => ExceptionDiagnostics;
    internal int MaxPendingUserTasks => MaximumPendingTasks;
    internal long RafBatchCount => AnimationFrameBatchCount;
    internal long RafCallbackCount => AnimationFrameCallbackCount;
    internal TimeSpan UserTaskDuration => TicksToTimeSpan(TaskTicks);
    internal TimeSpan RafDuration => TicksToTimeSpan(AnimationFrameTicks);
    internal TimeSpan MaximumUserTaskDuration => TicksToTimeSpan(MaximumTaskTicks);
    internal TimeSpan MaximumRafDuration => TicksToTimeSpan(MaximumAnimationFrameTicks);
    internal long UserTaskAllocatedBytes => TaskAllocatedBytes;
    internal long RafAllocatedBytes => AnimationFrameAllocatedBytes;
    internal long PositionedLayoutReapplyCount => _positionedLayoutReapplyCount;
    internal TimeSpan PositionedLayoutReapplyDuration => TicksToTimeSpan(_positionedLayoutReapplyTicks);
    internal int ResizeStyleReconciliationCount { get; private set; }
    internal int ResizeStyleReconciliationGeneration => _resizeStyleReconciliationGeneration;
    internal int LastReconciledResizeStyleGeneration { get; private set; }
    internal int PendingExternalAnimationFrameCount => PendingAnimationFrameCount;

    protected virtual AvaloniaDomDocument CreateDocument() => new(this);

    protected virtual WindowJs CreateWindowObject() => new(this);

    public void ArmTargetOnlyInlineStyles()
    {
        if (EnableTargetOnlyInlineStyles)
        {
            _targetOnlyInlineStylesArmed = true;
        }
    }

    public UiThreadWorkBudgetMetrics GetUiThreadWorkBudgetMetrics()
        => new(
            UiThreadWorkBudget,
            _javaScriptWorkSamples,
            _javaScriptWorkOverruns,
            TicksToTimeSpan(_maximumJavaScriptWorkTicks),
            _cssWorkSamples,
            _cssWorkOverruns,
            TicksToTimeSpan(_maximumCssWorkTicks),
            _layoutWorkSamples,
            _layoutWorkOverruns,
            TicksToTimeSpan(_maximumLayoutWorkTicks));

    public IReadOnlyList<UserTaskPerformanceMetric> GetUserTaskPerformanceMetrics()
        => GetPerformanceMetrics()
            .Select(static metric => new UserTaskPerformanceMetric(
                metric.Kind,
                metric.Count,
                TicksToTimeSpan(metric.TotalTicks),
                TicksToTimeSpan(metric.MaximumTicks))
            {
                TotalAllocatedBytes = metric.TotalAllocatedBytes,
                MaximumAllocatedBytes = metric.MaximumAllocatedBytes
            })
            .ToArray();

    public ResizeCallbackPerformanceMetrics GetResizeCallbackPerformanceMetrics()
        => new(
            _topLevelResizeCallbackCount,
            TicksToTimeSpan(_topLevelResizeCallbackTicks),
            _topLevelResizeCallbackAllocatedBytes,
            _ownerWindowResizeCallbackCount,
            TicksToTimeSpan(_ownerWindowResizeCallbackTicks),
            _ownerWindowResizeCallbackAllocatedBytes,
            _frameWindowResizeCallbackCount,
            TicksToTimeSpan(_frameWindowResizeCallbackTicks),
            _frameWindowResizeCallbackAllocatedBytes,
            _resizeObserverCallbackCount,
            TicksToTimeSpan(_resizeObserverCallbackTicks),
            _resizeObserverCallbackAllocatedBytes);

    internal void ResetUserTaskPerformanceMetrics() => ResetPerformanceMetrics();

    internal void RecordPositionedLayoutReapply(long ticks)
    {
        if (!CollectPerformanceMetrics) return;
        _positionedLayoutReapplyCount++;
        _positionedLayoutReapplyTicks += ticks;
    }

    internal void RecordUiThreadWork(UiThreadWorkKind kind, long elapsedTicks)
    {
        var budget = UiThreadWorkBudget;
        if (budget <= TimeSpan.Zero || elapsedTicks <= 0) return;
        var budgetTicks = budget.TotalSeconds * Stopwatch.Frequency;
        switch (kind)
        {
            case UiThreadWorkKind.JavaScript:
                _javaScriptWorkSamples++;
                _maximumJavaScriptWorkTicks = Math.Max(_maximumJavaScriptWorkTicks, elapsedTicks);
                if (elapsedTicks > budgetTicks) _javaScriptWorkOverruns++;
                break;
            case UiThreadWorkKind.Css:
                _cssWorkSamples++;
                _maximumCssWorkTicks = Math.Max(_maximumCssWorkTicks, elapsedTicks);
                if (elapsedTicks > budgetTicks) _cssWorkOverruns++;
                break;
            case UiThreadWorkKind.Layout:
                _layoutWorkSamples++;
                _maximumLayoutWorkTicks = Math.Max(_maximumLayoutWorkTicks, elapsedTicks);
                if (elapsedTicks > budgetTicks) _layoutWorkOverruns++;
                break;
        }
    }

    internal void RecordEventListener(string key, long elapsedTicks, long allocatedBytes)
    {
        if (!_traceEventListeners) return;
        if (_eventListenerMetrics.TryGetValue(key, out var metric))
        {
            _eventListenerMetrics[key] = metric with
            {
                Calls = metric.Calls + 1,
                ElapsedTicks = metric.ElapsedTicks + elapsedTicks,
                AllocatedBytes = metric.AllocatedBytes + allocatedBytes
            };
        }
        else
        {
            _eventListenerMetrics[key] = new EventListenerMetric(1, elapsedTicks, allocatedBytes);
        }
    }

    internal ResizeCallbackMeasurement MeasureResizeCallback(ResizeCallbackKind kind)
        => new(this, kind);

    internal readonly struct ResizeCallbackMeasurement : IDisposable
    {
        private readonly AvaloniaBrowserHost? _host;
        private readonly ResizeCallbackKind _kind;
        private readonly bool _topLevel;
        private readonly long _started;
        private readonly long _allocatedBytesStart;

        public ResizeCallbackMeasurement(AvaloniaBrowserHost host, ResizeCallbackKind kind)
        {
            if (!host.CollectPerformanceMetrics)
            {
                _host = null;
                _kind = default;
                _topLevel = false;
                _started = 0;
                _allocatedBytesStart = 0;
                return;
            }

            _host = host;
            _kind = kind;
            _topLevel = !host.IsExecutingJavaScript && host._resizeCallbackDepth == 0;
            host._resizeCallbackDepth++;
            _started = Stopwatch.GetTimestamp();
            _allocatedBytesStart = GC.GetAllocatedBytesForCurrentThread();
        }

        public void Dispose()
        {
            if (_host is null)
            {
                return;
            }

            var elapsedTicks = Stopwatch.GetTimestamp() - _started;
            var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - _allocatedBytesStart;
            _host._resizeCallbackDepth--;
            if (_topLevel)
            {
                _host._topLevelResizeCallbackCount++;
                _host._topLevelResizeCallbackTicks += elapsedTicks;
                _host._topLevelResizeCallbackAllocatedBytes += allocatedBytes;
            }

            switch (_kind)
            {
                case ResizeCallbackKind.OwnerWindow:
                    _host._ownerWindowResizeCallbackCount++;
                    _host._ownerWindowResizeCallbackTicks += elapsedTicks;
                    _host._ownerWindowResizeCallbackAllocatedBytes += allocatedBytes;
                    break;
                case ResizeCallbackKind.FrameWindow:
                    _host._frameWindowResizeCallbackCount++;
                    _host._frameWindowResizeCallbackTicks += elapsedTicks;
                    _host._frameWindowResizeCallbackAllocatedBytes += allocatedBytes;
                    break;
                case ResizeCallbackKind.Observer:
                    _host._resizeObserverCallbackCount++;
                    _host._resizeObserverCallbackTicks += elapsedTicks;
                    _host._resizeObserverCallbackAllocatedBytes += allocatedBytes;
                    break;
            }
        }
    }

    internal bool EnterJs(out long started)
    {
        var outermost = _jsCallDepth == 0;
        started = outermost && UiThreadWorkBudget > TimeSpan.Zero ? Stopwatch.GetTimestamp() : 0;
        _jsCallDepth++;
        return outermost;
    }

    internal void ExitJs(bool outermost, long started)
    {
        _jsCallDepth--;
        if (outermost && started != 0)
        {
            RecordUiThreadWork(UiThreadWorkKind.JavaScript, Stopwatch.GetTimestamp() - started);
        }
    }

    internal readonly struct JsCallScope : IDisposable
    {
        private readonly AvaloniaBrowserHost _host;
        private readonly bool _outermost;
        private readonly long _started;

        public JsCallScope(AvaloniaBrowserHost host)
        {
            _host = host;
            _outermost = host.EnterJs(out _started);
        }

        public void Dispose() => _host.ExitJs(_outermost, _started);
    }

    public IDisposable EnterExternalJavaScriptCall() => new JsCallScope(this);

    public double GetPerformanceTimestamp() => Services.Clock.Elapsed.TotalMilliseconds;

    IWebSceneJavaScriptDocument IWebSceneJavaScriptHost.Document => Document;

    object IWebSceneJavaScriptHost.BrowserWindow => BrowserWindow;

    Type IWebSceneJavaScriptHost.UrlBackendType => typeof(UrlJs);

    public object CreateCanvasPath(object? path) => new CanvasPath2D(path);

    internal double GetTimestamp() => GetPerformanceTimestamp();

    internal void ProcessPendingTasks()
    {
        // V8 owns its microtask checkpoint. This host only serializes browser
        // macrotasks and prevents callbacks from re-entering an active call.
    }

    internal void RestoreOwnerRealm()
    {
        // Engine globals are owned and restored by the V8 browsing context.
    }

    public void ExecuteExternalClassicScript(string specifier, Action<string, string> evaluator)
    {
        ArgumentNullException.ThrowIfNull(evaluator);
        var source = ResolveExternalScript(specifier);
        var previous = _currentScript;
        _currentScript = new ScriptElementJs(source.FileName);
        try
        {
            var started = CollectPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
            evaluator(source.Content, source.FileName);
            if (CollectPerformanceMetrics)
            {
                _javaScriptExecutionMetrics.Add(new JavaScriptExecutionMetric(
                    "external-classic-script",
                    source.CacheKey,
                    source.Content.Length,
                    false,
                    false,
                    TimeSpan.Zero,
                    TicksToTimeSpan(Stopwatch.GetTimestamp() - started)));
            }
        }
        finally
        {
            _currentScript = previous;
        }
    }

    public void ExecuteExternalInlineClassicScript(object currentScript, Action evaluator)
    {
        ArgumentNullException.ThrowIfNull(currentScript);
        ArgumentNullException.ThrowIfNull(evaluator);
        var previous = _currentScript;
        _currentScript = currentScript;
        try { evaluator(); }
        finally { _currentScript = previous; }
    }

    public ExternalJavaScriptSource ResolveExternalScript(string specifier, string? referrerDirectory = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(specifier);
        var resolved = Services.Resources.LoadText(
            new WebSceneResourceRequest(specifier, referrerDirectory, WebSceneResourceKind.Script));
        return new ExternalJavaScriptSource(resolved.CacheKey, resolved.Content, resolved.DisplayName, resolved.Directory);
    }

    internal string LoadTextResource(string specifier, string? baseHref = null)
    {
        return Services.Resources.LoadText(
            new WebSceneResourceRequest(specifier, baseHref, WebSceneResourceKind.Data)).Content;
    }

    internal WebSceneTextResource LoadTextResourceDetails(string specifier, string? baseHref = null)
        => Services.Resources.LoadText(
            new WebSceneResourceRequest(specifier, baseHref, WebSceneResourceKind.StyleSheet));

    internal Task<AvaloniaBinaryResource> LoadBinaryResourceAsync(
        string specifier,
        string? baseHref,
        CancellationToken cancellationToken)
        => _hostServices.ResourceLoader.LoadBytesAsync(specifier, baseHref, cancellationToken);

    internal async Task RequestDownloadAsync(string fileName, string href)
    {
        try
        {
            byte[] content;
            string contentType;
            if (UrlJs.TryGetObjectUrlData(href, out var objectUrlData, out var objectUrlContentType))
            {
                content = objectUrlData;
                contentType = objectUrlContentType;
            }
            else
            {
                var resource = await LoadBinaryResourceAsync(href, Location.href, CancellationToken.None);
                content = resource.Content;
                contentType = GetDataUriContentType(href);
            }
            var safeFileName = SanitizeDownloadFileName(fileName, contentType);
            var requested = new WebSceneDownloadRequestedEventArgs(safeFileName, contentType, content);
            DownloadRequested?.Invoke(this, requested);
            if (requested.Handled)
            {
                return;
            }

            var storage = TopLevel.StorageProvider;
            if (!storage.CanSave)
            {
                return;
            }

            var file = await storage.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = "Download image",
                SuggestedFileName = safeFileName,
                DefaultExtension = Path.GetExtension(safeFileName).TrimStart('.'),
                ShowOverwritePrompt = true,
                FileTypeChoices = contentType.Equals("image/png", StringComparison.OrdinalIgnoreCase)
                    ? [new FilePickerFileType("PNG image")
                    {
                        Patterns = ["*.png"],
                        MimeTypes = ["image/png"],
                        AppleUniformTypeIdentifiers = ["public.png"]
                    }]
                    : null
            });
            if (file is null)
            {
                return;
            }

            await using var stream = await file.OpenWriteAsync();
            stream.SetLength(0);
            await stream.WriteAsync(content);
        }
        catch (Exception exception)
        {
            JavaScriptExceptionDiagnostics.Add($"Download failed for '{fileName}': {exception.Message}");
        }
    }

    private static string GetDataUriContentType(string href)
    {
        if (!href.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
        {
            return "application/octet-stream";
        }

        var separator = href.IndexOfAny([';', ','], 5);
        return separator > 5 ? href[5..separator] : "text/plain";
    }

    private static string SanitizeDownloadFileName(string fileName, string contentType)
    {
        var fallback = contentType.Equals("image/png", StringComparison.OrdinalIgnoreCase)
            ? "download.png"
            : "download";
        var candidate = string.IsNullOrWhiteSpace(fileName) ? fallback : Path.GetFileName(fileName.Trim());
        foreach (var invalid in Path.GetInvalidFileNameChars())
        {
            candidate = candidate.Replace(invalid, '_');
        }
        return string.IsNullOrWhiteSpace(candidate) ? fallback : candidate;
    }

    private void OnTopLevelClosed(object? sender, EventArgs e)
    {
        Document.RaiseDocumentEvent("unload", bubbles: false, cancelable: false);
        Dispose();
    }

    private void EnsureViewportSubscription()
    {
        if (_viewportResizeSubscriptionSetup) return;
        _viewportResizeSubscriptionSetup = true;
        var viewport = Document.GetDocumentViewport();
        if (viewport is not null)
        {
            viewport.PropertyChanged += OnViewportPropertyChanged;
            _subscribedDocumentViewport = viewport;
        }
        if (!ReferenceEquals(viewport, TopLevel)) TopLevel.PropertyChanged += OnViewportPropertyChanged;
        _lastObservedViewportSize = GetObservedViewportSize();
        TopLevel.LayoutUpdated += OnTopLevelLayoutUpdated;
    }

    private void OnViewportPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == Visual.BoundsProperty || e.Property == TopLevel.ClientSizeProperty) ScheduleWindowResize();
        if (e.Property == Control.WidthProperty || e.Property == Control.HeightProperty) ScheduleResizeStyleReconciliation();
    }

    private void OnTopLevelLayoutUpdated(object? sender, EventArgs e)
    {
        var size = GetObservedViewportSize();
        if (size.Width <= 0 || size.Height <= 0 || size == _lastObservedViewportSize) return;
        _lastObservedViewportSize = size;
        ScheduleWindowResize();
        SchedulePositionedLayoutReapply();
        ScheduleResizeStyleReconciliation();
    }

    private Size GetObservedViewportSize()
    {
        var size = Services.Viewport.Metrics.ClientSize;
        return new Size(size.Width, size.Height);
    }

    private void ScheduleWindowResize()
    {
        if (_windowResizeDispatchScheduled) return;
        _windowResizeDispatchScheduled = true;
        Services.Dispatcher.Post(DispatchWindowResize, WebSceneDispatchPriority.Background);
    }

    private void DispatchWindowResize()
    {
        if (IsExecutingJavaScript)
        {
            Services.Dispatcher.Post(DispatchWindowResize, WebSceneDispatchPriority.Background);
            return;
        }
        _windowResizeDispatchScheduled = false;
        var size = GetObservedViewportSize();
        if (size.Width <= 0 || size.Height <= 0 || size == _lastDispatchedViewportSize) return;
        _lastDispatchedViewportSize = size;
        Document.DispatchWindowLifecycleEvent("resize");
    }

    private void SchedulePositionedLayoutReapply()
    {
        if (_disablePositionedLayoutReapply || _positionedLayoutReapplyScheduled) return;
        _positionedLayoutReapplyScheduled = true;
        Services.Dispatcher.Post(() =>
        {
            _positionedLayoutReapplyScheduled = false;
            if (!_disposed) Document.ReapplyAllPositionedLayout();
        }, WebSceneDispatchPriority.Render);
    }

    private void ScheduleResizeStyleReconciliation()
    {
        if (_disposed || !TargetOnlyInlineStylesArmed) return;
        var generation = ++_resizeStyleReconciliationGeneration;
        _resizeStyleReconciliationCancellation?.Cancel();
        var cancellation = new CancellationTokenSource();
        _resizeStyleReconciliationCancellation = cancellation;
        Services.Dispatcher.Post(
            () => _ = ReconcileResizeStylesAfterQuietPeriodAsync(generation, cancellation),
            WebSceneDispatchPriority.Background);
    }

    private async Task ReconcileResizeStylesAfterQuietPeriodAsync(
        int generation,
        CancellationTokenSource cancellation)
    {
        var quietPeriodElapsed = false;
        try
        {
            // A dense multi-document surface can spend more than 120 ms in the
            // charts' own synchronous resize handlers. A shorter debounce then
            // mistakes that still-active resize turn for idle time and starts a
            // full media-query cascade in the middle of the drag, feeding the
            // next resize event an even larger backlog. Half a second keeps the
            // conservative cascade off the active input path while the scoped
            // inline/layout updates continue to resize visuals immediately.
            await Task.Delay(TimeSpan.FromMilliseconds(500), cancellation.Token).ConfigureAwait(false);
            quietPeriodElapsed = true;
        }
        catch (OperationCanceledException)
        {
        }

        Services.Dispatcher.Post(() =>
        {
            try
            {
                if (quietPeriodElapsed
                    && !_disposed
                    && ReferenceEquals(_resizeStyleReconciliationCancellation, cancellation)
                    && generation == _resizeStyleReconciliationGeneration)
                {
                    Document.ReconcileStylesAfterViewportResize();
                    ResizeStyleReconciliationCount++;
                    LastReconciledResizeStyleGeneration = generation;
                }
            }
            finally
            {
                if (ReferenceEquals(_resizeStyleReconciliationCancellation, cancellation))
                {
                    _resizeStyleReconciliationCancellation = null;
                }
                cancellation.Dispose();
            }
        }, WebSceneDispatchPriority.Background);
    }

    public void Dispose()
    {
        if (_disposed) return;
        DisposeJavaScriptTaskScheduler();
        _disposed = true;
        var resizeStyleReconciliationCancellation = _resizeStyleReconciliationCancellation;
        _resizeStyleReconciliationCancellation = null;
        resizeStyleReconciliationCancellation?.Cancel();
        TopLevel.Closed -= OnTopLevelClosed;
        if (_subscribedDocumentViewport is not null) _subscribedDocumentViewport.PropertyChanged -= OnViewportPropertyChanged;
        if (_viewportResizeSubscriptionSetup)
        {
            TopLevel.PropertyChanged -= OnViewportPropertyChanged;
            TopLevel.LayoutUpdated -= OnTopLevelLayoutUpdated;
        }
        _windowObject?.Dispose();
        _windowObject = null;
        Document.DisposeExternalBrowsingContexts();
        Document.DisposeFontFaces();
        _backend.Dispose();
        _hostServices.ResourceLoader.ClearSearchDirectories();
        _hostServices.Dispose();
        ExternalCallbackAdapter = null;
        ExternalVirtualBrowsingContextFactory = null;
    }

    private static TimeSpan TicksToTimeSpan(long ticks) => TimeSpan.FromSeconds((double)ticks / Stopwatch.Frequency);

    private readonly record struct EventListenerMetric(int Calls, long ElapsedTicks, long AllocatedBytes);

    public class WindowJs : IDisposable
    {
        private readonly AvaloniaBrowserHost _host;
        private NavigatorJs? _navigator;
        private ScreenJs? _screen;

        public WindowJs(AvaloniaBrowserHost host) => _host = host;

        internal int PendingTimerCount => _host.PendingTimerCount;
        public NavigatorJs navigator => _navigator ??= new NavigatorJs(_host);
        public LocationJs location => _host.Location;
        public double devicePixelRatio => _host.Services.Viewport.Metrics.DeviceScaleFactor;
        public double innerWidth => GetClientWidth();
        public double innerHeight => GetClientHeight();
        public double outerWidth => GetClientWidth();
        public double outerHeight => GetClientHeight();
        public ScreenJs screen => _screen ??= new(GetClientWidth, GetClientHeight);
        public MediaQueryListJs matchMedia(string query) => new(query, GetClientWidth, GetClientHeight, () => devicePixelRatio);

        public int setTimeout(object callback) => setTimeout(callback, 0);
        public int setTimeout(object callback, double milliseconds) => setTimeout(callback, (int)milliseconds);
        public int setTimeout(object callback, int milliseconds)
        {
            var external = callback as IExternalJavaScriptCallback ?? _host.ExternalCallbackAdapter?.GetCallback(callback, create: true);
            return external is null ? 0 : _host.SetTimeout(external, milliseconds);
        }

        public void clearTimeout(int id) => _host.ClearTimeout(id);

        public int setInterval(object callback) => setInterval(callback, 0);
        public int setInterval(object callback, double milliseconds) => setInterval(callback, (int)milliseconds);
        public int setInterval(object callback, int milliseconds)
        {
            var external = callback as IExternalJavaScriptCallback ?? _host.ExternalCallbackAdapter?.GetCallback(callback, create: true);
            return external is null ? 0 : _host.SetInterval(external, milliseconds);
        }

        public void clearInterval(int id) => _host.ClearInterval(id);

        public int requestAnimationFrame(object callback)
        {
            var external = callback as IExternalJavaScriptCallback ?? _host.ExternalCallbackAdapter?.GetCallback(callback, create: true);
            return external is null ? 0 : _host.RequestAnimationFrame(external);
        }

        public void cancelAnimationFrame(int id) => _host.CancelAnimationFrame(id);

        public void queueMicrotask(object callback)
        {
            var external = callback as IExternalJavaScriptCallback
                           ?? _host.ExternalCallbackAdapter?.GetCallback(callback, create: true);
            if (external is not null) _host.QueueMicrotask(external);
        }

        public CssComputedStyle getComputedStyle(object? element) => _host.Document.getComputedStyle(element);

        private double GetClientWidth()
        {
            _host.EnsureViewportSubscription();
            return _host.Services.Viewport.Metrics.ClientSize.Width;
        }

        private double GetClientHeight()
        {
            _host.EnsureViewportSubscription();
            return _host.Services.Viewport.Metrics.ClientSize.Height;
        }

        public void Dispose()
            => _host.CancelAllTimers();
    }

    public sealed class MediaQueryListJs
    {
        private readonly Func<double> _width;
        private readonly Func<double> _height;
        private readonly Func<double> _ratio;
        public MediaQueryListJs(string media, Func<double> width, Func<double> height, Func<double> ratio)
        {
            this.media = media ?? string.Empty;
            _width = width;
            _height = height;
            _ratio = ratio;
        }
        public string media { get; }
        public bool matches => Evaluate(media, _width(), _height(), _ratio());
        public object? onchange { get; set; }
        public void addListener(object listener) { }
        public void removeListener(object listener) { }
        public void addEventListener(string type, object listener) { }
        public void removeEventListener(string type, object listener) { }
        public bool dispatchEvent(object? evt) => false;

        internal static bool Evaluate(string query, double width, double height, double devicePixelRatio)
            => CssMediaQueryEvaluator.Matches(
                query,
                new CssMediaEnvironment(width, height, devicePixelRatio));
    }

    public sealed class ScreenJs
    {
        private readonly Func<double> _width;
        private readonly Func<double> _height;

        public ScreenJs(double width, double height)
            : this(() => width, () => height)
        {
        }

        internal ScreenJs(Func<double> width, Func<double> height)
        {
            _width = width;
            _height = height;
        }
        public double width => Math.Max(0, _width());
        public double height => Math.Max(0, _height());
        public double availWidth => width;
        public double availHeight => height;
        public int colorDepth { get; } = 24;
        public int pixelDepth { get; } = 24;
    }

    public sealed class NavigatorJs
    {
        private readonly AvaloniaBrowserHost _host;
        private ClipboardJs? _clipboard;
        private StorageManagerJs? _storage;
        internal NavigatorJs(AvaloniaBrowserHost host) => _host = host;
        public string userAgent { get; } = $"JavaScript.Avalonia/{typeof(AvaloniaBrowserHost).Assembly.GetName().Version}";
        public string platform { get; } = RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "MacIntel" : RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "Win32" : RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? "Linux" : RuntimeInformation.OSDescription;
        public ClipboardJs clipboard => _clipboard ??= new ClipboardJs(_host);
        public StorageManagerJs storage => _storage ??= new StorageManagerJs();
    }

    public sealed class StorageManagerJs
    {
        public Task<StorageEstimateJs> estimate() => Task.FromResult(new StorageEstimateJs { quota = 1024d * 1024d * 1024d });
        public Task<bool> persist() => Task.FromResult(false);
        public Task<bool> persisted() => Task.FromResult(false);
    }

    public sealed class StorageEstimateJs
    {
        public double usage { get; init; }
        public double quota { get; init; }
    }

    public sealed class ClipboardJs
    {
        private readonly AvaloniaBrowserHost _host;
        internal ClipboardJs(AvaloniaBrowserHost host) => _host = host;
        public string readText() => _host.Services.Clipboard.GetText() ?? string.Empty;
        public bool writeText(string? text)
        {
            try
            {
                _host.Services.Clipboard.SetText(text);
                return true;
            }
            catch { return false; }
        }

        public bool writeBase64(string format, string base64)
        {
            try
            {
                _host.Services.Clipboard.SetData(format, Convert.FromBase64String(base64));
                return true;
            }
            catch
            {
                return false;
            }
        }
    }

    public sealed class LocationJs
    {
        private readonly AvaloniaBrowserHost _host;
        private string? _href;
        private Uri? Uri => System.Uri.TryCreate(href, UriKind.Absolute, out var uri) ? uri : null;
        public LocationJs(AvaloniaBrowserHost host) => _host = host;
        public string href
        {
            get
            {
                if (!string.IsNullOrEmpty(_href)) return _href;
                var path = Path.GetFullPath(string.IsNullOrWhiteSpace(_host.ScriptBaseDirectory) ? AppContext.BaseDirectory : _host.ScriptBaseDirectory);
                if (!path.EndsWith(Path.DirectorySeparatorChar)) path += Path.DirectorySeparatorChar;
                return _href = new Uri(path).AbsoluteUri;
            }
            set => _href = value;
        }
        public string protocol { get => Uri?.Scheme + ":" ?? string.Empty; set { } }
        public string host { get => Uri?.Authority ?? string.Empty; set { } }
        public string hostname { get => Uri?.Host ?? string.Empty; set { } }
        public string port { get => Uri is { IsDefaultPort: false } uri ? uri.Port.ToString(CultureInfo.InvariantCulture) : string.Empty; set { } }
        public string pathname { get => Uri?.AbsolutePath ?? string.Empty; set { } }
        public string origin { get => Uri is { } uri ? uri.Scheme + "://" + uri.Authority : "null"; set { } }
        public string search { get => Uri?.Query ?? string.Empty; set { } }
        public string hash { get => Uri?.Fragment ?? string.Empty; set { } }
        public void replace(string url) => href = url;
        public void reload() { }
        public override string ToString() => href;
    }

    public sealed class ScriptElementJs
    {
        internal ScriptElementJs(string src, string textContent = "", object? ownerDocument = null, IReadOnlyDictionary<string, string>? attributes = null)
        {
            this.src = src;
            this.textContent = textContent;
            this.ownerDocument = ownerDocument;
            _attributes = attributes;
        }
        private readonly IReadOnlyDictionary<string, string>? _attributes;
        public string src { get; }
        public string textContent { get; }
        public object? ownerDocument { get; }
        public string tagName => "SCRIPT";
        public string nodeName => "SCRIPT";
        public int nodeType => 1;
        public string nonce => getAttribute("nonce") ?? string.Empty;
        public string? getAttribute(string name)
        {
            if (_attributes is not null)
            {
                foreach (var attribute in _attributes)
                {
                    if (string.Equals(attribute.Key, name, StringComparison.OrdinalIgnoreCase)) return attribute.Value;
                }
            }
            return string.Equals(name, "src", StringComparison.OrdinalIgnoreCase) ? src : null;
        }
        public bool hasAttribute(string name) => getAttribute(name) is not null;
    }

    public sealed class UrlJs
    {
        private static readonly System.Collections.Concurrent.ConcurrentDictionary<string, ObjectUrlValue> s_objectUrls = new(StringComparer.Ordinal);
        private readonly Uri _uri;
        public UrlJs(string url) => _uri = new Uri(url, UriKind.RelativeOrAbsolute);
        public UrlJs(string url, string? @base) => _uri = string.IsNullOrEmpty(@base) ? new Uri(url, UriKind.RelativeOrAbsolute) : new Uri(new Uri(@base, UriKind.RelativeOrAbsolute), url);
        public string href => _uri.IsAbsoluteUri ? _uri.AbsoluteUri : _uri.ToString();
        public string protocol => _uri.IsAbsoluteUri ? _uri.Scheme + ":" : string.Empty;
        public string host => _uri.IsAbsoluteUri ? _uri.Authority : string.Empty;
        public string hostname => _uri.IsAbsoluteUri ? _uri.Host : string.Empty;
        public string port => _uri.IsAbsoluteUri && !_uri.IsDefaultPort ? _uri.Port.ToString(CultureInfo.InvariantCulture) : string.Empty;
        public string pathname => _uri.IsAbsoluteUri ? _uri.AbsolutePath : _uri.ToString();
        public string search => _uri.IsAbsoluteUri ? _uri.Query : string.Empty;
        public string hash => _uri.IsAbsoluteUri ? _uri.Fragment : string.Empty;
        public string origin => _uri.IsAbsoluteUri ? _uri.Scheme + "://" + _uri.Authority : "null";
        public static string createObjectURLText(string? text)
        {
            var url = "blob:webscene-" + Guid.NewGuid();
            s_objectUrls[url] = new ObjectUrlValue(Encoding.UTF8.GetBytes(text ?? string.Empty), "text/plain");
            return url;
        }
        public static string createObjectURLBase64(string? base64, string? contentType)
        {
            var url = "blob:webscene-" + Guid.NewGuid();
            s_objectUrls[url] = new ObjectUrlValue(
                Convert.FromBase64String(base64 ?? string.Empty),
                string.IsNullOrWhiteSpace(contentType) ? "application/octet-stream" : contentType);
            return url;
        }
        public static void revokeObjectURL(string url)
        {
            if (!string.IsNullOrWhiteSpace(url)) s_objectUrls.TryRemove(url, out _);
        }
        internal static bool TryGetObjectUrlText(string url, out string text)
        {
            var suffix = url.IndexOfAny(['?', '#']);
            if (suffix >= 0) url = url[..suffix];
            if (s_objectUrls.TryGetValue(url, out var value))
            {
                text = Encoding.UTF8.GetString(value.Content);
                return true;
            }
            text = string.Empty;
            return false;
        }
        internal static bool TryGetObjectUrlData(string url, out byte[] content, out string contentType)
        {
            var suffix = url.IndexOfAny(['?', '#']);
            if (suffix >= 0) url = url[..suffix];
            if (s_objectUrls.TryGetValue(url, out var value))
            {
                content = value.Content.ToArray();
                contentType = value.ContentType;
                return true;
            }
            content = Array.Empty<byte>();
            contentType = string.Empty;
            return false;
        }
        internal static bool TryGetObjectUrlRelativePath(string url, out string relativePath)
        {
            foreach (var objectUrl in s_objectUrls.Keys)
            {
                if (url.Length <= objectUrl.Length || !url.StartsWith(objectUrl, StringComparison.Ordinal)) continue;
                relativePath = url[objectUrl.Length..].TrimStart('/', '\\');
                return !string.IsNullOrWhiteSpace(relativePath);
            }
            relativePath = string.Empty;
            return false;
        }
        public override string ToString() => href;

        private sealed record ObjectUrlValue(byte[] Content, string ContentType);
    }
}
