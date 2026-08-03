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
    public async Task DiscoveryAndWebSocketForwardCompleteInspectorMessages()
    {
        var port = ReserveLoopbackPort();
        var session = new FakeInspectorSession();
        await using var host = new WebSceneV8InspectorHost(
            () => session,
            () => "file:///workspace/component.tsx",
            new WebSceneV8InspectorOptions
            {
                Enabled = true,
                Address = IPAddress.Loopback,
                Port = port
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
        Assert.Contains($"token={host.AccessToken}", websocketUrl);

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

    private static int ReserveLoopbackPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
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
}
