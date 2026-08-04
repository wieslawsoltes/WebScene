namespace WebScene.Backends;

/// <summary>
/// A raw, duplex V8 Inspector Protocol session. Messages are complete UTF-8
/// JSON payloads suitable for forwarding unchanged to a CDP WebSocket.
/// </summary>
public interface INativeV8InspectorSession : IAsyncDisposable
{
    ulong SessionId { get; }

    ValueTask SendAsync(
        ReadOnlyMemory<byte> message,
        CancellationToken cancellationToken = default);

    IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(
        CancellationToken cancellationToken = default);
}
