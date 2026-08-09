using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WebScene.Backends.Uno.Native;
using WebScene.Backends.Native;
using WebScene.JavaScript.Interop;
using WebScene.Sdk.NativeHost.Internal;

namespace WebScene.Sdk.Uno;

public enum WebSceneComponentHostState
{
    Idle,
    Mounting,
    Mounted,
    Unmounting,
    Faulted,
    Disposed
}

public sealed class WebSceneComponentHostStateChangedEventArgs(
    WebSceneComponentHostState previousState,
    WebSceneComponentHostState state) : EventArgs
{
    public WebSceneComponentHostState PreviousState { get; } = previousState;

    public WebSceneComponentHostState State { get; } = state;
}

public sealed class WebSceneComponentHostFailedEventArgs(Exception exception)
    : EventArgs
{
    public Exception Exception { get; } = exception;
}

public sealed class WebSceneSdkDiagnosticEventArgs(WebSceneSdkDiagnostic diagnostic)
    : EventArgs
{
    public WebSceneSdkDiagnostic Diagnostic { get; } = diagnostic;
}

public sealed class WebSceneComponentCompatibilityException(
    WebSceneCompatibilityReport report)
    : Exception("The component is not compatible with WebScene Component Profile 1.")
{
    public WebSceneCompatibilityReport Report { get; } = report;
}

/// <summary>
/// Reusable Uno Platform control that validates, loads, mounts, and isolates one
/// packaged WebScene component in the native ABI 3 runtime.
/// </summary>
public sealed class WebSceneComponentHost : ContentControl, IAsyncDisposable
{
    public static readonly DependencyProperty PackagePathProperty =
        DependencyProperty.Register(
            nameof(PackagePath),
            typeof(string),
            typeof(WebSceneComponentHost),
            new PropertyMetadata(null));

    public static readonly DependencyProperty NativeLibraryPathProperty =
        DependencyProperty.Register(
            nameof(NativeLibraryPath),
            typeof(string),
            typeof(WebSceneComponentHost),
            new PropertyMetadata(null));

    public static readonly DependencyProperty CompilationCacheDirectoryProperty =
        DependencyProperty.Register(
            nameof(CompilationCacheDirectory),
            typeof(string),
            typeof(WebSceneComponentHost),
            new PropertyMetadata(null));

    public static readonly DependencyProperty AutoMountProperty =
        DependencyProperty.Register(
            nameof(AutoMount),
            typeof(bool),
            typeof(WebSceneComponentHost),
            new PropertyMetadata(true));

    private static readonly UTF8Encoding s_strictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private static readonly WebSceneSharedAssetCache s_assetCache = new();
    private const string NativeBridgeAdapter = """
        (() => {
          const nativeBridge = globalThis.__webSceneNativeHostBridge;
          if (!nativeBridge) throw new Error('The native WebScene host bridge is unavailable.');
          globalThis.__webSceneHostBridge = Object.freeze({
            invoke(request, resolve, reject) {
              Promise.resolve(nativeBridge.invoke(request)).then(resolve, reject);
            },
            cancel(requestId) { nativeBridge.cancel(requestId); }
          });
        })();
        """;

    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly Dictionary<string, IWebSceneHostCapabilityHandler> _handlers =
        new(StringComparer.Ordinal);
    private readonly WebSceneDiagnosticCollector _diagnostics = new();
    private readonly ForwardingDiagnosticSink _diagnosticSink;
    private CancellationTokenSource? _operationCancellation;
    private WebSceneComponentInstance? _instance;
    private NativeComponentBridgeSession? _bridgeSession;
    private WebSceneComponentHostState _state;
    private bool _disposed;

    public WebSceneComponentHost()
    {
        View = new UnoNativeWebSceneView();
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
        Content = View;
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
        _diagnosticSink = new ForwardingDiagnosticSink(this, _diagnostics);
    }

    public string? PackagePath
    {
        get => (string?)GetValue(PackagePathProperty);
        set => SetValue(PackagePathProperty, value);
    }

    public string? NativeLibraryPath
    {
        get => (string?)GetValue(NativeLibraryPathProperty);
        set => SetValue(NativeLibraryPathProperty, value);
    }

    public string? CompilationCacheDirectory
    {
        get => (string?)GetValue(CompilationCacheDirectoryProperty);
        set => SetValue(CompilationCacheDirectoryProperty, value);
    }

