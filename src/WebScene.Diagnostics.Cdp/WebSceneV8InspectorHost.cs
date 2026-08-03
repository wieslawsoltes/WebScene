using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using WebScene.Backends;

namespace WebScene.Diagnostics.Cdp;

public sealed class WebSceneV8InspectorOptions
{
    public bool Enabled { get; set; }

    public IPAddress Address { get; set; } = IPAddress.Loopback;

    public int Port { get; set; } = 9229;

    /// <summary>
    /// Opens the first Inspector session in V8's waiting-for-debugger mode.
    /// The engine worker remains paused until a client sends
    /// Runtime.runIfWaitingForDebugger, while the host UI thread stays free.
    /// </summary>
    public bool WaitForDebugger { get; set; }

    public bool AllowRemoteConnections { get; set; }

    public string? AccessToken { get; set; }

    public int MaxMessageBytes { get; set; } = 16 * 1024 * 1024;

    public TimeSpan KeepAliveInterval { get; set; } = TimeSpan.FromSeconds(30);
}

/// <summary>
/// Chrome-compatible discovery and WebSocket host for one WebScene V8 target.
/// The host never interprets Inspector messages; it forwards complete UTF-8
/// JSON payloads between Chrome and the native isolate.
/// </summary>
public sealed class WebSceneV8InspectorHost : IAsyncDisposable
{
    private readonly Func<bool, INativeV8InspectorSession> _openSession;
    private readonly Func<string?> _targetUrl;
    private readonly WebSceneV8InspectorOptions _options;
    private readonly string _targetId;
    private readonly string _title;
    private readonly object _gate = new();
    private readonly Dictionary<long, Task> _connections = [];
    private HttpListener? _listener;
    private CancellationTokenSource? _shutdown;
    private Task? _listenLoop;
    private INativeV8InspectorSession? _waitingSession;
    private long _nextConnectionId;

    public WebSceneV8InspectorHost(
        Func<INativeV8InspectorSession> openSession,
        Func<string?> targetUrl,
        WebSceneV8InspectorOptions options,
        string targetId = "webscene-v8",
        string title = "WebScene V8")
    {
        ArgumentNullException.ThrowIfNull(openSession);
        _openSession = _ => openSession();
        _targetUrl = targetUrl
            ?? throw new ArgumentNullException(nameof(targetUrl));
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _targetId = string.IsNullOrWhiteSpace(targetId)
            ? throw new ArgumentException("A target id is required.", nameof(targetId))
            : targetId;
        _title = string.IsNullOrWhiteSpace(title) ? "WebScene V8" : title;
    }

    public WebSceneV8InspectorHost(
        Func<bool, INativeV8InspectorSession> openSession,
        Func<string?> targetUrl,
        WebSceneV8InspectorOptions options,
        string targetId = "webscene-v8",
        string title = "WebScene V8")
    {
        _openSession = openSession
            ?? throw new ArgumentNullException(nameof(openSession));
        _targetUrl = targetUrl
            ?? throw new ArgumentNullException(nameof(targetUrl));
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _targetId = string.IsNullOrWhiteSpace(targetId)
            ? throw new ArgumentException("A target id is required.", nameof(targetId))
            : targetId;
        _title = string.IsNullOrWhiteSpace(title) ? "WebScene V8" : title;
    }

    public bool IsRunning => _listener?.IsListening == true;

    /// <summary>
    /// The actual listening port. This differs from options.Port when zero was
    /// requested for an ephemeral loopback endpoint.
    /// </summary>
    public int BoundPort { get; private set; }

    public Uri DiscoveryUri
        => new($"http://{FormatAddress(_options.Address)}:{BoundPort}/");

    public string AccessToken { get; private set; } = string.Empty;

