using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using System.Runtime.InteropServices;
using WebScene.Core;
using WebScene.Sdk;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

namespace WebScene.Sdk.Avalonia;

/// <summary>XAML-first host for one isolated packaged WebScene component instance.</summary>
public sealed class WebSceneComponentHost : ContentControl, IDisposable
{
    public static readonly StyledProperty<string?> PackagePathProperty =
        AvaloniaProperty.Register<WebSceneComponentHost, string?>(nameof(PackagePath));

    public static readonly StyledProperty<bool> AutoMountProperty =
        AvaloniaProperty.Register<WebSceneComponentHost, bool>(nameof(AutoMount), defaultValue: true);

    private static readonly WebSceneSharedAssetCache s_assetCache = new();
    private readonly List<IWebSceneHostCapabilityHandler> _handlers = [];
    private readonly WebSceneDiagnosticCollector _diagnostics = new();
    private AvaloniaBrowserHost? _browserHost;
    private ClearScriptV8Runtime? _runtime;
    private WebSceneJavaScriptHostBridgeAdapter? _bridgeAdapter;
    private WebSceneComponentInstance? _instance;
    private bool _disposed;

    public string? PackagePath
    {
        get => GetValue(PackagePathProperty);
        set => SetValue(PackagePathProperty, value);
    }

    public bool AutoMount
    {
        get => GetValue(AutoMountProperty);
        set => SetValue(AutoMountProperty, value);
    }

    public WebSceneComponentState? ComponentState => _instance?.State;

    public IReadOnlyList<WebSceneSdkDiagnostic> Diagnostics => _diagnostics.Diagnostics;

    public event EventHandler<WebSceneSdkDiagnostic>? DiagnosticReported;

