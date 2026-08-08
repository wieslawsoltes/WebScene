using System.Collections.Concurrent;
using WebScene.JavaScript.Interop;

namespace WebScene.Sdk.Avalonia;

internal sealed class NativeComponentHostBridge : IJavaScriptBinaryCallbackTarget
{
    public static IReadOnlyList<JavaScriptBinaryCallbackMethod> Methods { get; } =
    [
        new("invoke", 0, JavaScriptCallbackReturnKind.Promise),
        new("cancel", 1, JavaScriptCallbackReturnKind.Void)
    ];

    private readonly WebSceneHostBridge _bridge;
    private readonly ConcurrentDictionary<string, CancellationTokenSource> _requests =
        new(StringComparer.Ordinal);

    public NativeComponentHostBridge(WebSceneHostBridge bridge)
        => _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));

    public ValueTask DispatchBinaryAsync(
        uint methodId,
        JavaScriptBinaryValue arguments,
        JavaScriptBinaryCallbackCompletion completion,
        CancellationToken cancellationToken = default)
    {
        if (arguments.Kind != JavaScriptBinaryValueKind.Array
            || arguments.Count != 1
            || arguments.GetArrayItem(0).Kind != JavaScriptBinaryValueKind.String)
        {
            throw new InvalidDataException(
                "The component host bridge expects one string argument.");
        }
        var value = arguments.GetArrayItem(0).GetString();
        return methodId switch
        {
            0 => InvokeAsync(value, completion, cancellationToken),
            1 => CancelAsync(value),
            _ => ValueTask.FromException(
                new InvalidDataException($"Unknown component host bridge method {methodId}."))
        };
    }

    public void CancelAll()
    {
        foreach (var request in _requests.Values)
        {
            request.Cancel();
        }
    }

    private async ValueTask InvokeAsync(
        string requestJson,
        JavaScriptBinaryCallbackCompletion completion,
        CancellationToken cancellationToken)
    {
        var requestId = ReadRequestId(requestJson);
        using var requestCancellation =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        if (!_requests.TryAdd(requestId, requestCancellation))
        {
            throw new InvalidDataException(
                $"A host bridge request with ID '{requestId}' is already active.");
        }
        try
        {
            var result = await _bridge.InvokeJsonAsync(
                    requestJson,
                    requestCancellation.Token)
                .ConfigureAwait(false);
            completion.SetResult<string, StringResultCodec>(result);
        }
        finally
        {
            _requests.TryRemove(requestId, out _);
        }
    }

    private ValueTask CancelAsync(string requestId)
    {
        if (_requests.TryGetValue(requestId, out var cancellation))
        {
            cancellation.Cancel();
        }
        return ValueTask.CompletedTask;
    }

    private static string ReadRequestId(string requestJson)
    {
        using var document = System.Text.Json.JsonDocument.Parse(requestJson);
        if (!document.RootElement.TryGetProperty("requestId", out var property)
            || property.ValueKind != System.Text.Json.JsonValueKind.String
            || string.IsNullOrWhiteSpace(property.GetString()))
        {
            throw new InvalidDataException("Host bridge requestId is required.");
        }
        return property.GetString()!;
    }

    private readonly struct StringResultCodec
        : IJavaScriptBinaryCallbackResultCodec<string>
    {
        public static uint EncodeResult(
            ref JavaScriptBinaryWriter writer,
            in string result)
            => writer.WriteString(result);
    }
}