    public bool AutoMount
    {
        get => (bool)GetValue(AutoMountProperty);
        set => SetValue(AutoMountProperty, value);
    }

    /// <summary>Scripts supplied by the application before the package is mounted.</summary>
    public IReadOnlyList<WebSceneDocumentScript> DocumentStartScripts { get; set; } = [];

    /// <summary>Optional Inspector/startup hook run before document navigation.</summary>
    public Func<UnoNativeWebSceneView, CancellationToken, ValueTask>? BeforeNavigationAsync { get; set; }

    public UnoNativeWebSceneView View { get; }

    public WebSceneComponentHostState State => _state;

    public WebSceneComponentPackage? ComponentPackage => _instance?.Package;

    public WebSceneComponentInstance? ComponentInstance => _instance;

    public WebSceneCompatibilityReport? CompatibilityReport { get; private set; }

    public Exception? LastException { get; private set; }

    public IReadOnlyList<WebSceneSdkDiagnostic> Diagnostics => _diagnostics.Diagnostics;

    public event EventHandler<WebSceneComponentHostStateChangedEventArgs>? StateChanged;

    public event EventHandler? ComponentMounted;

    public event EventHandler? ComponentUnmounted;

    public event EventHandler<WebSceneComponentHostFailedEventArgs>? MountFailed;

    public event EventHandler<WebSceneSdkDiagnosticEventArgs>? DiagnosticReported;