    public void RegisterHostCapability(IWebSceneHostCapabilityHandler handler)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(handler);
        if (_runtime is not null)
        {
            throw new InvalidOperationException("Host capabilities must be registered before mounting the component.");
        }
        if (_handlers.Any(existing => string.Equals(existing.Capability, handler.Capability, StringComparison.Ordinal)))
        {
            throw new InvalidOperationException($"Capability '{handler.Capability}' is already registered.");
        }
        _handlers.Add(handler);
    }

    public void MountComponent()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_instance?.State == WebSceneComponentState.Mounted)
        {
            return;
        }
        var packagePath = PackagePath;
        if (string.IsNullOrWhiteSpace(packagePath))
        {
            throw new InvalidOperationException("PackagePath is required before mounting an WebScene component.");
        }
        var topLevel = TopLevel.GetTopLevel(this)
                       ?? throw new InvalidOperationException("The component host must be attached to a TopLevel before mounting.");
        var root = Content as Control;
        if (root is null)
        {
            root = new Panel();
            Content = root;
        }

        var package = WebSceneComponentPackage.Open(ResolvePackagePath(packagePath), s_assetCache);
        EnsureBackendCapabilities(package.Manifest);
        var entryPoint = package.GetEntryPoint();
        var source = System.Text.Encoding.UTF8.GetString(entryPoint.Content.Span);
        var compatibility = WebSceneCompatibilityChecker.Check(source, package.Manifest, package.Manifest.EntryPoint);
        foreach (var diagnostic in compatibility.Diagnostics)
        {
            Report(new WebSceneSdkDiagnostic(
                diagnostic.Code,
                diagnostic.Severity == WebSceneCompatibilitySeverity.Error ? WebSceneDiagnosticSeverity.Error : WebSceneDiagnosticSeverity.Warning,
                $"{diagnostic.Source}:{diagnostic.Line}:{diagnostic.Column} {diagnostic.Message}",
                package.Manifest.Id));
        }
        if (!compatibility.IsCompatible)
        {
            throw new InvalidDataException($"Component '{package.Manifest.Id}' uses APIs outside WebScene Component Profile 1.");
        }

        try
        {
            _browserHost = new AvaloniaBrowserHost(topLevel, host => new ComponentDocument(host, root));
            _runtime = new ClearScriptV8Runtime(_browserHost);
            var bridge = new WebSceneHostBridge(package.Manifest, _handlers, new ForwardingDiagnosticSink(this));
            _bridgeAdapter = new WebSceneJavaScriptHostBridgeAdapter(bridge, _runtime, _browserHost.Services.Dispatcher);
            _runtime.Engine.AddHostObject("__webSceneHostBridge", _bridgeAdapter);
            _runtime.Execute(WebSceneHostBridgeBootstrap.Script, "webscene-host-bridge.js");
            _runtime.Execute(source, package.Manifest.EntryPoint);
            _instance = package.CreateInstance(new ForwardingDiagnosticSink(this));
            _instance.Mount();
            Report(new WebSceneSdkDiagnostic(
                "component.asset",
                WebSceneDiagnosticSeverity.Info,
                $"Loaded {package.Manifest.EntryPoint} ({entryPoint.Content.Length} bytes, sha256 {entryPoint.Sha256}).",
                package.Manifest.Id));
            Report(new WebSceneSdkDiagnostic(
                "runtime.native",
                WebSceneDiagnosticSeverity.Info,
                $"RID {RuntimeInformation.RuntimeIdentifier}; ClearScript {typeof(ClearScriptV8Runtime).Assembly.GetName().Version}; native override '{Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE") ?? "<package resolver>"}'.",
                package.Manifest.Id));
            _runtime.Execute(
                $"globalThis[{System.Text.Json.JsonSerializer.Serialize(package.Manifest.Lifecycle.MountExport)}]?.({{ instanceId: {System.Text.Json.JsonSerializer.Serialize(_instance.InstanceId.ToString("D"))} }});",
                "webscene-component-mount.js");
        }
        catch
        {
            UnloadComponent(invokeLifecycle: false);
            throw;
        }
    }

    public void UnmountComponent() => UnloadComponent(invokeLifecycle: true);

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        if (AutoMount && _runtime is null)
        {
            MountComponent();
        }
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        UnloadComponent(invokeLifecycle: true);
        base.OnDetachedFromVisualTree(e);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        UnloadComponent(invokeLifecycle: true);
        _disposed = true;
        GC.SuppressFinalize(this);
    }

    private void UnloadComponent(bool invokeLifecycle)
    {
        if (invokeLifecycle && _runtime is not null && _instance is not null)
        {
            var export = _instance.Package.Manifest.Lifecycle.UnmountExport;
            try
            {
                _runtime.Execute($"globalThis[{System.Text.Json.JsonSerializer.Serialize(export)}]?.();", "webscene-component-unmount.js");
            }
            catch (Exception exception)
            {
                Report(new WebSceneSdkDiagnostic("component.unmount.error", WebSceneDiagnosticSeverity.Error, exception.Message, _instance.Package.Manifest.Id));
            }
        }
        if (_instance?.State == WebSceneComponentState.Mounted)
        {
            _instance.Unmount();
        }
        CaptureRuntimeDiagnostics();
        _instance?.Dispose();
        _instance = null;
        _bridgeAdapter?.Dispose();
        _bridgeAdapter = null;
        _runtime?.Dispose();
        _runtime = null;
        _browserHost?.Dispose();
        _browserHost = null;
    }

    private void CaptureRuntimeDiagnostics()
    {
        if (_runtime is not null)
        {
            var cache = _runtime.SharedCacheMetrics;
            Report(new WebSceneSdkDiagnostic(
                "runtime.cache",
                WebSceneDiagnosticSeverity.Info,
                $"Source cache {cache.SourceHits} hits/{cache.SourceMisses} misses; code cache {cache.CodeHits} hits/{cache.CodeMisses} misses; {cache.CodeBytes} bytes.",
                _instance?.Package.Manifest.Id));
        }
        if (_browserHost is null)
        {
            return;
        }
        foreach (var exception in _browserHost.JavaScriptExceptionDiagnostics)
        {
            Report(new WebSceneSdkDiagnostic("runtime.script", WebSceneDiagnosticSeverity.Error, exception, _instance?.Package.Manifest.Id));
        }
        var budget = _browserHost.GetUiThreadWorkBudgetMetrics();
        if (budget.JavaScriptOverruns + budget.CssOverruns + budget.LayoutOverruns > 0)
        {
            Report(new WebSceneSdkDiagnostic(
                "runtime.longtask",
                WebSceneDiagnosticSeverity.Warning,
                $"UI budget {budget.Budget.TotalMilliseconds:F1} ms: JavaScript {budget.JavaScriptOverruns}, CSS {budget.CssOverruns}, layout {budget.LayoutOverruns} overruns.",
                _instance?.Package.Manifest.Id));
        }
        foreach (var diagnostic in _browserHost.Backend.Diagnostics)
        {
            Report(new WebSceneSdkDiagnostic(
                "runtime.backend",
                WebSceneDiagnosticSeverity.Warning,
                $"{diagnostic.Category}: {diagnostic.Message}",
                _instance?.Package.Manifest.Id));
        }
    }

    private void Report(in WebSceneSdkDiagnostic diagnostic)
    {
        _diagnostics.Report(diagnostic);
        DiagnosticReported?.Invoke(this, diagnostic);
    }

    private static string ResolvePackagePath(string path)
        => Path.IsPathRooted(path) ? path : Path.Combine(AppContext.BaseDirectory, path);

    private static void EnsureBackendCapabilities(WebSceneComponentManifest manifest)
    {
        var required = WebSceneBackendCapabilities.None;
        foreach (var capability in manifest.Capabilities)
        {
            required |= capability switch
            {
                WebSceneComponentCapabilities.Dom => WebSceneBackendCapabilities.DomProjection,
                WebSceneComponentCapabilities.CssLayout => WebSceneBackendCapabilities.CssLayout,
                WebSceneComponentCapabilities.Canvas2D => WebSceneBackendCapabilities.Canvas2D,
                WebSceneComponentCapabilities.Svg => WebSceneBackendCapabilities.Svg,
                WebSceneComponentCapabilities.Pointer => WebSceneBackendCapabilities.PointerInput,
                WebSceneComponentCapabilities.Keyboard => WebSceneBackendCapabilities.KeyboardInput,
                WebSceneComponentCapabilities.Focus => WebSceneBackendCapabilities.Focus,
                WebSceneComponentCapabilities.Clipboard => WebSceneBackendCapabilities.Clipboard,
                _ => WebSceneBackendCapabilities.None
            };
        }
        var available = WebScene.Backends.Avalonia.AvaloniaBackendHost.DefaultCapabilities;
        if ((required & ~available) != WebSceneBackendCapabilities.None)
        {
            throw new WebSceneBackendCapabilityException(required, available);
        }
    }

    private sealed class ComponentDocument(AvaloniaBrowserHost host, Control root) : AvaloniaDomDocument(host)
    {
        protected override Control? GetDocumentRoot() => root;
    }

    private sealed class ForwardingDiagnosticSink(WebSceneComponentHost owner) : IWebSceneDiagnosticSink
    {
        public void Report(in WebSceneSdkDiagnostic diagnostic) => owner.Report(diagnostic);
    }
}
