using System.Text;
using System.Text.Json;
using WebScene.JavaScript.Interop;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class JavaScriptBinaryInteropTests
{
    [Fact]
    public void WriterBuildsRealtimeBarArgumentsWithoutJson()
    {
        var writer = new JavaScriptBinaryWriter();
        try
        {
            var arguments = WriteRealtimeArguments(
                ref writer,
                "subscriber-7",
                1_785_413_968_000,
                101.25,
                103.5,
                100.75,
                102.875,
                42.5);

            Assert.Equal(9, writer.Values.Length);
            Assert.Equal(8, writer.Edges.Length);
            Assert.DoesNotContain((byte)'{', writer.Utf8.ToArray());
            Assert.DoesNotContain((byte)'[', writer.Utf8.ToArray());

            unsafe
            {
                fixed (JavaScriptBinaryValueData* values = writer.Values)
                fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                fixed (byte* utf8 = writer.Utf8)
                {
                    var root = new JavaScriptBinaryValue(
                        values,
                        checked((uint)writer.Values.Length),
                        edges,
                        checked((uint)writer.Edges.Length),
                        utf8,
                        checked((uint)writer.Utf8.Length),
                        arguments);
                    Assert.Equal("subscriber-7", root.GetArrayItem(0).GetString());
                    var bar = root.GetArrayItem(1);
                    Assert.Equal(
                        1_785_413_968_000,
                        bar.GetRequiredProperty("time"u8).GetNumber());
                    Assert.Equal(
                        102.875,
                        bar.GetRequiredProperty("close"u8).GetNumber());
                    Assert.Equal(
                        42.5,
                        bar.GetRequiredProperty("volume"u8).GetNumber());
                }
            }
        }
        finally
        {
            writer.Dispose();
        }
    }

    [Fact]
    public void WriterUsesOnlyPooledStorageAfterWarmup()
    {
        WriteAndDispose();
        var before = GC.GetAllocatedBytesForCurrentThread();
        for (var iteration = 0; iteration < 1_000; iteration++)
        {
            WriteAndDispose();
        }
        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;

        Assert.True(
            allocated <= 1_024,
            $"Expected pooled binary writes to allocate at most 1 KiB, allocated {allocated} bytes.");
    }

    [Fact]
    public unsafe void DynamicJsonRoundTripsThroughTaggedBinaryArena()
    {
        using var source = JsonDocument.Parse(
            """
            {
              "settings": {
                "theme": "dark",
                "empty": ""
              },
              "layoutCount": 4,
              "enabled": true,
              "items": [1, null, "three"]
            }
            """);
        var writer = new JavaScriptBinaryWriter();
        try
        {
            var root = writer.WriteJsonElement(source.RootElement);
            fixed (JavaScriptBinaryValueData* values = writer.Values)
            fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
            fixed (byte* utf8 = writer.Utf8)
            {
                var encoded = new JavaScriptBinaryValue(
                    values,
                    checked((uint)writer.Values.Length),
                    edges,
                    checked((uint)writer.Edges.Length),
                    utf8,
                    checked((uint)writer.Utf8.Length),
                    root);
                var decoded = encoded.GetJsonElement();

                Assert.True(
                    JsonElement.DeepEquals(source.RootElement, decoded));
            }
        }
        finally
        {
            writer.Dispose();
        }
    }

    [Fact]
    public unsafe void ReaderRejectsMalformedUtf8AndEdgeOffsets()
    {
        var values = new JavaScriptBinaryValueData[]
        {
            new()
            {
                Kind = JavaScriptBinaryValueKind.String,
                Offset = 4,
                Length = 2
            },
            new()
            {
                Kind = JavaScriptBinaryValueKind.Array,
                Offset = 2,
                Length = 1
            }
        };
        var edges = new JavaScriptBinaryEdgeData[1];
        var utf8 = Encoding.UTF8.GetBytes("abc");

        fixed (JavaScriptBinaryValueData* valuePointer = values)
        fixed (JavaScriptBinaryEdgeData* edgePointer = edges)
        fixed (byte* utf8Pointer = utf8)
        {
            var invalidString = new JavaScriptBinaryValue(
                valuePointer,
                2,
                edgePointer,
                1,
                utf8Pointer,
                3,
                0);
            AssertInvalidUtf8(invalidString);

            var invalidArray = new JavaScriptBinaryValue(
                valuePointer,
                2,
                edgePointer,
                1,
                utf8Pointer,
                3,
                1);
            AssertInvalidCount(invalidArray);
        }
    }

    [Fact]
    public void BorrowScopeReleasesItsTokenAfterOwningLeaseIsDisposed()
    {
        var lease = new TrackingResultLease();
        var scope = lease.Borrow();

        lease.Dispose();
        _ = scope.Root;
        scope.Dispose();

        Assert.True(lease.IsDisposed);
        Assert.Equal(1, lease.ReleaseCount);
        Assert.Same(lease.Token, lease.ReleasedToken);
    }

    private static void WriteAndDispose()
    {
        var writer = new JavaScriptBinaryWriter();
        try
        {
            _ = WriteRealtimeArguments(
                ref writer,
                "subscriber-7",
                1_785_413_968_000,
                101.25,
                103.5,
                100.75,
                102.875,
                42.5);
        }
        finally
        {
            writer.Dispose();
        }
    }

    private static uint WriteRealtimeArguments(
        ref JavaScriptBinaryWriter writer,
        string subscriber,
        double time,
        double open,
        double high,
        double low,
        double close,
        double volume)
    {
        var arguments = writer.BeginArray(2);
        writer.SetArrayItem(arguments, 0, writer.WriteString(subscriber));
        var bar = writer.BeginObject(6);
        writer.SetObjectProperty(bar, 0, "time"u8, writer.WriteNumber(time));
        writer.SetObjectProperty(bar, 1, "open"u8, writer.WriteNumber(open));
        writer.SetObjectProperty(bar, 2, "high"u8, writer.WriteNumber(high));
        writer.SetObjectProperty(bar, 3, "low"u8, writer.WriteNumber(low));
        writer.SetObjectProperty(bar, 4, "close"u8, writer.WriteNumber(close));
        writer.SetObjectProperty(bar, 5, "volume"u8, writer.WriteNumber(volume));
        writer.SetArrayItem(arguments, 1, bar);
        return arguments;
    }

    private static void AssertInvalidUtf8(JavaScriptBinaryValue value)
    {
        try
        {
            _ = value.Utf8.Length;
            Assert.Fail("Expected an invalid UTF-8 range.");
        }
        catch (InvalidDataException)
        {
        }
    }

    private static void AssertInvalidCount(JavaScriptBinaryValue value)
    {
        try
        {
            _ = value.Count;
            Assert.Fail("Expected an invalid edge range.");
        }
        catch (InvalidDataException)
        {
        }
    }

    private sealed class TrackingResultLease : JavaScriptBinaryResultLease
    {
        internal object Token { get; } = new();

        internal object? ReleasedToken { get; private set; }

        internal int ReleaseCount { get; private set; }

        internal bool IsDisposed { get; private set; }

        protected override JavaScriptBinaryValue AcquireBorrow(
            out object? borrowToken)
        {
            ObjectDisposedException.ThrowIf(IsDisposed, this);
            borrowToken = Token;
            return default;
        }

        protected override void ReleaseBorrow(object? borrowToken)
        {
            ReleasedToken = borrowToken;
            ReleaseCount++;
        }

        public override void Dispose()
            => IsDisposed = true;
    }
}
