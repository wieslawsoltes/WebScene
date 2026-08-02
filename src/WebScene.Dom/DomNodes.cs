using WebScene.Core;
using System.Runtime.CompilerServices;

namespace WebScene.Dom;

/// <summary>
/// Stable JavaScript/DOM identity shared by documents, elements, text nodes, and
/// document fragments independently of any backend object.
/// </summary>
public abstract class DomNodeCore
{
    private static long s_nextJavaScriptIdentity;

    protected DomNodeCore()
    {
        __webSceneDomIdentity = Interlocked.Increment(ref s_nextJavaScriptIdentity);
    }

    public long __webSceneDomIdentity { get; }

    public WebSceneNodeId DomNodeId => new(__webSceneDomIdentity);
}

public abstract class DomDocumentCore : DomNodeCore
{
}

/// <summary>
/// Typed portable document state shared by backend documents. Keeping the active
/// element field on this generic base preserves the concrete backend node type and
/// object layout without casts, handles, or a separate focus-state allocation.
/// </summary>
public abstract class DomDocumentCore<TElement> : DomDocumentCore
    where TElement : DomElementCore
{
    protected TElement? _virtualActiveElement;
}

/// <summary>
/// Backend-neutral identity and attribute state for a DOM element. Presentation
/// adapters inherit this storage directly; no wrapper or copied dictionary is added
/// to element access paths.
/// </summary>
public abstract class DomElementCore : DomNodeCore
{
    protected DomElementCore(WebSceneBackendHandle backendHandle)
    {
        BackendHandle = backendHandle;
    }

    public WebSceneBackendHandle BackendHandle { get; }

    protected readonly Dictionary<string, string?> _attributes =
        new(StringComparer.OrdinalIgnoreCase);

    protected readonly HashSet<string> _attributeNames =
        new(StringComparer.OrdinalIgnoreCase);

    protected readonly Dictionary<string, string?> _dataAttributes =
        new(StringComparer.OrdinalIgnoreCase);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    protected void SetGenericAttribute(string attributeName, string? value)
    {
        attributeName = attributeName.ToLowerInvariant();
        if (value is null)
        {
            _attributes.Remove(attributeName);
        }
        else
        {
            _attributes[attributeName] = value;
        }
    }

    protected bool TryGetGenericAttribute(string attributeName, out string? value)
        => _attributes.TryGetValue(attributeName.ToLowerInvariant(), out value);

    protected void SetAttributePresence(string attributeName, bool present)
    {
        attributeName = attributeName.ToLowerInvariant();
        if (present)
        {
            _attributeNames.Add(attributeName);
        }
        else
        {
            _attributeNames.Remove(attributeName);
        }
    }

    protected bool HasAttributePresence(string attributeName)
        => _attributeNames.Contains(attributeName.ToLowerInvariant());
}
