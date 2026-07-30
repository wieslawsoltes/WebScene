using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using BenchmarkDotNet.Attributes;
using WebScene.Backends.Avalonia.Native;

namespace JavaScript.Avalonia.Benchmarks;

[MemoryDiagnoser]
public unsafe class NativeInteropTransportBenchmarks
{
    private string _json = null!;
    private byte[] _utf8Json = null!;
    private byte* _leasedUtf8Json;
    private NativeInteropResultView* _binaryView;

    [Params(16, 256, 4096)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var quotes = Enumerable.Range(0, Count)
            .Select(index => new NativeInteropBenchmarkQuote(
                $"SYM{index:D5}",
                100.0 + index * 0.125,
                1_000L + index))
            .ToArray();
        _json = JsonSerializer.Serialize(quotes);
        _utf8Json = JsonSerializer.SerializeToUtf8Bytes(
            quotes,
            NativeInteropBenchmarkJsonContext.Default
                .NativeInteropBenchmarkQuoteArray);
        _leasedUtf8Json = (byte*)NativeMemory.Alloc(
            checked((nuint)_utf8Json.Length));
        _utf8Json.CopyTo(new Span<byte>(_leasedUtf8Json, _utf8Json.Length));
        _binaryView = CreateBinaryArena(quotes);
    }

