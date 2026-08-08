namespace WebScene.Backends;

/// <summary>
/// Read-only diagnostics over the native-authored DOM. Implementations return
/// immutable worker snapshots; callers never traverse live runtime objects.
/// </summary>
public interface INativeDomInspector
{
    ValueTask<NativeDomSnapshot> GetDomSnapshotAsync(
        CancellationToken cancellationToken = default);

    ValueTask SetDomInspectModeAsync(
        bool enabled,
        CancellationToken cancellationToken = default);

    ValueTask SetDomHighlightAsync(
        uint nativeNodeId,
        CancellationToken cancellationToken = default);
}

public sealed record NativeDomSnapshot(
    ulong DocumentRevision,
    ulong DocumentEpoch,
    IReadOnlyList<NativeDomNodeSnapshot> Nodes,
    uint HighlightedNodeId,
    uint SelectedNodeId,
    ulong SelectionSequence);

public sealed record NativeDomNodeSnapshot(
    uint NodeId,
    uint ParentId,
    uint NodeType,
    string NodeName,
    string NodeValue,
    string NamespaceUri,
    IReadOnlyList<NativeDomAttributeSnapshot> Attributes,
    IReadOnlyList<NativeDomPropertySnapshot> ComputedStyle,
    NativeDomBoxSnapshot Box,
    uint ChildCount,
    bool IsVisible);

public sealed record NativeDomAttributeSnapshot(string Name, string Value);

public sealed record NativeDomPropertySnapshot(string Name, string Value);

public readonly record struct NativeDomBoxSnapshot(
    float X,
    float Y,
    float Width,
    float Height,
    float MarginLeft,
    float MarginTop,
    float MarginRight,
    float MarginBottom,
    float BorderLeft,
    float BorderTop,
    float BorderRight,
    float BorderBottom,
    float PaddingLeft,
    float PaddingTop,
    float PaddingRight,
    float PaddingBottom);
