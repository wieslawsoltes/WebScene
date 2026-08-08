using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading.Channels;
using WebScene.Backends;

namespace WebScene.Diagnostics.Cdp;

/// <summary>
/// Adds WebScene's renderer-owned DOM/CSS/Overlay domains to a raw V8
/// Inspector session. Messages for V8 domains are forwarded without rewriting.
/// </summary>
internal sealed class WebSceneCdpSession : INativeV8InspectorSession
{
    private readonly INativeV8InspectorSession _v8;
    private readonly INativeDomInspector? _dom;
    private readonly Channel<ReadOnlyMemory<byte>> _outgoing =
        Channel.CreateBounded<ReadOnlyMemory<byte>>(new BoundedChannelOptions(256)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.Wait
        });
    private volatile bool _domEnabled;
    private ulong _lastDocumentEpoch;
    private ulong _lastTreeSignature;
    private ulong _lastSelectionSequence;
    private bool _disposed;

    public WebSceneCdpSession(
        INativeV8InspectorSession v8,
        INativeDomInspector? dom)
    {
        _v8 = v8;
        _dom = dom;
    }

    public ulong SessionId => _v8.SessionId;

    public async ValueTask SendAsync(
        ReadOnlyMemory<byte> message,
        CancellationToken cancellationToken)
    {
        if (_dom is null)
        {
            await _v8.SendAsync(message, cancellationToken).ConfigureAwait(false);
            return;
        }

        JsonDocument request;
        try
        {
            request = JsonDocument.Parse(message);
        }
        catch (JsonException)
        {
            await _v8.SendAsync(message, cancellationToken).ConfigureAwait(false);
            return;
        }
        using (request)
        {
            var root = request.RootElement;
            if (!root.TryGetProperty("method", out var methodElement)
                || methodElement.ValueKind != JsonValueKind.String
                || !root.TryGetProperty("id", out var id))
            {
                await _v8.SendAsync(message, cancellationToken).ConfigureAwait(false);
                return;
            }
            var method = methodElement.GetString()!;
            var parameters = root.TryGetProperty("params", out var paramsElement)
                ? paramsElement
                : default;
            if (await TryHandleAsync(
                    id,
                    method,
                    parameters,
                    cancellationToken).ConfigureAwait(false))
            {
                return;
            }
        }
        await _v8.SendAsync(message, cancellationToken).ConfigureAwait(false);
    }

    public async IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        var v8Pump = PumpV8Async(linked.Token);
        var domPump = PumpDomAsync(linked.Token);
        try
        {
            await foreach (var message in _outgoing.Reader
                               .ReadAllAsync(cancellationToken)
                               .ConfigureAwait(false))
            {
                yield return message;
            }
        }
        finally
        {
            linked.Cancel();
            try
            {
                await Task.WhenAll(v8Pump, domPump).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }
    }

    private async Task PumpV8Async(CancellationToken cancellationToken)
    {
        try
        {
            await foreach (var message in _v8.ReadAllAsync(cancellationToken)
                               .ConfigureAwait(false))
            {
                await _outgoing.Writer.WriteAsync(
                    message.ToArray(), cancellationToken).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception error)
        {
            _outgoing.Writer.TryComplete(error);
        }
    }

    private async Task PumpDomAsync(CancellationToken cancellationToken)
    {
        if (_dom is null) return;
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                if (_domEnabled)
                {
                    var snapshot = await _dom.GetDomSnapshotAsync(cancellationToken)
                        .ConfigureAwait(false);
                    var treeSignature = ComputeTreeSignature(snapshot);
                    if (_lastDocumentEpoch != 0
                        && (snapshot.DocumentEpoch != _lastDocumentEpoch
                            || treeSignature != _lastTreeSignature))
                    {
                        await PublishEventAsync(
                            "DOM.documentUpdated", null, cancellationToken)
                            .ConfigureAwait(false);
                    }
                    _lastDocumentEpoch = snapshot.DocumentEpoch;
                    _lastTreeSignature = treeSignature;
                    if (snapshot.SelectionSequence > _lastSelectionSequence)
                    {
                        _lastSelectionSequence = snapshot.SelectionSequence;
                        await _dom.SetDomInspectModeAsync(false, cancellationToken)
                            .ConfigureAwait(false);
                        await PublishEventAsync(
                            "Overlay.inspectNodeRequested",
                            new { backendNodeId = ToCdpNodeId(snapshot.SelectedNodeId) },
                            cancellationToken).ConfigureAwait(false);
                    }
                }
                await Task.Delay(50, cancellationToken).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception error)
        {
            _outgoing.Writer.TryComplete(error);
        }
    }

    private async ValueTask<bool> TryHandleAsync(
        JsonElement id,
        string method,
        JsonElement parameters,
        CancellationToken cancellationToken)
    {
        switch (method)
        {
            case "DOM.enable":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                _lastDocumentEpoch = snapshot.DocumentEpoch;
                _lastTreeSignature = ComputeTreeSignature(snapshot);
                _lastSelectionSequence = snapshot.SelectionSequence;
                _domEnabled = true;
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "DOM.disable":
                _domEnabled = false;
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            case "CSS.enable":
            case "CSS.disable":
            case "Overlay.enable":
            case "Overlay.disable":
            case "DOMDebugger.enable":
            case "DOMDebugger.disable":
            case "Accessibility.enable":
            case "Accessibility.disable":
            case "DOM.setInspectedNode":
            case "DOM.requestChildNodes":
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            case "DOM.getDocument":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var depth = ReadInt(parameters, "depth", -1);
                await RespondAsync(
                    id,
                    new { root = CreateDocument(snapshot, depth) },
                    cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "DOM.describeNode":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                if (node is null)
                {
                    await RespondNodeNotFoundAsync(id, cancellationToken).ConfigureAwait(false);
                }
                else
                {
                    await RespondAsync(id, new { node = CreateNode(snapshot, node, 0) },
                        cancellationToken).ConfigureAwait(false);
                }
                return true;
            }
            case "DOM.getAttributes":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                await RespondAsync(id, new
                {
                    attributes = node?.Attributes
                        .SelectMany(static item => new[] { item.Name, item.Value })
                        .ToArray() ?? []
                }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "DOM.getBoxModel":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                if (node is null)
                {
                    await RespondNodeNotFoundAsync(id, cancellationToken).ConfigureAwait(false);
                }
                else
                {
                    await RespondAsync(id, new { model = CreateBoxModel(node.Box) },
                        cancellationToken).ConfigureAwait(false);
                }
                return true;
            }
            case "DOM.getNodeForLocation":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var x = ReadDouble(parameters, "x");
                var y = ReadDouble(parameters, "y");
                var node = snapshot.Nodes.LastOrDefault(candidate =>
                    candidate.NodeType == 1 && candidate.IsVisible
                    && x >= candidate.Box.X && y >= candidate.Box.Y
                    && x <= candidate.Box.X + candidate.Box.Width
                    && y <= candidate.Box.Y + candidate.Box.Height);
                var nodeId = ToCdpNodeId(node?.NodeId ?? 0);
                await RespondAsync(id, new { nodeId, backendNodeId = nodeId },
                    cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "DOM.pushNodesByBackendIdsToFrontend":
            {
                var ids = parameters.TryGetProperty("backendNodeIds", out var source)
                    ? source.EnumerateArray().Select(static item => item.GetInt32()).ToArray()
                    : [];
                await RespondAsync(id, new { nodeIds = ids }, cancellationToken)
                    .ConfigureAwait(false);
                return true;
            }
            case "DOM.getOuterHTML":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                await RespondAsync(id, new { outerHTML = node is null ? "" : SerializeNode(snapshot, node) },
                    cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "DOM.resolveNode":
            {
                var nodeId = ReadNodeId(parameters);
                await RespondAsync(id, new
                {
                    @object = new
                    {
                        type = "object",
                        subtype = "node",
                        className = "HTMLElement",
                        description = $"WebScene node {nodeId}",
                        objectId = $"webscene-node-{nodeId}"
                    }
                }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "Runtime.getProperties" when IsWebSceneObject(parameters):
                await RespondAsync(id, new { result = Array.Empty<object>(), internalProperties = Array.Empty<object>() },
                    cancellationToken).ConfigureAwait(false);
                return true;
            case "DOMDebugger.getEventListeners" when IsWebSceneObject(parameters):
                await RespondAsync(id, new { listeners = Array.Empty<object>() },
                    cancellationToken).ConfigureAwait(false);
                return true;
            case "CSS.getComputedStyleForNode":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                await RespondAsync(id, new
                {
                    computedStyle = node?.ComputedStyle.Select(static property => new
                    {
                        name = property.Name,
                        value = property.Value
                    }).ToArray() ?? []
                }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "CSS.getMatchedStylesForNode":
            case "CSS.getInlineStylesForNode":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                var inlineStyle = CreateInlineStyle(node);
                await RespondAsync(id, method.EndsWith("MatchedStylesForNode", StringComparison.Ordinal)
                    ? new
                    {
                        inlineStyle,
                        attributesStyle = (object?)null,
                        matchedCSSRules = Array.Empty<object>(),
                        pseudoElements = Array.Empty<object>(),
                        inherited = Array.Empty<object>(),
                        cssKeyframesRules = Array.Empty<object>()
                    }
                    : new { inlineStyle, attributesStyle = (object?)null },
                    cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "CSS.forcePseudoState":
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            case "Overlay.highlightNode":
            case "DOM.highlightNode":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var requestedNode = FindNode(snapshot, ReadNodeId(parameters));
                var nativeNodeId = requestedNode?.NodeType == 3
                    ? requestedNode.ParentId
                    : requestedNode?.NodeId ?? 0U;
                await _dom!.SetDomHighlightAsync(
                    nativeNodeId, cancellationToken)
                    .ConfigureAwait(false);
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "Overlay.hideHighlight":
            case "DOM.hideHighlight":
                await _dom!.SetDomHighlightAsync(0, cancellationToken).ConfigureAwait(false);
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            case "Overlay.setInspectMode":
            {
                var enabled = parameters.TryGetProperty("mode", out var mode)
                    && !string.Equals(mode.GetString(), "none", StringComparison.Ordinal);
                await _dom!.SetDomInspectModeAsync(enabled, cancellationToken)
                    .ConfigureAwait(false);
                await RespondAsync(id, new { }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "Accessibility.getAXNode":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                var node = FindNode(snapshot, ReadNodeId(parameters));
                await RespondAsync(id, new
                {
                    nodes = node is null
                        ? Array.Empty<object>()
                        : new[] { CreateAxNode(snapshot, node) }
                }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            case "Accessibility.getFullAXTree":
            case "Accessibility.queryAXTree":
            {
                var snapshot = await EnsureSnapshotAsync(cancellationToken)
                    .ConfigureAwait(false);
                await RespondAsync(id, new
                {
                    nodes = snapshot.Nodes
                        .Where(static node => node.NodeType == 1)
                        .Select(node => CreateAxNode(snapshot, node))
                        .ToArray()
                }, cancellationToken).ConfigureAwait(false);
                return true;
            }
            default:
                if (method.StartsWith("DOM.", StringComparison.Ordinal)
                    || method.StartsWith("CSS.", StringComparison.Ordinal)
                    || method.StartsWith("Overlay.", StringComparison.Ordinal)
                    || method.StartsWith("Accessibility.", StringComparison.Ordinal))
                {
                    await RespondErrorAsync(id, -32601,
                        $"'{method}' is not implemented by WebScene.", cancellationToken)
                        .ConfigureAwait(false);
                    return true;
                }
                return false;
        }
    }

    private async ValueTask<NativeDomSnapshot> EnsureSnapshotAsync(
        CancellationToken cancellationToken)
        => await _dom!.GetDomSnapshotAsync(cancellationToken)
            .ConfigureAwait(false);

    private static object CreateDocument(NativeDomSnapshot snapshot, int depth)
        => new
        {
            nodeId = 1,
            backendNodeId = 1,
            nodeType = 9,
            nodeName = "#document",
            localName = "",
            nodeValue = "",
            childNodeCount = snapshot.Nodes.Count(static node => node.ParentId == 0),
            documentURL = "webscene://runtime",
            baseURL = "webscene://runtime",
            children = depth == 0
                ? null
                : snapshot.Nodes.Where(static node => node.ParentId == 0)
                    .Select(node => CreateNode(snapshot, node, depth < 0 ? -1 : depth - 1))
                    .ToArray()
        };

    private static object CreateNode(
        NativeDomSnapshot snapshot,
        NativeDomNodeSnapshot node,
        int depth)
    {
        var children = snapshot.Nodes.Where(candidate => candidate.ParentId == node.NodeId).ToArray();
        var nodeName = node.NodeType == 1
            ? node.NodeName.ToUpperInvariant()
            : node.NodeName;
        return new
        {
            nodeId = ToCdpNodeId(node.NodeId),
            backendNodeId = ToCdpNodeId(node.NodeId),
            nodeType = node.NodeType,
            nodeName,
            localName = node.NodeType == 1 ? node.NodeName : "",
            nodeValue = node.NodeValue,
            childNodeCount = children.Length,
            attributes = node.Attributes
                .SelectMany(static item => new[] { item.Name, item.Value }).ToArray(),
            children = depth == 0 ? null : children
                .Select(child => CreateNode(snapshot, child, depth < 0 ? -1 : depth - 1))
                .ToArray(),
            namespaceURI = node.NamespaceUri
        };
    }

    private static object CreateBoxModel(NativeDomBoxSnapshot box)
    {
        var border = Quad(box.X, box.Y, box.X + box.Width, box.Y + box.Height);
        var padding = Quad(
            box.X + box.BorderLeft,
            box.Y + box.BorderTop,
            box.X + box.Width - box.BorderRight,
            box.Y + box.Height - box.BorderBottom);
        var content = Quad(
            box.X + box.BorderLeft + box.PaddingLeft,
            box.Y + box.BorderTop + box.PaddingTop,
            box.X + box.Width - box.BorderRight - box.PaddingRight,
            box.Y + box.Height - box.BorderBottom - box.PaddingBottom);
        var margin = Quad(
            box.X - box.MarginLeft,
            box.Y - box.MarginTop,
            box.X + box.Width + box.MarginRight,
            box.Y + box.Height + box.MarginBottom);
        return new
        {
            content,
            padding,
            border,
            margin,
            width = box.Width,
            height = box.Height
        };
    }

    private static float[] Quad(float left, float top, float right, float bottom)
        => [left, top, right, top, right, bottom, left, bottom];

    private static object CreateInlineStyle(NativeDomNodeSnapshot? node)
    {
        var cssText = node?.Attributes.FirstOrDefault(static item => item.Name == "style")?.Value ?? "";
        var declarations = cssText.Split(';', StringSplitOptions.RemoveEmptyEntries)
            .Select(static declaration => declaration.Split(':', 2))
            .Where(static parts => parts.Length == 2)
            .Select(static parts => new
            {
                name = parts[0].Trim(),
                value = parts[1].Trim(),
                important = false,
                implicitValue = false,
                parsedOk = true,
                disabled = false
            }).ToArray();
        return new
        {
            cssProperties = declarations,
            shorthandEntries = Array.Empty<object>(),
            cssText
        };
    }

    private static NativeDomNodeSnapshot? FindNode(
        NativeDomSnapshot snapshot,
        int cdpNodeId)
    {
        var nativeId = FromCdpNodeId(cdpNodeId);
        return snapshot.Nodes.FirstOrDefault(node => node.NodeId == nativeId);
    }

    private static int ReadNodeId(JsonElement parameters)
    {
        foreach (var name in new[] { "nodeId", "backendNodeId" })
        {
            if (parameters.ValueKind == JsonValueKind.Object
                && parameters.TryGetProperty(name, out var value)
                && value.TryGetInt32(out var nodeId))
            {
                return nodeId;
            }
        }
        return 0;
    }

    private static int ReadInt(JsonElement parameters, string name, int fallback)
        => parameters.ValueKind == JsonValueKind.Object
            && parameters.TryGetProperty(name, out var value)
            && value.TryGetInt32(out var result) ? result : fallback;

    private static double ReadDouble(JsonElement parameters, string name)
        => parameters.ValueKind == JsonValueKind.Object
            && parameters.TryGetProperty(name, out var value)
            && value.TryGetDouble(out var result) ? result : 0;

    private static bool IsWebSceneObject(JsonElement parameters)
        => parameters.ValueKind == JsonValueKind.Object
        && parameters.TryGetProperty("objectId", out var objectId)
        && objectId.GetString()?.StartsWith("webscene-node-", StringComparison.Ordinal) == true;

    private static object CreateAxNode(
        NativeDomSnapshot snapshot,
        NativeDomNodeSnapshot node)
    {
        var childIds = snapshot.Nodes
            .Where(candidate => candidate.ParentId == node.NodeId
                && candidate.NodeType == 1)
            .Select(static child => $"webscene-ax-{child.NodeId}")
            .ToArray();
        var parentId = node.ParentId == 0
            ? null
            : $"webscene-ax-{node.ParentId}";
        return new
        {
            nodeId = $"webscene-ax-{node.NodeId}",
            ignored = false,
            role = new { type = "role", value = AccessibleRole(node) },
            name = new { type = "computedString", value = AccessibleName(snapshot, node) },
            parentId,
            childIds,
            backendDOMNodeId = ToCdpNodeId(node.NodeId)
        };
    }

    private static string AccessibleName(
        NativeDomSnapshot snapshot,
        NativeDomNodeSnapshot node)
        => node.Attributes.FirstOrDefault(static item => item.Name is "aria-label" or "alt")?.Value
            ?? string.Concat(snapshot.Nodes
                .Where(candidate => candidate.ParentId == node.NodeId
                    && candidate.NodeType == 3)
                .Select(static child => child.NodeValue));

    private static string AccessibleRole(NativeDomNodeSnapshot node)
        => node.Attributes.FirstOrDefault(static item => item.Name == "role")?.Value
            ?? node.NodeName switch
            {
                "a" => "link",
                "button" => "button",
                "h1" or "h2" or "h3" or "h4" or "h5" or "h6" => "heading",
                "input" => "textbox",
                "img" => "image",
                "main" => "main",
                "nav" => "navigation",
                _ => "generic"
            };

    private static ulong ComputeTreeSignature(NativeDomSnapshot snapshot)
    {
        const ulong offset = 1469598103934665603UL;
        const ulong prime = 1099511628211UL;
        var hash = offset;
        void AddUInt(uint value)
        {
            hash ^= value;
            hash *= prime;
        }
        void AddString(string value)
        {
            foreach (var character in value)
            {
                hash ^= character;
                hash *= prime;
            }
            hash ^= 0xFF;
            hash *= prime;
        }
        foreach (var node in snapshot.Nodes)
        {
            AddUInt(node.NodeId);
            AddUInt(node.ParentId);
            AddUInt(node.NodeType);
            AddString(node.NodeName);
            AddString(node.NodeValue);
            foreach (var attribute in node.Attributes)
            {
                AddString(attribute.Name);
                AddString(attribute.Value);
            }
        }
        return hash;
    }

    private static string SerializeNode(
        NativeDomSnapshot snapshot,
        NativeDomNodeSnapshot node)
    {
        if (node.NodeType is 3 or 8)
        {
            return HtmlEncoder.Default.Encode(node.NodeValue);
        }
        var builder = new StringBuilder();
        builder.Append('<').Append(node.NodeName);
        foreach (var attribute in node.Attributes)
        {
            builder.Append(' ').Append(attribute.Name).Append("=\"")
                .Append(HtmlEncoder.Default.Encode(attribute.Value)).Append('"');
        }
        builder.Append('>');
        foreach (var child in snapshot.Nodes.Where(candidate => candidate.ParentId == node.NodeId))
        {
            builder.Append(SerializeNode(snapshot, child));
        }
        builder.Append("</").Append(node.NodeName).Append('>');
        return builder.ToString();
    }

    private static int ToCdpNodeId(uint nativeNodeId)
        => nativeNodeId == 0 ? 0 : checked((int)nativeNodeId + 1);

    private static uint FromCdpNodeId(int cdpNodeId)
        => cdpNodeId <= 1 ? 0U : checked((uint)(cdpNodeId - 1));

    private ValueTask RespondAsync(
        JsonElement id,
        object result,
        CancellationToken cancellationToken)
        => PublishAsync(new { id = ReadRequestId(id), result }, cancellationToken);

    private ValueTask RespondErrorAsync(
        JsonElement id,
        int code,
        string message,
        CancellationToken cancellationToken)
        => PublishAsync(new { id = ReadRequestId(id), error = new { code, message } }, cancellationToken);

    private ValueTask RespondNodeNotFoundAsync(
        JsonElement id,
        CancellationToken cancellationToken)
        => RespondErrorAsync(id, -32000, "Could not find node with given id.", cancellationToken);

    private ValueTask PublishEventAsync(
        string method,
        object? parameters,
        CancellationToken cancellationToken)
        => PublishAsync(parameters is null ? new { method } : new { method, @params = parameters },
            cancellationToken);

    private ValueTask PublishAsync(object value, CancellationToken cancellationToken)
        => _outgoing.Writer.WriteAsync(
            JsonSerializer.SerializeToUtf8Bytes(value), cancellationToken);

    private static object ReadRequestId(JsonElement id)
        => id.ValueKind == JsonValueKind.String
            ? id.GetString()!
            : id.TryGetInt64(out var number) ? number : 0L;

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        _outgoing.Writer.TryComplete();
        if (_dom is not null)
        {
            try
            {
                await _dom.SetDomInspectModeAsync(false).ConfigureAwait(false);
                await _dom.SetDomHighlightAsync(0).ConfigureAwait(false);
            }
            catch (InvalidOperationException)
            {
            }
        }
        await _v8.DisposeAsync().ConfigureAwait(false);
    }
}