    [GlobalCleanup]
    public void Cleanup()
    {
        if (_binaryView != null)
        {
            NativeMemory.Free(_binaryView->Values);
            NativeMemory.Free(_binaryView->Edges);
            NativeMemory.Free(_binaryView->Utf8Bytes);
            NativeMemory.Free(_binaryView);
            _binaryView = null;
        }
        NativeMemory.Free(_leasedUtf8Json);
        _leasedUtf8Json = null;
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Materialized")]
    public NativeInteropBenchmarkQuote[] CurrentJsonString()
        => JsonSerializer.Deserialize<NativeInteropBenchmarkQuote[]>(_json)!;

    [Benchmark]
    [BenchmarkCategory("Materialized")]
    public NativeInteropBenchmarkQuote[] PooledUtf8JsonSourceGenerated()
        => JsonSerializer.Deserialize(
            _utf8Json,
            NativeInteropBenchmarkJsonContext.Default
                .NativeInteropBenchmarkQuoteArray)!;

    [Benchmark]
    [BenchmarkCategory("Materialized")]
    public NativeInteropBenchmarkQuote[] LeasedNativeUtf8Json()
        => JsonSerializer.Deserialize(
            new ReadOnlySpan<byte>(_leasedUtf8Json, _utf8Json.Length),
            NativeInteropBenchmarkJsonContext.Default
                .NativeInteropBenchmarkQuoteArray)!;

    [Benchmark]
    [BenchmarkCategory("Materialized")]
    public NativeInteropBenchmarkQuote[] TaggedBinaryMaterialized()
    {
        var root = new NativeInteropValue(
            _binaryView,
            _binaryView->RootValueIndex);
        var result = new NativeInteropBenchmarkQuote[root.Count];
        for (var index = 0; index < result.Length; ++index)
        {
            var quote = root.GetArrayItem(index);
            quote.TryGetProperty("Symbol"u8, out var symbol);
            quote.TryGetProperty("Price"u8, out var price);
            quote.TryGetProperty("Volume"u8, out var volume);
            result[index] = new NativeInteropBenchmarkQuote(
                symbol.GetString(),
                price.GetNumber(),
                checked((long)volume.GetNumber()));
        }
        return result;
    }

    [Benchmark]
    [BenchmarkCategory("Borrowed")]
    public double TaggedBinaryBorrowed()
    {
        var root = new NativeInteropValue(
            _binaryView,
            _binaryView->RootValueIndex);
        var checksum = 0.0;
        for (var index = 0; index < root.Count; ++index)
        {
            var quote = root.GetArrayItem(index);
            quote.TryGetProperty("Symbol"u8, out var symbol);
            quote.TryGetProperty("Price"u8, out var price);
            quote.TryGetProperty("Volume"u8, out var volume);
            checksum += symbol.Utf8.Length;
            checksum += price.GetNumber();
            checksum += volume.GetNumber();
        }
        return checksum;
    }

    private static NativeInteropResultView* CreateBinaryArena(
        NativeInteropBenchmarkQuote[] quotes)
    {
        var values = new NativeInteropValueData[1 + quotes.Length * 4];
        var edges = new NativeInteropEdgeData[quotes.Length * 4];
        var utf8 = new List<byte>(
            Encoding.UTF8.GetByteCount("SymbolPriceVolume")
            + quotes.Sum(quote => Encoding.UTF8.GetByteCount(quote.Symbol)));

        var symbolName = AppendUtf8(utf8, "Symbol");
        var priceName = AppendUtf8(utf8, "Price");
        var volumeName = AppendUtf8(utf8, "Volume");
        values[0] = new NativeInteropValueData
        {
            Kind = NativeInteropValueKind.Array,
            Offset = 0,
            Length = checked((uint)quotes.Length)
        };

        for (var index = 0; index < quotes.Length; ++index)
        {
            var objectIndex = 1 + index * 4;
            var propertyEdgeIndex = quotes.Length + index * 3;
            var symbol = AppendUtf8(utf8, quotes[index].Symbol);
            values[objectIndex] = new NativeInteropValueData
            {
                Kind = NativeInteropValueKind.Object,
                Offset = checked((uint)propertyEdgeIndex),
                Length = 3
            };
            values[objectIndex + 1] = new NativeInteropValueData
            {
                Kind = NativeInteropValueKind.String,
                Offset = symbol.Offset,
                Length = symbol.Length
            };
            values[objectIndex + 2] = Number(quotes[index].Price);
            values[objectIndex + 3] = Number(quotes[index].Volume);

            edges[index].ValueIndex = checked((uint)objectIndex);
            edges[propertyEdgeIndex] = Property(symbolName, objectIndex + 1);
            edges[propertyEdgeIndex + 1] = Property(
                priceName,
                objectIndex + 2);
            edges[propertyEdgeIndex + 2] = Property(
                volumeName,
                objectIndex + 3);
        }

        var view = (NativeInteropResultView*)NativeMemory.Alloc(
            checked((nuint)sizeof(NativeInteropResultView)));
        var nativeValues = (NativeInteropValueData*)NativeMemory.Alloc(
            checked((nuint)(values.Length * sizeof(NativeInteropValueData))));
        var nativeEdges = (NativeInteropEdgeData*)NativeMemory.Alloc(
            checked((nuint)(edges.Length * sizeof(NativeInteropEdgeData))));
        var nativeUtf8 = (byte*)NativeMemory.Alloc(
            checked((nuint)utf8.Count));
        values.CopyTo(new Span<NativeInteropValueData>(
            nativeValues,
            values.Length));
        edges.CopyTo(new Span<NativeInteropEdgeData>(
            nativeEdges,
            edges.Length));
        CollectionsMarshal.AsSpan(utf8).CopyTo(
            new Span<byte>(nativeUtf8, utf8.Count));
        *view = new NativeInteropResultView
        {
            StructSize = (uint)sizeof(NativeInteropResultView),
            Version = 3,
            Status = NativeInteropResultStatus.Succeeded,
            Values = nativeValues,
            ValueCount = checked((uint)values.Length),
            Edges = nativeEdges,
            EdgeCount = checked((uint)edges.Length),
            Utf8Bytes = nativeUtf8,
            Utf8ByteCount = checked((uint)utf8.Count)
        };
        return view;
    }

    private static NativeInteropValueData Number(double value)
        => new()
        {
            Kind = NativeInteropValueKind.Number,
            Payload = unchecked(
                (ulong)BitConverter.DoubleToInt64Bits(value))
        };

    private static NativeInteropEdgeData Property(
        (uint Offset, uint Length) name,
        int valueIndex)
        => new()
        {
            NameOffset = name.Offset,
            NameLength = name.Length,
            ValueIndex = checked((uint)valueIndex)
        };

    private static (uint Offset, uint Length) AppendUtf8(
        List<byte> destination,
        string value)
    {
        var offset = destination.Count;
        destination.AddRange(Encoding.UTF8.GetBytes(value));
        return (checked((uint)offset), checked((uint)(destination.Count - offset)));
    }
}

public sealed record NativeInteropBenchmarkQuote(
    string Symbol,
    double Price,
    long Volume);

[JsonSerializable(typeof(NativeInteropBenchmarkQuote[]))]
internal partial class NativeInteropBenchmarkJsonContext
    : JsonSerializerContext;