    public Task StartAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ValidateOptions();
        lock (_gate)
        {
            if (_listener is not null)
            {
                throw new InvalidOperationException(
                    "The WebScene V8 Inspector host is already running.");
            }
            AccessToken = string.IsNullOrWhiteSpace(_options.AccessToken)
                ? Convert.ToHexString(RandomNumberGenerator.GetBytes(32))
                    .ToLowerInvariant()
                : _options.AccessToken;
            BoundPort = ResolvePort(_options.Address, _options.Port);
            var listener = new HttpListener();
            listener.Prefixes.Add(
                $"http://{FormatAddress(_options.Address)}:{BoundPort}/");
            var shutdown = new CancellationTokenSource();
            try
            {
                listener.Start();
                _listener = listener;
                _shutdown = shutdown;
                _listenLoop = ListenAsync(listener, shutdown.Token);
                if (_options.WaitForDebugger)
                {
                    _waitingSession = _openSession(true);
                }
            }
            catch
            {
                _listener = null;
                _shutdown = null;
                _listenLoop = null;
                listener.Close();
                shutdown.Dispose();
                throw;
            }
        }
        return Task.CompletedTask;
    }

    public async Task StopAsync(CancellationToken cancellationToken = default)
    {
        HttpListener? listener;
        CancellationTokenSource? shutdown;
        Task? listenLoop;
        INativeV8InspectorSession? waitingSession;
        lock (_gate)
        {
            listener = _listener;
            shutdown = _shutdown;
            listenLoop = _listenLoop;
            _listener = null;
            _shutdown = null;
            _listenLoop = null;
            waitingSession = Interlocked.Exchange(ref _waitingSession, null);
        }
        if (listener is null) return;
        shutdown?.Cancel();
        listener.Close();
        if (waitingSession is not null)
        {
            await waitingSession.DisposeAsync().ConfigureAwait(false);
        }
        if (listenLoop is not null)
        {
            try
            {
                await listenLoop.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
            catch (HttpListenerException)
            {
            }
        }
        Task[] connections;
        lock (_gate) connections = _connections.Values.ToArray();
        if (connections.Length != 0)
        {
            try
            {
                await Task.WhenAll(connections)
                    .WaitAsync(cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
            }
        }
        shutdown?.Dispose();
    }

    private async Task ListenAsync(
        HttpListener listener,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested && listener.IsListening)
        {
            HttpListenerContext context;
            try
            {
                context = await listener.GetContextAsync()
                    .WaitAsync(cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (HttpListenerException) when (!listener.IsListening)
            {
                break;
            }
            var connectionId = Interlocked.Increment(ref _nextConnectionId);
            var task = HandleAsync(context, cancellationToken);
            lock (_gate) _connections[connectionId] = task;
            _ = ObserveConnectionAsync(connectionId, task);
        }
    }

    private async Task ObserveConnectionAsync(long connectionId, Task task)
    {
        try
        {
            await task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene V8 Inspector host] {error}");
        }
        finally
        {
            lock (_gate) _connections.Remove(connectionId);
        }
    }

    private async Task HandleAsync(
        HttpListenerContext context,
        CancellationToken cancellationToken)
    {
        var path = context.Request.Url?.AbsolutePath.TrimEnd('/') ?? string.Empty;
        if (context.Request.IsWebSocketRequest)
        {
            if (path != $"/devtools/page/{_targetId}"
                || !IsAuthorized(context.Request)
                || !IsAllowedOrigin(context.Request.Headers["Origin"]))
            {
                context.Response.StatusCode = (int)HttpStatusCode.Unauthorized;
                context.Response.Close();
                return;
            }
            await HandleWebSocketAsync(context, cancellationToken).ConfigureAwait(false);
            return;
        }
        if (!string.Equals(context.Request.HttpMethod, "GET", StringComparison.OrdinalIgnoreCase))
        {
            context.Response.StatusCode = (int)HttpStatusCode.MethodNotAllowed;
            context.Response.Close();
            return;
        }
        if (path == "/json/version")
        {
            await WriteJsonAsync(
                context.Response,
                new
                {
                    Browser = "WebScene/V8",
                    ProtocolVersion = "1.3",
                    UserAgent = "WebScene V8 Inspector"
                },
                cancellationToken).ConfigureAwait(false);
            return;
        }
        if (path is "/json" or "/json/list")
        {
            var authority = context.Request.Url?.Authority
                ?? $"{FormatAddress(_options.Address)}:{_options.Port}";
            var websocketUrl = $"ws://{authority}/devtools/page/{_targetId}?token={AccessToken}";
            await WriteJsonAsync(
                context.Response,
                new[]
                {
                    new
                    {
                        id = _targetId,
                        title = _title,
                        type = "page",
                        url = _targetUrl() ?? "webscene://runtime",
                        description = "WebScene native V8 runtime",
                        webSocketDebuggerUrl = websocketUrl,
                        devtoolsFrontendUrl =
                            $"devtools://devtools/bundled/inspector.html?ws={Uri.EscapeDataString(websocketUrl[5..])}"
                    }
                },
                cancellationToken).ConfigureAwait(false);
            return;
        }
        context.Response.StatusCode = (int)HttpStatusCode.NotFound;
        context.Response.Close();
    }

    private async Task HandleWebSocketAsync(
        HttpListenerContext context,
        CancellationToken cancellationToken)
    {
        var accepted = await context.AcceptWebSocketAsync(
            null,
            _options.KeepAliveInterval)
            .ConfigureAwait(false);
        using var socket = accepted.WebSocket;
        await using var inspector = Interlocked.Exchange(ref _waitingSession, null)
            ?? _openSession(false);
        using var connectionShutdown = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        var receive = ReceiveBrowserMessagesAsync(
            socket,
            inspector,
            connectionShutdown.Token);
        var send = SendInspectorMessagesAsync(
            socket,
            inspector,
            connectionShutdown.Token);
        await Task.WhenAny(receive, send).ConfigureAwait(false);
        connectionShutdown.Cancel();
        try
        {
            await Task.WhenAll(receive, send).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (WebSocketException)
        {
            // Browser tabs and DevTools commonly disappear without a close
            // handshake. The native inspector session is still disposed below.
        }
        if (socket.State is WebSocketState.Open or WebSocketState.CloseReceived)
        {
            try
            {
                await socket.CloseAsync(
                    WebSocketCloseStatus.NormalClosure,
                    "Inspector session closed",
                    CancellationToken.None).ConfigureAwait(false);
            }
            catch (WebSocketException)
            {
            }
        }
    }

    private async Task ReceiveBrowserMessagesAsync(
        WebSocket socket,
        INativeV8InspectorSession inspector,
        CancellationToken cancellationToken)
    {
        var buffer = new byte[16 * 1024];
        using var message = new MemoryStream();
        while (socket.State == WebSocketState.Open)
        {
            var received = await socket.ReceiveAsync(buffer, cancellationToken)
                .ConfigureAwait(false);
            if (received.MessageType == WebSocketMessageType.Close) return;
            if (received.MessageType != WebSocketMessageType.Text)
            {
                throw new InvalidOperationException(
                    "V8 Inspector accepts text WebSocket messages only.");
            }
            message.Write(buffer, 0, received.Count);
            if (message.Length > _options.MaxMessageBytes)
            {
                throw new InvalidOperationException(
                    "The V8 Inspector request exceeded the configured limit.");
            }
            if (!received.EndOfMessage) continue;
            await inspector.SendAsync(message.ToArray(), cancellationToken)
                .ConfigureAwait(false);
            message.SetLength(0);
        }
    }

    private static async Task SendInspectorMessagesAsync(
        WebSocket socket,
        INativeV8InspectorSession inspector,
        CancellationToken cancellationToken)
    {
        await foreach (var message in inspector.ReadAllAsync(cancellationToken)
                           .ConfigureAwait(false))
        {
            await socket.SendAsync(
                message,
                WebSocketMessageType.Text,
                true,
                cancellationToken).ConfigureAwait(false);
        }
    }

    private bool IsAuthorized(HttpListenerRequest request)
        => string.Equals(
            request.QueryString["token"],
            AccessToken,
            StringComparison.Ordinal)
        || string.Equals(
            request.Headers["Authorization"],
            $"Bearer {AccessToken}",
            StringComparison.Ordinal);

    private static bool IsAllowedOrigin(string? origin)
        => string.IsNullOrWhiteSpace(origin)
        || origin.StartsWith("devtools://", StringComparison.OrdinalIgnoreCase)
        || origin.StartsWith("chrome-devtools://", StringComparison.OrdinalIgnoreCase);

    private static async Task WriteJsonAsync(
        HttpListenerResponse response,
        object value,
        CancellationToken cancellationToken)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(value);
        response.ContentType = "application/json; charset=UTF-8";
        response.ContentLength64 = bytes.Length;
        response.StatusCode = (int)HttpStatusCode.OK;
        await response.OutputStream.WriteAsync(bytes, cancellationToken)
            .ConfigureAwait(false);
        response.Close();
    }

    private void ValidateOptions()
    {
        if (!_options.Enabled)
        {
            throw new InvalidOperationException(
                "Set WebSceneV8InspectorOptions.Enabled to true explicitly.");
        }
        if (_options.Port is < 0 or > 65535)
        {
            throw new ArgumentOutOfRangeException(nameof(_options.Port));
        }
        if (_options.MaxMessageBytes < 1024)
        {
            throw new ArgumentOutOfRangeException(nameof(_options.MaxMessageBytes));
        }
        if (!IPAddress.IsLoopback(_options.Address)
            && !_options.AllowRemoteConnections)
        {
            throw new InvalidOperationException(
                "Non-loopback Inspector bindings require AllowRemoteConnections=true.");
        }
        if (!IPAddress.IsLoopback(_options.Address)
            && !string.IsNullOrEmpty(_options.AccessToken)
            && _options.AccessToken.Length < 32)
        {
            throw new InvalidOperationException(
                "Remote Inspector access tokens must contain at least 32 characters.");
        }
    }

    private static string FormatAddress(IPAddress address)
        => address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetworkV6
            ? $"[{address}]"
            : address.ToString();

    private static int ResolvePort(IPAddress address, int requestedPort)
    {
        if (requestedPort != 0) return requestedPort;
        var reservation = new TcpListener(address, 0);
        reservation.Start();
        try
        {
            return ((IPEndPoint)reservation.LocalEndpoint).Port;
        }
        finally
        {
            reservation.Stop();
        }
    }

    public ValueTask DisposeAsync() => new(StopAsync());
}
