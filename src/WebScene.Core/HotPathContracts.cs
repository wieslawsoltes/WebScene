namespace WebScene.Core;

/// <summary>
/// Typed geometry seam for engine adapters. Implementations write x, y, width,
/// height, right, bottom, client width, and client height without boxing.
/// </summary>
public interface IWebSceneGeometryWriter
{
    bool TryWriteGeometry(WebSceneNodeId nodeId, Span<double> destination);
}

public readonly record struct WebSceneEventPacket(
    WebSceneNodeId Target,
    int EventType,
    double X,
    double Y,
    int Flags);

public interface IWebSceneEventPacketSink
{
    void Dispatch(in WebSceneEventPacket packet);
}

/// <summary>
/// Batch boundary used by JavaScript engines to transfer Canvas operations without
/// per-operation reflection, boxing, or dictionary-shaped calls.
/// </summary>
public interface IWebSceneCanvasPacketSink
{
    void Replay(ReadOnlySpan<double> values, IReadOnlyList<string> strings);
}
