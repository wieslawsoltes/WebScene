using WebScene.Backends.Native;
#if WEBSCENE_UNO
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using UiControl = Microsoft.UI.Xaml.UIElement;
namespace WebScene.Backends.Uno.Native;
public sealed partial class UnoNativeWebSceneView
#else
using Avalonia.Controls;
using Avalonia.Threading;
using UiControl = Avalonia.Controls.Control;
namespace WebScene.Backends.Avalonia.Native;
public sealed partial class NativeWebSceneView
#endif
{
    private readonly NativeRuntimeDiagnostics _runtimeDiagnostics = new();

    /// <summary>Ordered background notifications, including page-initiated errors. Dispatch to the UI thread before changing UI.</summary>
    public event Action<WebSceneJavaScriptException> JavaScriptException
    {
        add => _runtimeDiagnostics.JavaScriptException += value;
        remove => _runtimeDiagnostics.JavaScriptException -= value;
    }
    /// <summary>Subscribing enables bounded console snapshots. Removing the last listener disables capture unless explicitly enabled.</summary>
    public event Action<WebSceneConsoleMessage> ConsoleMessage
    {
        add => _runtimeDiagnostics.ConsoleMessage += value;
        remove => _runtimeDiagnostics.ConsoleMessage -= value;
    }
    public event Action<WebSceneRuntimeFailure> RuntimeFailed
    {
        add => _runtimeDiagnostics.RuntimeFailed += value;
        remove => _runtimeDiagnostics.RuntimeFailed -= value;
    }
    /// <summary>Failed host resource requests, independently of console/JS exception capture.
    /// Subscriptions should be installed before LoadAsync. Delivered on the diagnostic background dispatcher.</summary>
    public event Action<WebSceneResourceFailure> ResourceFailed
    {
        add => _runtimeDiagnostics.ResourceFailed += value;
        remove => _runtimeDiagnostics.ResourceFailed -= value;
    }
    public WebSceneRuntimeState RuntimeState => _runtimeDiagnostics.State;
    public WebSceneRuntimeFailure? LastFailure => _runtimeDiagnostics.LastFailure;
    public long DroppedDiagnosticCount => _runtimeDiagnostics.DroppedCount;
    public bool CaptureConsoleMessages { get => _runtimeDiagnostics.CaptureConsole; set => _runtimeDiagnostics.CaptureConsole = value; }
    /// <summary>Explicit opt-in for the legacy DrainConsoleMessages/TryTakeConsoleMessage pull APIs.</summary>
    public bool CaptureLegacyConsoleMessages { get => _runtimeDiagnostics.LegacyConsole; set => _runtimeDiagnostics.LegacyConsole = value; }
    /// <summary>Explicitly wait for queued diagnostics before disposing a failed startup.
    /// Use a bounded cancellation token; do not call from a diagnostic subscriber.</summary>
    public Task FlushRuntimeDiagnosticsAsync(CancellationToken cancellationToken)
        => _runtimeDiagnostics.FlushAsync(cancellationToken);
    /// <summary>Opt-in error UI. Ordinary uncaught JS errors do not replace the page.</summary>
    public bool ShowRuntimeFailure { get => _runtimeDiagnostics.Fallback; set => _runtimeDiagnostics.Fallback = value; }
    /// <summary>Optional replacement for the built-in failure UI. Called on the UI thread.</summary>
    public Func<WebSceneRuntimeFailure, UiControl>? RuntimeFailureContentFactory { get; set; }

    /// <summary>Explicitly treats an application-specific condition as terminal, stops the runtime, and retains LastFailure.</summary>
    public async Task ReportFatalFailureAsync(string message, string? stack = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        _runtimeDiagnostics.Fail(message, stack, "application", Source);
        if (LastFailure is { } failure) await StopFailedRuntimeAsync(failure);
    }

    private void InitializeRuntimeDiagnostics()
    {
        _runtimeDiagnostics.FailureStateChanged += failure => PostDiagnosticUi(() =>
        {
            _ = StopFailedRuntimeAsync(failure);
        });
    }
    private async Task StopFailedRuntimeAsync(WebSceneRuntimeFailure failure)
    {
#if WEBSCENE_UNO
        if (!DispatcherQueue.HasThreadAccess) {
            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!DispatcherQueue.TryEnqueue(async () => {
                try { await StopFailedRuntimeAsync(failure); completion.TrySetResult(); }
                catch (Exception error) { completion.TrySetException(error); }
            })) throw new InvalidOperationException("The WebScene UI dispatcher is unavailable.");
            await completion.Task;
            return;
        }
#endif
        await _lifecycleGate.WaitAsync();
        try {
            if (failure.Context.Generation != _runtimeDiagnostics.Generation || RuntimeState != WebSceneRuntimeState.Failed) return;
#if WEBSCENE_UNO
            // UnloadCoreAsync touches Uno controls; this method is normally entered from DispatcherQueue.
            await UnloadCoreAsync();
            ShowFailure(failure);
#else
            await UnloadCoreAsync().ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(() => ShowFailure(failure));
#endif
        }
        finally { _lifecycleGate.Release(); }
    }
    private void ShowFailure(WebSceneRuntimeFailure failure)
    {
        if (!ShowRuntimeFailure || failure.Context.Generation != _runtimeDiagnostics.Generation || RuntimeState != WebSceneRuntimeState.Failed) return;
        try {
            if (RuntimeFailureContentFactory is { } factory) { Content = factory(failure); return; }
            var panel = new StackPanel { Spacing = 12, Margin = new(16) };
            panel.Children.Add(new TextBlock { Text = failure.Message, TextWrapping =
#if WEBSCENE_UNO
                TextWrapping.Wrap
#else
                global::Avalonia.Media.TextWrapping.Wrap
#endif
            });
            panel.Children.Add(new Expander { Header = "Error details", IsExpanded = false,
                Content = new TextBlock { Text = failure.Stack ?? failure.Stage } });
            Content = new ScrollViewer { Content = panel };
        }
        catch (Exception error) { System.Diagnostics.Trace.TraceError("WebScene failure UI failed: {0}", error); }
    }
    private void PostDiagnosticUi(Action action)
    {
#if WEBSCENE_UNO
        DispatcherQueue.TryEnqueue(() => action());
#else
        Dispatcher.UIThread.Post(action);
#endif
    }
}
