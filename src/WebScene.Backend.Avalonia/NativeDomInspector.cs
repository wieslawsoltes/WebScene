using System.Runtime.InteropServices;
using System.Text;
using WebScene.Backends;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomStringRef
{
    public uint ByteOffset;
    public uint ByteLength;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomAttribute
{
    public NativeDomStringRef Name;
    public NativeDomStringRef Value;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomProperty
{
    public NativeDomStringRef Name;
    public NativeDomStringRef Value;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomBox
{
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public float MarginLeft;
    public float MarginTop;
    public float MarginRight;
    public float MarginBottom;
    public float BorderLeft;
    public float BorderTop;
    public float BorderRight;
    public float BorderBottom;
    public float PaddingLeft;
    public float PaddingTop;
    public float PaddingRight;
    public float PaddingBottom;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomNode
{
    public uint NodeId;
    public uint ParentId;
    public uint NodeType;
    public uint Flags;
    public uint ChildCount;
    public uint AttributeOffset;
    public uint AttributeCount;
    public uint PropertyOffset;
    public uint PropertyCount;
    public NativeDomStringRef NodeName;
    public NativeDomStringRef NodeValue;
    public NativeDomStringRef NamespaceUri;
    public NativeDomBox Box;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeDomSnapshotView
{
    public uint StructSize;
    public uint AbiVersion;
    public ulong DocumentRevision;
    public ulong DocumentEpoch;
    public NativeDomNode* Nodes;
    public NativeDomAttribute* Attributes;
    public NativeDomProperty* Properties;
    public byte* StringBytes;
    public void* LeaseToken;
    public uint NodeCount;
    public uint AttributeCount;
    public uint PropertyCount;
    public uint StringByteCount;
    public uint HighlightedNodeId;
    public uint SelectedNodeId;
    public ulong SelectionSequence;
}

public static unsafe partial class NativeWebSceneApi
{
    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_dom_snapshot")]
    private static extern IntPtr EngineAcquireDomSnapshot(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_dom_snapshot_release")]
    private static extern void DomSnapshotRelease(IntPtr snapshot);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_dom_inspect_mode")]
    private static extern byte EngineSetDomInspectMode(IntPtr engine, byte enabled);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_dom_highlight")]
    private static extern byte EngineSetDomHighlight(IntPtr engine, uint nodeId);

    internal static NativeDomSnapshot? TryGetDomSnapshot(IntPtr engine)
    {
        var snapshot = EngineAcquireDomSnapshot(engine);
        if (snapshot == IntPtr.Zero) return null;
        try
        {
            var view = (NativeDomSnapshotView*)snapshot;
            if (view->StructSize < (uint)sizeof(NativeDomSnapshotView)
                || view->AbiVersion != NativeWebSceneRuntime.RequiredAbiVersion
                || view->NodeCount > int.MaxValue
                || view->AttributeCount > int.MaxValue
                || view->PropertyCount > int.MaxValue
                || view->StringByteCount > int.MaxValue
                || (view->NodeCount != 0 && view->Nodes == null)
                || (view->AttributeCount != 0 && view->Attributes == null)
                || (view->PropertyCount != 0 && view->Properties == null)
                || (view->StringByteCount != 0 && view->StringBytes == null))
            {
                throw new InvalidOperationException(
                    "The native DOM diagnostics snapshot is malformed.");
            }

            var nodes = new NativeDomNodeSnapshot[checked((int)view->NodeCount)];
            for (var index = 0; index < nodes.Length; index++)
            {
                var source = view->Nodes[index];
                if ((ulong)source.AttributeOffset + source.AttributeCount
                        > view->AttributeCount
                    || (ulong)source.PropertyOffset + source.PropertyCount
                        > view->PropertyCount)
                {
                    throw new InvalidOperationException(
                        "A native DOM diagnostics node range is invalid.");
                }
                var attributes = new NativeDomAttributeSnapshot[
                    checked((int)source.AttributeCount)];
                for (var item = 0; item < attributes.Length; item++)
                {
                    var attribute = view->Attributes[source.AttributeOffset + item];
                    attributes[item] = new NativeDomAttributeSnapshot(
                        ReadDomString(view, attribute.Name),
                        ReadDomString(view, attribute.Value));
                }
                var properties = new NativeDomPropertySnapshot[
                    checked((int)source.PropertyCount)];
                for (var item = 0; item < properties.Length; item++)
                {
                    var property = view->Properties[source.PropertyOffset + item];
                    properties[item] = new NativeDomPropertySnapshot(
                        ReadDomString(view, property.Name),
                        ReadDomString(view, property.Value));
                }
                var box = source.Box;
                nodes[index] = new NativeDomNodeSnapshot(
                    source.NodeId,
                    source.ParentId,
                    source.NodeType,
                    ReadDomString(view, source.NodeName),
                    ReadDomString(view, source.NodeValue),
                    ReadDomString(view, source.NamespaceUri),
                    attributes,
                    properties,
                    new NativeDomBoxSnapshot(
                        box.X, box.Y, box.Width, box.Height,
                        box.MarginLeft, box.MarginTop,
                        box.MarginRight, box.MarginBottom,
                        box.BorderLeft, box.BorderTop,
                        box.BorderRight, box.BorderBottom,
                        box.PaddingLeft, box.PaddingTop,
                        box.PaddingRight, box.PaddingBottom),
                    source.ChildCount,
                    (source.Flags & 1U) != 0);
            }
            return new NativeDomSnapshot(
                view->DocumentRevision,
                view->DocumentEpoch,
                nodes,
                view->HighlightedNodeId,
                view->SelectedNodeId,
                view->SelectionSequence);
        }
        finally
        {
            DomSnapshotRelease(snapshot);
        }
    }

    internal static bool SetDomInspectMode(IntPtr engine, bool enabled)
        => EngineSetDomInspectMode(engine, enabled ? (byte)1 : (byte)0) != 0;

    internal static bool SetDomHighlight(IntPtr engine, uint nodeId)
        => EngineSetDomHighlight(engine, nodeId) != 0;

    private static string ReadDomString(
        NativeDomSnapshotView* view,
        NativeDomStringRef value)
    {
        var end = (ulong)value.ByteOffset + value.ByteLength;
        if (end > view->StringByteCount)
        {
            throw new InvalidOperationException(
                "A native DOM diagnostics string is out of range.");
        }
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(
            view->StringBytes + value.ByteOffset,
            checked((int)value.ByteLength)));
    }
}
