using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using WebScene.Backends;
using WebScene.Diagnostics.Cdp;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class WebSceneV8InspectorHostTests
{
    [Fact]
    public async Task MessageLimitCannotExceedNativeInspectorAbiLimit()
    {
        await using var host = new WebSceneV8InspectorHost(
            () => new FakeInspectorSession(),
            () => "webscene://oversized-limit",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Port = 0,
                MaxMessageBytes =
                    WebSceneV8InspectorOptions.NativeMaximumMessageBytes + 1
            });

        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(
            () => host.StartAsync());
    }

    [Fact]
    public async Task EphemeralPortPublishesActualDiscoveryEndpoint()
    {
        await using var host = new WebSceneV8InspectorHost(
            () => new FakeInspectorSession(),
            () => "webscene://ephemeral",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Port = 0
            });

        await host.StartAsync();

        Assert.InRange(host.BoundPort, 1, 65535);
        Assert.Equal(host.BoundPort, host.DiscoveryUri.Port);
        using var http = new HttpClient();
        using var discovery = JsonDocument.Parse(
            await http.GetStringAsync(new Uri(host.DiscoveryUri, "json/list")));
        Assert.Equal(
            host.BoundPort,
            new Uri(discovery.RootElement[0]
                .GetProperty("webSocketDebuggerUrl")
                .GetString()!).Port);
    }

    [Fact]
    public async Task WaitForDebuggerPreopensAndReusesWaitingSession()
    {
        var session = new FakeInspectorSession();
        var waits = new List<bool>();
        await using var host = new WebSceneV8InspectorHost(
            waitForDebugger =>
            {
                waits.Add(waitForDebugger);
                return session;
            },
            () => "webscene://waiting",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Port = 0,
                WaitForDebugger = true
            });

        await host.StartAsync();
        Assert.Equal([true], waits);

        using var http = new HttpClient();
        using var discovery = JsonDocument.Parse(
            await http.GetStringAsync(new Uri(host.DiscoveryUri, "json/list")));
        var websocketUrl = discovery.RootElement[0]
            .GetProperty("webSocketDebuggerUrl")
            .GetString();
        using var socket = new ClientWebSocket();
        await socket.ConnectAsync(new Uri(websocketUrl!), CancellationToken.None);
        await socket.SendAsync(
            Encoding.UTF8.GetBytes(
                "{\"id\":1,\"method\":\"Runtime.runIfWaitingForDebugger\"}"),
            WebSocketMessageType.Text,
            true,
            CancellationToken.None);
        var buffer = new byte[1024];
        await socket.ReceiveAsync(buffer, CancellationToken.None);

        Assert.Equal([true], waits);
        Assert.Equal(
            "{\"id\":1,\"method\":\"Runtime.runIfWaitingForDebugger\"}",
            await session.Received.Reader.ReadAsync());
        await socket.CloseAsync(
            WebSocketCloseStatus.NormalClosure,
            "test complete",
            CancellationToken.None);
    }

    [Fact]
    public async Task WaitForDebuggerSessionIsReservedBeforeListenerStarts()
    {
        var openEntered = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseOpen = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await using var host = new WebSceneV8InspectorHost(
            waitForDebugger =>
            {
                Assert.True(waitForDebugger);
                openEntered.TrySetResult(true);
                releaseOpen.Task.GetAwaiter().GetResult();
                return new FakeInspectorSession();
            },
            () => "webscene://waiting-startup",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Port = 0,
                WaitForDebugger = true
            });

        var start = Task.Run(() => host.StartAsync());
        await openEntered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        try
        {
            Assert.False(host.IsRunning);
        }
        finally
        {
            releaseOpen.TrySetResult(true);
        }

        await start.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(host.IsRunning);
    }

    [Fact]
    public async Task CustomTargetIdIsEncodedInDebuggerPath()
    {
        const string targetId = "custom?target#1";
        await using var host = new WebSceneV8InspectorHost(
            () => new FakeInspectorSession(),
            () => "webscene://custom-target",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Port = 0
            },
            targetId);

        await host.StartAsync();

        using var http = new HttpClient();
        using var discovery = JsonDocument.Parse(
            await http.GetStringAsync(new Uri(host.DiscoveryUri, "json/list")));
        var target = discovery.RootElement[0];
        var websocketUrl = target.GetProperty("webSocketDebuggerUrl").GetString();
        Assert.Equal(targetId, target.GetProperty("id").GetString());
        Assert.Contains("/devtools/page/custom%3Ftarget%231", websocketUrl);

        using var socket = new ClientWebSocket();
        await socket.ConnectAsync(new Uri(websocketUrl!), CancellationToken.None);
        Assert.Equal(WebSocketState.Open, socket.State);
        await socket.CloseAsync(
            WebSocketCloseStatus.NormalClosure,
            "test complete",
            CancellationToken.None);
    }

    [Fact]
    public async Task DiscoveryAndWebSocketForwardCompleteInspectorMessages()
    {
        const string accessToken =
            "base64+/token&with=reserved?chars-0123456789";
        var port = ReserveLoopbackPort();
        var session = new FakeInspectorSession();
        await using var host = new WebSceneV8InspectorHost(
            () => session,
            () => "file:///workspace/component.tsx",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Address = IPAddress.Loopback,
                Port = port,
                AccessToken = accessToken
            });
        await host.StartAsync();

        using var http = new HttpClient();
        using var discovery = JsonDocument.Parse(
            await http.GetStringAsync(new Uri(host.DiscoveryUri, "json/list")));
        var target = discovery.RootElement[0];
        Assert.Equal("webscene-v8", target.GetProperty("id").GetString());
        Assert.Equal(
            "file:///workspace/component.tsx",
            target.GetProperty("url").GetString());
        var websocketUrl = target.GetProperty("webSocketDebuggerUrl").GetString();
        Assert.Equal(accessToken, host.AccessToken);
        Assert.Contains(
            $"token={Uri.EscapeDataString(accessToken)}",
            websocketUrl);

        using var socket = new ClientWebSocket();
        await socket.ConnectAsync(new Uri(websocketUrl!), CancellationToken.None);
        const string request = "{\"id\":1,\"method\":\"Runtime.enable\"}";
        await socket.SendAsync(
            Encoding.UTF8.GetBytes(request),
            WebSocketMessageType.Text,
            true,
            CancellationToken.None);
        var buffer = new byte[1024];
        var response = await socket.ReceiveAsync(buffer, CancellationToken.None);

        Assert.True(response.EndOfMessage);
        Assert.Equal(WebSocketMessageType.Text, response.MessageType);
        Assert.Equal(
            "{\"id\":1,\"result\":{}}",
            Encoding.UTF8.GetString(buffer, 0, response.Count));
        Assert.Equal(request, await session.Received.Reader.ReadAsync());
        await socket.CloseAsync(
            WebSocketCloseStatus.NormalClosure,
            "test complete",
            CancellationToken.None);
    }

    [Fact]
    public async Task DomCssAndOverlayCommandsUseNativeSnapshotWithoutRewritingV8Traffic()
    {
        var v8 = new FakeInspectorSession();
        var dom = new FakeDomInspector();
        await using var host = new WebSceneV8InspectorHost(
            () => v8,
            () => "webscene://elements",
            new WebSceneV8InspectorOptions { Enabled = true, Port = 0 },
            domInspector: dom);
        await host.StartAsync();

        using var socket = await ConnectAsync(host);
        using var document = await SendAndReadAsync(
            socket,
            "{\"id\":21,\"method\":\"DOM.getDocument\"}");
        var root = document.RootElement.GetProperty("result").GetProperty("root");
        Assert.Equal(1, root.GetProperty("nodeId").GetInt32());
        var body = root.GetProperty("children")[0];
        Assert.Equal(2, body.GetProperty("nodeId").GetInt32());
        var div = body.GetProperty("children")[0];
        Assert.Equal("DIV", div.GetProperty("nodeName").GetString());
        Assert.Equal("probe", div.GetProperty("attributes")[1].GetString());

        using var computed = await SendAndReadAsync(
            socket,
            "{\"id\":22,\"method\":\"CSS.getComputedStyleForNode\",\"params\":{\"nodeId\":3}}");
        Assert.Contains(
            computed.RootElement.GetProperty("result").GetProperty("computedStyle")
                .EnumerateArray(),
            property => property.GetProperty("name").GetString() == "display"
                && property.GetProperty("value").GetString() == "block");

        using var box = await SendAndReadAsync(
            socket,
            "{\"id\":23,\"method\":\"DOM.getBoxModel\",\"params\":{\"nodeId\":3}}");
        Assert.Equal(120, box.RootElement.GetProperty("result")
            .GetProperty("model").GetProperty("width").GetSingle());

        using var highlight = await SendAndReadAsync(
            socket,
            "{\"id\":24,\"method\":\"Overlay.highlightNode\",\"params\":{\"nodeId\":4}}");
        Assert.Equal(2U, dom.HighlightedNodeId);

        using var accessibility = await SendAndReadAsync(
            socket,
            "{\"id\":26,\"method\":\"Accessibility.getAXNode\",\"params\":{\"nodeId\":3}}");
        var axNode = accessibility.RootElement.GetProperty("result")
            .GetProperty("nodes")[0];
        Assert.Equal("Hello", axNode.GetProperty("name").GetProperty("value").GetString());

        using var accessibilityTree = await SendAndReadAsync(
            socket,
            "{\"id\":27,\"method\":\"Accessibility.getFullAXTree\"}");
        Assert.Equal(2, accessibilityTree.RootElement.GetProperty("result")
            .GetProperty("nodes").GetArrayLength());

        const string runtimeRequest =
            "{\"id\":25,\"method\":\"Runtime.enable\",\"params\":{\"preserve\":true}}";
        using var runtime = await SendAndReadAsync(socket, runtimeRequest);
        Assert.Equal(runtimeRequest, await v8.Received.Reader.ReadAsync());
    }

    [Fact]
    public async Task PickerSelectionPublishesOverlayInspectionNotification()
    {
        var dom = new FakeDomInspector();
        await using var host = new WebSceneV8InspectorHost(
            () => new FakeInspectorSession(),
            () => "webscene://picker",
            new WebSceneV8InspectorOptions { Enabled = true, Port = 0 },
            domInspector: dom);
        await host.StartAsync();

        using var socket = await ConnectAsync(host);
        using var enable = await SendAndReadAsync(
            socket,
            "{\"id\":31,\"method\":\"DOM.enable\"}");
        using var inspect = await SendAndReadAsync(
            socket,
            "{\"id\":32,\"method\":\"Overlay.setInspectMode\",\"params\":{\"mode\":\"searchForNode\"}}");
        Assert.True(dom.InspectMode);

        dom.Select(2);
        using var notification = await ReadJsonAsync(socket);
        Assert.Equal("Overlay.inspectNodeRequested",
            notification.RootElement.GetProperty("method").GetString());
        Assert.Equal(3, notification.RootElement.GetProperty("params")
            .GetProperty("backendNodeId").GetInt32());
        Assert.False(dom.InspectMode);
    }

    [Fact]
    public async Task DomMutationPublishesDocumentUpdatedNotification()
    {
        var dom = new FakeDomInspector();
        await using var host = new WebSceneV8InspectorHost(
            () => new FakeInspectorSession(),
            () => "webscene://mutation",
            new WebSceneV8InspectorOptions { Enabled = true, Port = 0 },
            domInspector: dom);
        await host.StartAsync();

        using var socket = await ConnectAsync(host);
        using var enable = await SendAndReadAsync(
            socket,
            "{\"id\":41,\"method\":\"DOM.enable\"}");

        dom.MutateText("Updated");
        using var notification = await ReadJsonAsync(socket);
        Assert.Equal("DOM.documentUpdated",
            notification.RootElement.GetProperty("method").GetString());
    }

    [Fact]
    public async Task StopIgnoresConnectionFaultAlreadyObservedByHost()
    {
        var session = new FaultingInspectorSession();
        await using var host = new WebSceneV8InspectorHost(
            () => session,
            () => "webscene://faulting-connection",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Port = 0
            });
        await host.StartAsync();

        using var http = new HttpClient();
        using var discovery = JsonDocument.Parse(
            await http.GetStringAsync(new Uri(host.DiscoveryUri, "json/list")));
        var websocketUrl = discovery.RootElement[0]
            .GetProperty("webSocketDebuggerUrl")
            .GetString();
        using var socket = new ClientWebSocket();
        await socket.ConnectAsync(new Uri(websocketUrl!), CancellationToken.None);
        await socket.SendAsync(
            Encoding.UTF8.GetBytes("{\"id\":1,\"method\":\"Runtime.enable\"}"),
            WebSocketMessageType.Text,
            true,
            CancellationToken.None);
        await session.SendEntered.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var stop = host.StopAsync();
        Assert.False(stop.IsCompleted);
        session.FailSend(new InvalidOperationException("malformed client message"));

        await stop.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task RemoteDiscoveryRequiresTokenBeforePublishingWebSocketUrl()
    {
        var address = GetNonLoopbackAddress();
        await using var host = new WebSceneV8InspectorHost(
            () => new FakeInspectorSession(),
            () => "webscene://remote",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Address = address,
                Port = 0,
                AllowRemoteConnections = true
            });
        await host.StartAsync();

        using var http = new HttpClient(new HttpClientHandler { UseProxy = false });
        using var unauthorized = await http.GetAsync(
            new Uri(host.DiscoveryUri, "json/list"));
        var unauthorizedBody = await unauthorized.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.Unauthorized, unauthorized.StatusCode);
        Assert.DoesNotContain(host.AccessToken, unauthorizedBody);

        using var authorized = await http.GetAsync(
            new Uri(host.DiscoveryUri, $"json/list?token={host.AccessToken}"));
        var authorizedBody = await authorized.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, authorized.StatusCode);
        Assert.Contains($"token={host.AccessToken}", authorizedBody);
    }

    private static int ReserveLoopbackPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    private static IPAddress GetNonLoopbackAddress()
        => Dns.GetHostAddresses(Dns.GetHostName()).FirstOrDefault(
            address => address.AddressFamily == AddressFamily.InterNetwork
                && !IPAddress.IsLoopback(address))
            ?? throw new InvalidOperationException(
                "The remote-discovery regression requires a non-loopback IPv4 address.");

    private static async Task<ClientWebSocket> ConnectAsync(
        WebSceneV8InspectorHost host)
    {
        using var http = new HttpClient();
        using var discovery = JsonDocument.Parse(
            await http.GetStringAsync(new Uri(host.DiscoveryUri, "json/list")));
        var url = discovery.RootElement[0]
            .GetProperty("webSocketDebuggerUrl").GetString()!;
        var socket = new ClientWebSocket();
        await socket.ConnectAsync(new Uri(url), CancellationToken.None);
        return socket;
    }

    private static async Task<JsonDocument> SendAndReadAsync(
        ClientWebSocket socket,
        string request)
    {
        await socket.SendAsync(
            Encoding.UTF8.GetBytes(request),
            WebSocketMessageType.Text,
            true,
            CancellationToken.None);
        return await ReadJsonAsync(socket);
    }

    private static async Task<JsonDocument> ReadJsonAsync(ClientWebSocket socket)
    {
        var buffer = new byte[32 * 1024];
        var result = await socket.ReceiveAsync(buffer, CancellationToken.None);
        Assert.True(result.EndOfMessage);
        return JsonDocument.Parse(buffer.AsMemory(0, result.Count));
    }

    private sealed class FakeInspectorSession : INativeV8InspectorSession
    {
        private readonly Channel<ReadOnlyMemory<byte>> _outgoing =
            Channel.CreateUnbounded<ReadOnlyMemory<byte>>();

        public Channel<string> Received { get; } = Channel.CreateUnbounded<string>();

        public ulong SessionId => 1;

        public ValueTask SendAsync(
            ReadOnlyMemory<byte> message,
            CancellationToken cancellationToken = default)
        {
            var request = Encoding.UTF8.GetString(message.Span);
            Received.Writer.TryWrite(request);
            _outgoing.Writer.TryWrite(
                Encoding.UTF8.GetBytes("{\"id\":1,\"result\":{}}"));
            return ValueTask.CompletedTask;
        }

        public IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(
            CancellationToken cancellationToken = default)
            => _outgoing.Reader.ReadAllAsync(cancellationToken);

        public ValueTask DisposeAsync()
        {
            _outgoing.Writer.TryComplete();
            Received.Writer.TryComplete();
            return ValueTask.CompletedTask;
        }
    }

    private sealed class FaultingInspectorSession : INativeV8InspectorSession
    {
        private readonly Channel<ReadOnlyMemory<byte>> _outgoing =
            Channel.CreateUnbounded<ReadOnlyMemory<byte>>();
        private readonly TaskCompletionSource<bool> _sendCompletion = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource<bool> SendEntered { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public ulong SessionId => 2;

        public ValueTask SendAsync(
            ReadOnlyMemory<byte> message,
            CancellationToken cancellationToken = default)
        {
            SendEntered.TrySetResult(true);
            return new ValueTask(_sendCompletion.Task);
        }

        public IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(
            CancellationToken cancellationToken = default)
            => _outgoing.Reader.ReadAllAsync(cancellationToken);

        public void FailSend(Exception error)
            => _sendCompletion.TrySetException(error);

        public ValueTask DisposeAsync()
        {
            _outgoing.Writer.TryComplete();
            return ValueTask.CompletedTask;
        }
    }

    private sealed class FakeDomInspector : INativeDomInspector
    {
        private ulong _documentRevision = 7;
        private ulong _selectionSequence;
        private uint _selectedNodeId;
        private string _text = "Hello";

        public bool InspectMode { get; private set; }

        public uint HighlightedNodeId { get; private set; }

        public ValueTask<NativeDomSnapshot> GetDomSnapshotAsync(
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new NativeDomSnapshot(
                _documentRevision,
                1,
                [
                    new NativeDomNodeSnapshot(
                        1, 0, 1, "body", "", "http://www.w3.org/1999/xhtml",
                        [],
                        [new("display", "block")],
                        new(0, 0, 800, 600, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                        1, true),
                    new NativeDomNodeSnapshot(
                        2, 1, 1, "div", "", "http://www.w3.org/1999/xhtml",
                        [new("id", "probe"), new("class", "card")],
                        [new("display", "block"), new("color", "rgb(1, 2, 3)")],
                        new(10, 20, 120, 40, 4, 5, 6, 7, 1, 2, 3, 4, 8, 9, 10, 11),
                        1, true),
                    new NativeDomNodeSnapshot(
                        3, 2, 3, "#text", _text, "", [], [],
                        new(19, 31, 80, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                        0, true)
                ],
                HighlightedNodeId,
                _selectedNodeId,
                _selectionSequence));
        }

        public ValueTask SetDomInspectModeAsync(
            bool enabled,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            InspectMode = enabled;
            return ValueTask.CompletedTask;
        }

        public ValueTask SetDomHighlightAsync(
            uint nativeNodeId,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            HighlightedNodeId = nativeNodeId;
            return ValueTask.CompletedTask;
        }

        public void Select(uint nativeNodeId)
        {
            _selectedNodeId = nativeNodeId;
            _selectionSequence++;
        }

        public void MutateText(string text)
        {
            _text = text;
            _documentRevision++;
        }
    }
}
