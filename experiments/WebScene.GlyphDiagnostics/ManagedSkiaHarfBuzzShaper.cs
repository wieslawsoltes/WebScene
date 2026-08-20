using System.Runtime.InteropServices;
using HarfBuzzSharp;
using SkiaSharp;
using Buffer = HarfBuzzSharp.Buffer;

internal sealed class ManagedSkiaHarfBuzzShaper : IDisposable
{
    private const int PositionScale = 64;
    private readonly Font _openTypeFont;
    private readonly Font _font;
    private readonly FontFunctions _functions;

    internal ManagedSkiaHarfBuzzShaper(
        SKTypeface typeface,
        SKFont skiaFont,
        bool propagateRequestedVariations,
        float requestedOpticalSize,
        int requestedWeight)
    {
        ArgumentNullException.ThrowIfNull(typeface);
        ArgumentNullException.ThrowIfNull(skiaFont);

        using var stream = typeface.OpenStream(out var collectionIndex)
            ?? throw new InvalidOperationException("The Skia typeface did not expose font data.");
        var fontData = new byte[stream.Length];
        var pinnedFontData = GCHandle.Alloc(fontData, GCHandleType.Pinned);
        var offset = 0;
        try
        {
            while (offset < fontData.Length)
            {
                var read = stream.Read(
                    IntPtr.Add(pinnedFontData.AddrOfPinnedObject(), offset),
                    fontData.Length - offset);
                if (read <= 0) break;
                offset += read;
            }
        }
        finally
        {
            pinnedFontData.Free();
        }
        if (offset != fontData.Length)
        {
            throw new EndOfStreamException(
                $"Read {offset} of {fontData.Length} bytes from the Skia typeface.");
        }

        using var blob = CreateCopiedBlob(fontData);
        using var face = new Face(blob, collectionIndex)
        {
            Index = collectionIndex,
            UnitsPerEm = typeface.UnitsPerEm
        };
        _openTypeFont = new Font(face);
        var harfBuzzScale = checked((int)MathF.Round(skiaFont.Size * PositionScale));
        _openTypeFont.SetScale(harfBuzzScale, harfBuzzScale);
        _openTypeFont.SetFunctionsOpenType();
        if (propagateRequestedVariations)
        {
            var variations = new[]
            {
                new HarfBuzzVariation(Tag("opsz"), requestedOpticalSize),
                new HarfBuzzVariation(Tag("wght"), requestedWeight)
            };
            HarfBuzzNative.SetVariations(
                _openTypeFont.Handle,
                variations,
                checked((uint)variations.Length));
        }

        // A sub-font inherits OpenType glyph selection, GPOS, extents and every
        // callback we do not replace. Only advances are redirected to the same
        // SKFont instance that will paint the resulting glyphs.
        _font = new Font(_openTypeFont);
        _font.SetScale(harfBuzzScale, harfBuzzScale);
        _functions = new FontFunctions();
        _functions.SetHorizontalGlyphAdvanceDelegate(GetHorizontalAdvance);
        _functions.MakeImmutable();
        _font.SetFontFunctions(_functions, new FontMetricState(skiaFont));
    }

    internal ManagedShapeResult Shape(string text, float baseline)
    {
        if (string.IsNullOrEmpty(text))
        {
            return new ManagedShapeResult([], [], [], 0);
        }

        using var buffer = new Buffer();
        buffer.AddUtf8(text);
        buffer.GuessSegmentProperties();
        _font.Shape(buffer);

        var info = buffer.GetGlyphInfoSpan();
        var positions = buffer.GetGlyphPositionSpan();
        var glyphs = new uint[info.Length];
        var clusters = new uint[info.Length];
        var points = new SKPoint[info.Length];
        var x = 0f;
        var y = baseline;
        for (var index = 0; index < info.Length; index++)
        {
            glyphs[index] = info[index].Codepoint;
            clusters[index] = info[index].Cluster;
            points[index] = new SKPoint(
                x + positions[index].XOffset / (float)PositionScale,
                y - positions[index].YOffset / (float)PositionScale);
            x += positions[index].XAdvance / (float)PositionScale;
            y += positions[index].YAdvance / (float)PositionScale;
        }
        return new ManagedShapeResult(glyphs, clusters, points, x);
    }

    public void Dispose()
    {
        _font.Dispose();
        _functions.Dispose();
        _openTypeFont.Dispose();
    }

    private static int GetHorizontalAdvance(Font _, object fontData, uint glyph)
    {
        var state = (FontMetricState)fontData;
        Span<ushort> glyphs = stackalloc ushort[1];
        Span<float> widths = stackalloc float[1];
        glyphs[0] = checked((ushort)glyph);
        state.Font.GetGlyphWidths(glyphs, widths, Span<SKRect>.Empty);
        return ToHarfBuzzPosition(widths[0]);
    }

    private static int ToHarfBuzzPosition(float value)
        => checked((int)MathF.Round(value * PositionScale));

    private static Blob CreateCopiedBlob(byte[] data)
    {
        var pinned = GCHandle.Alloc(data, GCHandleType.Pinned);
        try
        {
            return new Blob(
                pinned.AddrOfPinnedObject(),
                data.Length,
                MemoryMode.Duplicate);
        }
        finally
        {
            pinned.Free();
        }
    }

    private static uint Tag(string value)
        => ((uint)value[0] << 24)
           | ((uint)value[1] << 16)
           | ((uint)value[2] << 8)
           | value[3];

    private sealed record FontMetricState(SKFont Font);
}

internal sealed record ManagedShapeResult(
    uint[] Codepoints,
    uint[] Clusters,
    SKPoint[] Points,
    float Width);
