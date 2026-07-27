using System.Text.Json;
using System.Collections.Concurrent;
using WebScene.Core;
using WebScene.JavaScript;

namespace WebScene.Sdk;

public sealed record WebSceneHostBridgeRequest(
    string RequestId,
    string Version,
    string Capability,
    string Method,
    JsonElement Arguments);

public sealed record WebSceneHostBridgeError(string Code, string Message);

public sealed record WebSceneHostBridgeResponse(
    string RequestId,
    bool Ok,
    JsonElement? Result = null,
    WebSceneHostBridgeError? Error = null);

public interface IWebSceneHostCapabilityHandler
{
    string Capability { get; }

    ValueTask<JsonElement?> InvokeAsync(string method, JsonElement arguments, CancellationToken cancellationToken);
}

public sealed class WebSceneDelegateCapabilityHandler : IWebSceneHostCapabilityHandler
{
    private readonly Func<string, JsonElement, CancellationToken, ValueTask<JsonElement?>> _handler;

    public WebSceneDelegateCapabilityHandler(
        string capability,
        Func<string, JsonElement, CancellationToken, ValueTask<JsonElement?>> handler)
    {
        if (!WebSceneComponentCapabilities.Known.Contains(capability)
            || !capability.StartsWith("host.", StringComparison.Ordinal))
        {
            throw new ArgumentException($"'{capability}' is not an WebScene host capability.", nameof(capability));
        }
        Capability = capability;
        _handler = handler ?? throw new ArgumentNullException(nameof(handler));
    }

    public string Capability { get; }

    public ValueTask<JsonElement?> InvokeAsync(
        string method,
        JsonElement arguments,
        CancellationToken cancellationToken)
        => _handler(method, arguments, cancellationToken);
}

/// <summary>Capability-gated, JSON-only async boundary between trusted components and application services.</summary>
public sealed class WebSceneHostBridge
{
    public const string CurrentVersion = "1.0";

    private static readonly JsonSerializerOptions s_jsonOptions = new(JsonSerializerDefaults.Web);
    private readonly WebSceneComponentManifest _manifest;
    private readonly IReadOnlyDictionary<string, IWebSceneHostCapabilityHandler> _handlers;
    private readonly IWebSceneDiagnosticSink? _diagnostics;

    public WebSceneHostBridge(
        WebSceneComponentManifest manifest,
        IEnumerable<IWebSceneHostCapabilityHandler> handlers,
        IWebSceneDiagnosticSink? diagnostics = null)
    {
        WebSceneComponentManifestSerializer.Validate(manifest).ThrowIfInvalid();
        ArgumentNullException.ThrowIfNull(handlers);
        _manifest = manifest;
        _diagnostics = diagnostics;
        _handlers = handlers.ToDictionary(static handler => handler.Capability, StringComparer.Ordinal);
    }

    public async ValueTask<WebSceneHostBridgeResponse> InvokeAsync(
        WebSceneHostBridgeRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (!string.Equals(request.Version, CurrentVersion, StringComparison.Ordinal))
        {
            return Error(request, "bridge.version", $"Unsupported host bridge version '{request.Version}'.");
        }
        if (string.IsNullOrWhiteSpace(request.RequestId) || string.IsNullOrWhiteSpace(request.Method))
        {
            return Error(request, "bridge.request", "requestId and method are required.");
        }
        if (!_manifest.Capabilities.Contains(request.Capability, StringComparer.Ordinal))
        {
            return Error(request, "bridge.capability.denied", $"Component '{_manifest.Id}' did not declare '{request.Capability}'.");
        }
        if (!_handlers.TryGetValue(request.Capability, out var handler))
        {
            return Error(request, "bridge.capability.unavailable", $"Host did not grant '{request.Capability}'.");
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var result = await handler.InvokeAsync(request.Method, request.Arguments, cancellationToken).ConfigureAwait(false);
            Report("bridge.completed", WebSceneDiagnosticSeverity.Info, $"Completed {request.Capability}.{request.Method}.");
            return new WebSceneHostBridgeResponse(request.RequestId, true, result);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            Report("bridge.cancelled", WebSceneDiagnosticSeverity.Info, $"Cancelled {request.Capability}.{request.Method}.");
            throw;
        }
        catch (Exception exception)
        {
            return Error(request, "bridge.handler", exception.Message);
        }
    }

    public async ValueTask<string> InvokeJsonAsync(string requestJson, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(requestJson);
        var request = JsonSerializer.Deserialize<WebSceneHostBridgeRequest>(requestJson, s_jsonOptions)
                      ?? throw new InvalidDataException("Host bridge request was empty.");
        var response = await InvokeAsync(request, cancellationToken).ConfigureAwait(false);
        return JsonSerializer.Serialize(response, s_jsonOptions);
    }