    /// <summary>
    /// Grants a capability implementation to subsequently mounted components.
    /// The component must also declare the capability in its manifest.
    /// </summary>
    public void RegisterHostCapability(IWebSceneHostCapabilityHandler handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_state is WebSceneComponentHostState.Mounting
            or WebSceneComponentHostState.Mounted
            or WebSceneComponentHostState.Unmounting)
        {
            throw new InvalidOperationException(
                "Host capabilities can be changed only while the component is idle or faulted.");
        }
        _handlers[handler.Capability] = handler;
    }

    public bool RemoveHostCapability(string capability)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(capability);
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_state is WebSceneComponentHostState.Mounting
            or WebSceneComponentHostState.Mounted
            or WebSceneComponentHostState.Unmounting)
        {
            throw new InvalidOperationException(
                "Host capabilities can be changed only while the component is idle or faulted.");
        }
        return _handlers.Remove(capability);
    }

    public async Task MountAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycleGate.WaitAsync(cancellationToken);
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_state == WebSceneComponentHostState.Mounted)
            {
                return;
            }

            SetState(WebSceneComponentHostState.Mounting);
            LastException = null;
            CompatibilityReport = null;
            using var operation =
                CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            _operationCancellation = operation;
            try
            {
                await MountCoreAsync(operation.Token);
                SetState(WebSceneComponentHostState.Mounted);
                ComponentMounted?.Invoke(this, EventArgs.Empty);
            }
            catch (Exception error)
            {
                LastException = error;
                await CleanupCoreAsync(invokeUnmount: false, CancellationToken.None);
                SetState(WebSceneComponentHostState.Faulted);
                Report(
                    "component.mount.failed",
                    WebSceneDiagnosticSeverity.Error,
                    error.Message);
                MountFailed?.Invoke(
                    this,
                    new WebSceneComponentHostFailedEventArgs(error));
                throw;
            }
            finally
            {
                _operationCancellation = null;
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    public async Task UnmountAsync(CancellationToken cancellationToken = default)
    {
        _operationCancellation?.Cancel();
        await _lifecycleGate.WaitAsync(cancellationToken);
        try
        {
            if (_disposed || _state is WebSceneComponentHostState.Idle)
            {
                return;
            }
            SetState(WebSceneComponentHostState.Unmounting);
            Exception? failure = null;
            try
            {
                await CleanupCoreAsync(invokeUnmount: true, cancellationToken);
            }
            catch (Exception error)
            {
                failure = error;
                LastException = error;
                Report(
                    "component.unmount.failed",
                    WebSceneDiagnosticSeverity.Error,
                    error.Message);
            }
            SetState(failure is null
                ? WebSceneComponentHostState.Idle
                : WebSceneComponentHostState.Faulted);
            ComponentUnmounted?.Invoke(this, EventArgs.Empty);
            if (failure is not null)
            {
                throw failure;
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    public async Task ReloadAsync(CancellationToken cancellationToken = default)
    {
        await UnmountAsync(cancellationToken);
        await MountAsync(cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed)
        {
            return;
        }
        _operationCancellation?.Cancel();
        await _lifecycleGate.WaitAsync();
        try
        {
            if (_disposed)
            {
                return;
            }
            try
            {
                await CleanupCoreAsync(invokeUnmount: true, CancellationToken.None);
            }
            finally
            {
                Loaded -= OnLoaded;
                Unloaded -= OnUnloaded;
                await View.DisposeAsync();
                _disposed = true;
                SetState(WebSceneComponentHostState.Disposed);
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    public static string ResolveNativeLibraryPath(string? configuredPath = null)
    {
        var path = string.IsNullOrWhiteSpace(configuredPath)
            ? Environment.GetEnvironmentVariable("WEBSCENE_NATIVE_ENGINE_LIBRARY")
            : configuredPath;
        if (!string.IsNullOrWhiteSpace(path))
        {
            return Path.GetFullPath(path);
        }

        var packaged = Path.Combine(AppContext.BaseDirectory, NativeLibraryFileName());
        if (File.Exists(packaged))
        {
            return packaged;
        }
        throw new FileNotFoundException(
            "The WebScene native engine was not found. Set NativeLibraryPath or "
            + "WEBSCENE_NATIVE_ENGINE_LIBRARY, or deploy the native runtime package.",
            packaged);
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        if (AutoMount
            && _state is WebSceneComponentHostState.Idle
                or WebSceneComponentHostState.Faulted)
        {
            _ = RunAutomaticMountAsync();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        if (!_disposed && _state != WebSceneComponentHostState.Idle)
        {
            _ = RunAutomaticUnmountAsync();
        }
    }

    private async Task MountCoreAsync(CancellationToken cancellationToken)
    {
        var packagePath = ResolvePackagePath(PackagePath);
        var package = WebSceneComponentPackage.Open(packagePath, s_assetCache);
        var entryPoint = package.GetEntryPoint();
        string source;
        try
        {
            source = s_strictUtf8.GetString(entryPoint.Content.Span);
        }
        catch (DecoderFallbackException error)
        {
            throw new InvalidDataException(
                $"Component entry point '{package.Manifest.EntryPoint}' is not UTF-8 text.",
                error);
        }

        CompatibilityReport = WebSceneCompatibilityChecker.Check(
            source,
            package.Manifest,
            package.Manifest.EntryPoint);
        foreach (var diagnostic in CompatibilityReport.Diagnostics)
        {
            Report(
                diagnostic.Code,
                diagnostic.Severity == WebSceneCompatibilitySeverity.Error
                    ? WebSceneDiagnosticSeverity.Error
                    : WebSceneDiagnosticSeverity.Warning,
                $"{diagnostic.Source}({diagnostic.Line},{diagnostic.Column}): {diagnostic.Message}",
                package.Manifest.Id);
        }
        if (!CompatibilityReport.IsCompatible)
        {
            throw new WebSceneComponentCompatibilityException(CompatibilityReport);
        }

        var instance = package.CreateInstance(_diagnosticSink);
        _instance = instance;
        var resources = new WebSceneComponentResourceLoader(package, instance.InstanceId);
        await View.LoadAsync(
            new NativeWebSceneLoadOptions
            {
                Source = resources.DocumentUrl,
                NativeLibraryPath = ResolveNativeLibraryPath(NativeLibraryPath),
                CompilationCacheDirectory = CompilationCacheDirectory,
                DocumentStartScripts = DocumentStartScripts,
                ResourceLoader = resources
            },
            BeforeNavigationAsync,
            documentBarrierTimeout: null,
            cancellationToken);

        var bridge = new WebSceneHostBridge(
            package.Manifest,
            _handlers.Values,
            _diagnosticSink);
        _bridgeSession = await NativeComponentBridgeSession.CreateAsync(
            View.CreateJavaScriptInvoker(),
            bridge,
            cancellationToken);
        await EvaluateDiscardAsync(
            NativeBridgeAdapter + "\n" + WebSceneHostBridgeBootstrap.Script,
            "webscene-component-host-bootstrap.js",
            cancellationToken);
        await EvaluateDiscardAsync(
            source,
            resources.GetAssetUrl(package.Manifest.EntryPoint),
            cancellationToken);
        await EvaluateDiscardAsync(
            CreateLifecycleInvocation(
                package.Manifest.Lifecycle.MountExport,
                instance.InstanceId,
                includeOptions: true),
            "webscene-component-mount.js",
            cancellationToken);
        instance.Mount();
    }

    private async Task CleanupCoreAsync(
        bool invokeUnmount,
        CancellationToken cancellationToken)
    {
        Exception? lifecycleFailure = null;
        var instance = _instance;
        if (invokeUnmount
            && instance?.State == WebSceneComponentState.Mounted
            && View.Source is not null)
        {
            try
            {
                await EvaluateDiscardAsync(
                    CreateLifecycleInvocation(
                        instance.Package.Manifest.Lifecycle.UnmountExport,
                        instance.InstanceId,
                        includeOptions: false),
                    "webscene-component-unmount.js",
                    cancellationToken);
                instance.Unmount();
            }
            catch (Exception error)
            {
                lifecycleFailure = error;
            }
        }

        var bridgeSession = Interlocked.Exchange(ref _bridgeSession, null);
        if (bridgeSession is not null)
        {
            await bridgeSession.DisposeAsync();
        }
        await View.UnloadAsync();
        _instance = null;
        instance?.Dispose();
        if (lifecycleFailure is not null)
        {
            throw lifecycleFailure;
        }
    }

    private async Task EvaluateDiscardAsync(
        string source,
        string documentName,
        CancellationToken cancellationToken)
    {
        using var result = await View.EvaluateAsync(
            source,
            documentName,
            cancellationToken);
    }

    private static string CreateLifecycleInvocation(
        string export,
        Guid instanceId,
        bool includeOptions)
    {
        var name = JsonSerializer.Serialize(export);
        var arguments = includeOptions
            ? $"{{ instanceId: {JsonSerializer.Serialize(instanceId.ToString())} }}"
            : string.Empty;
        return $$"""
            (async () => {
              const lifecycle = globalThis[{{name}}];
              if (typeof lifecycle !== 'function') {
                throw new Error('Component lifecycle export {{export}} is not a function.');
              }
              await lifecycle({{arguments}});
              return true;
            })()
            """;
    }

    private static string ResolvePackagePath(string? packagePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(packagePath);
        var resolved = Path.IsPathRooted(packagePath)
            ? Path.GetFullPath(packagePath)
            : Path.GetFullPath(packagePath, AppContext.BaseDirectory);
        if (!Directory.Exists(resolved))
        {
            throw new DirectoryNotFoundException(
                $"The WebScene component package directory does not exist: {resolved}");
        }
        return resolved;
    }

    private static string NativeLibraryFileName()
        => RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? "webscene_native_engine.dll"
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libwebscene_native_engine.dylib"
                : "libwebscene_native_engine.so";

    private void SetState(WebSceneComponentHostState state)
    {
        if (_state == state)
        {
            return;
        }
        var previous = _state;
        _state = state;
        StateChanged?.Invoke(
            this,
            new WebSceneComponentHostStateChangedEventArgs(previous, state));
    }

    private void Report(
        string code,
        WebSceneDiagnosticSeverity severity,
        string message,
        string? componentId = null)
        => _diagnosticSink.Report(new WebSceneSdkDiagnostic(
            code,
            severity,
            message,
            componentId,
            DateTimeOffset.UtcNow));

    private async Task RunAutomaticMountAsync()
    {
        try
        {
            await MountAsync();
        }
        catch
        {
            // MountFailed and diagnostics are the observable failure channels
            // for an automatic lifecycle operation.
        }
    }

    private async Task RunAutomaticUnmountAsync()
    {
        try
        {
            await UnmountAsync();
        }
        catch
        {
            // Unmount diagnostics are retained by the host.
        }
    }

    private sealed class ForwardingDiagnosticSink(
        WebSceneComponentHost owner,
        WebSceneDiagnosticCollector collector) : IWebSceneDiagnosticSink
    {
        public void Report(in WebSceneSdkDiagnostic diagnostic)
        {
            var timestamped = diagnostic with
            {
                Timestamp = diagnostic.Timestamp ?? DateTimeOffset.UtcNow
            };
            collector.Report(timestamped);
            owner.DiagnosticReported?.Invoke(
                owner,
                new WebSceneSdkDiagnosticEventArgs(timestamped));
        }
    }
}
