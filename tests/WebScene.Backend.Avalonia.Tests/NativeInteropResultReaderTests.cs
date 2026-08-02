using System.Text;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed unsafe class NativeInteropResultReaderTests
{
    [Fact]
    public void Borrowed_value_traverses_object_array_and_utf8_without_materializing()
    {
        var utf8 = Encoding.UTF8.GetBytes("symbolvaluesAAPL€");
        var values = new NativeInteropValueData[7];
        values[0] = new()
        {
            Kind = NativeInteropValueKind.Object,
            Offset = 0,
            Length = 2
        };
        values[1] = new()
        {
            Kind = NativeInteropValueKind.String,
            Offset = 12,
            Length = 7
        };
        values[2] = new()
        {
            Kind = NativeInteropValueKind.Array,
            Offset = 2,
            Length = 4
        };
        values[3] = new()
        {
            Kind = NativeInteropValueKind.Boolean,
            Payload = 1
        };
        values[4] = new()
        {
            Kind = NativeInteropValueKind.Number,
            Payload = unchecked((ulong)BitConverter.DoubleToInt64Bits(187.5))
        };
        values[5] = new()
        {
            Kind = NativeInteropValueKind.Handle,
            Payload = 42
        };
        values[6] = new() { Kind = NativeInteropValueKind.Undefined };

        var edges = new NativeInteropEdgeData[6];
        edges[0] = new()
        {
            NameOffset = 0,
            NameLength = 6,
            ValueIndex = 1
        };
        edges[1] = new()
        {
            NameOffset = 6,
            NameLength = 6,
            ValueIndex = 2
        };
        edges[2].ValueIndex = 3;
        edges[3].ValueIndex = 4;
        edges[4].ValueIndex = 5;
        edges[5].ValueIndex = 6;

        fixed (byte* utf8Pointer = utf8)
        fixed (NativeInteropValueData* valuePointer = values)
        fixed (NativeInteropEdgeData* edgePointer = edges)
        {
            var view = CreateView(
                valuePointer,
                values.Length,
                edgePointer,
                edges.Length,
                utf8Pointer,
                utf8.Length);
            var root = new NativeInteropValue(&view, 0);

            Assert.Equal(NativeInteropValueKind.Object, root.Kind);
            Assert.Equal(2, root.Count);
            Assert.True(root.TryGetProperty("symbol"u8, out var symbol));
            Assert.True(symbol.Utf8.SequenceEqual("AAPL€"u8));
            Assert.Equal("AAPL€", symbol.GetString());
            Assert.True(root.TryGetProperty("values"u8, out var array));
            Assert.Equal(4, array.Count);
            Assert.True(array.GetArrayItem(0).GetBoolean());
            Assert.Equal(187.5, array.GetArrayItem(1).GetNumber());
            Assert.Equal(42, array.GetArrayItem(2).GetHandle());
            Assert.Equal(
                NativeInteropValueKind.Undefined,
                array.GetArrayItem(3).Kind);
        }
    }

    [Fact]
    public void Borrowed_value_rejects_malformed_offsets()
    {
        var utf8 = new byte[] { (byte)'x' };
        var values = new[]
        {
            new NativeInteropValueData
            {
                Kind = NativeInteropValueKind.String,
                Offset = 2,
                Length = 1
            }
        };

        fixed (byte* utf8Pointer = utf8)
        fixed (NativeInteropValueData* valuePointer = values)
        {
            var view = CreateView(
                valuePointer,
                values.Length,
                null,
                0,
                utf8Pointer,
                utf8.Length);
            var value = new NativeInteropValue(&view, 0);
            try
            {
                _ = value.Utf8.Length;
                Assert.Fail("Malformed UTF-8 offsets should be rejected.");
            }
            catch (InvalidDataException)
            {
            }
        }
    }

    [Fact]
    public void Borrowed_value_rejects_malformed_child_indices()
    {
        var values = new[]
        {
            new NativeInteropValueData
            {
                Kind = NativeInteropValueKind.Array,
                Length = 1
            }
        };
        var edges = new[]
        {
            new NativeInteropEdgeData { ValueIndex = 99 }
        };

        fixed (NativeInteropValueData* valuePointer = values)
        fixed (NativeInteropEdgeData* edgePointer = edges)
        {
            var view = CreateView(
                valuePointer,
                values.Length,
                edgePointer,
                edges.Length,
                null,
                0);
            var value = new NativeInteropValue(&view, 0);
            try
            {
                _ = value.GetArrayItem(0);
                Assert.Fail("Malformed child indices should be rejected.");
            }
            catch (InvalidDataException)
            {
            }
        }
    }

    private static NativeInteropResultView CreateView(
        NativeInteropValueData* values,
        int valueCount,
        NativeInteropEdgeData* edges,
        int edgeCount,
        byte* utf8,
        int utf8Length)
        => new()
        {
            StructSize = (uint)sizeof(NativeInteropResultView),
            Version = 3,
            Status = NativeInteropResultStatus.Succeeded,
            Values = values,
            ValueCount = checked((uint)valueCount),
            Edges = edges,
            EdgeCount = checked((uint)edgeCount),
            Utf8Bytes = utf8,
            Utf8ByteCount = checked((uint)utf8Length)
        };
}