    private WebSceneHostBridgeResponse Error(WebSceneHostBridgeRequest request, string code, string message)
    {
        Report(code, WebSceneDiagnosticSeverity.Error, message);
        return new WebSceneHostBridgeResponse(request.RequestId ?? string.Empty, false, Error: new WebSceneHostBridgeError(code, message));
    }

    private void Report(string code, WebSceneDiagnosticSeverity severity, string message)
        => _diagnostics?.Report(new WebSceneSdkDiagnostic(code, severity, message, _manifest.Id));
}

/// <summary>Callback adapter suitable for exposing as a single ClearScript host object.</summary>
public sealed class WebSceneJavaScriptHostBridgeAdapter : IDisposable
{
    private readonly WebSceneHostBridge _bridge;
    private readonly IWebSceneJavaScriptRuntime _runtime;
    private readonly IWebSceneDispatcher _dispatcher;
    private readonly ConcurrentDictionary<string, CancellationTokenSource> _requests = new(StringComparer.Ordinal);
    private bool _disposed;

    public WebSceneJavaScriptHostBridgeAdapter(
        WebSceneHostBridge bridge,
        IWebSceneJavaScriptRuntime runtime,
        IWebSceneDispatcher dispatcher)
    {
        _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
    }

    public void invoke(string requestJson, object resolve, object reject)
    {
        if (_disposed)
        {
            return;
        }
        string requestId;
        try
        {
            using var document = JsonDocument.Parse(requestJson);
            requestId = document.RootElement.GetProperty("requestId").GetString()
                        ?? throw new InvalidDataException("requestId is required.");
        }
        catch (Exception exception)
        {
            _dispatcher.Post(() => _runtime.Invoke(reject, exception.Message));
            return;
        }

        var cancellation = new CancellationTokenSource();
        if (!_requests.TryAdd(requestId, cancellation))
        {
            cancellation.Dispose();
            _dispatcher.Post(() => _runtime.Invoke(reject, $"Duplicate host bridge request '{requestId}'."));
            return;
        }
        _ = InvokeCoreAsync(requestId, requestJson, resolve, reject, cancellation);
    }

    public void cancel(string requestId)
    {
        if (!string.IsNullOrWhiteSpace(requestId) && _requests.TryGetValue(requestId, out var cancellation))
        {
            cancellation.Cancel();
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        foreach (var cancellation in _requests.Values)
        {
            cancellation.Cancel();
        }
        GC.SuppressFinalize(this);
    }

    private async Task InvokeCoreAsync(
        string requestId,
        string requestJson,
        object resolve,
        object reject,
        CancellationTokenSource cancellation)
    {
        try
        {
            var response = await _bridge.InvokeJsonAsync(requestJson, cancellation.Token).ConfigureAwait(false);
            PostIfActive(resolve, response);
        }
        catch (Exception exception)
        {
            PostIfActive(reject, exception.Message);
        }
        finally
        {
            _requests.TryRemove(requestId, out _);
            cancellation.Dispose();
        }
    }

    private void PostIfActive(object callback, object? value)
    {
        if (!_disposed)
        {
            _dispatcher.Post(() =>
            {
                if (!_disposed)
                {
                    _runtime.Invoke(callback, value);
                }
            });
        }
    }
}

public static class WebSceneHostBridgeBootstrap
{
    public const string Script = """
(() => {
  const bridge = globalThis.__webSceneHostBridge;
  const client = capability => Object.freeze({
    invoke(method, argumentsValue = {}, options = {}) {
      const requestId = `${Date.now()}-${Math.random()}`;
      const request = JSON.stringify({
        requestId,
        version: '1.0', capability, method, arguments: argumentsValue
      });
      return new Promise((resolve, reject) => {
        if (options.signal?.aborted) return reject(options.signal.reason);
        options.signal?.addEventListener('abort', () => bridge.cancel(requestId), { once: true });
        bridge.invoke(request, value => {
          const response = JSON.parse(value);
          response.ok ? resolve(response.result) : reject(Object.assign(new Error(response.error.message), { code: response.error.code }));
        }, reject);
      });
    }
  });
  globalThis.webscene = Object.freeze({ profileVersion: '1.0', host: Object.freeze({
    commands: client('host.commands'), settings: client('host.settings'),
    notifications: client('host.notifications'), network: client('host.network'),
    clipboard: client('host.clipboard'), files: client('host.files')
  })});
})();
""";
}
