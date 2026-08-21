using System.Runtime.InteropServices;
using SkiaSharp;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal readonly record struct NativeTextRunPositionRequest(
    string Text,
    string FamilyList,
    float FontSize,
    int FontWeight,
    SKFontStyleSlant Slant,
    uint FeatureFlags,
    uint[] ExpectedGlyphs,
    NativeTextShaping.WebTypefaceRegistry? WebTypefaces);

internal sealed record NativePositionedTextRun(
    ushort[] Glyphs,
    SKPoint[] Positions,
    float AdvanceWidth);

internal interface INativeTextRunPositioner
{
    bool IsEligible(in NativeTextRunPositionRequest request);

    bool TryPosition(
        in NativeTextRunPositionRequest request,
        out NativePositionedTextRun run);
}

internal sealed class DefaultNativeTextRunPositioner : INativeTextRunPositioner
{
    internal const string ModeEnvironmentVariable = "WEBSCENE_TEXT_POSITIONING";
    internal static readonly DefaultNativeTextRunPositioner Instance = new();

    private readonly bool _platformPositioningEnabled;
    private readonly MacCoreTextRunPositioner _macCoreText = new();

    private DefaultNativeTextRunPositioner()
    {
        var mode = Environment.GetEnvironmentVariable(ModeEnvironmentVariable);
        _platformPositioningEnabled = mode?.Trim().ToLowerInvariant()
            is not ("harfbuzz" or "legacy" or "off" or "0");
    }

    public bool TryPosition(
        in NativeTextRunPositionRequest request,
        out NativePositionedTextRun run)
    {
        if (IsEligible(in request)
            && _macCoreText.TryPosition(in request, out run))
        {
            return true;
        }

        // Returning false is the service contract for the existing
        // HarfBuzz/Skia measurement and painting path. Platform providers are
        // optional refinements; they must never prevent a text run rendering.
        run = null!;
        return false;
    }

    public bool IsEligible(in NativeTextRunPositionRequest request)
        => _platformPositioningEnabled && _macCoreText.IsEligible(in request);
}

internal sealed class MacCoreTextRunPositioner : INativeTextRunPositioner
{
    private const int MaximumCachedRuns = 2048;
    private const int MaximumCachedFonts = 256;
    private const int MaximumEligibleTextLength = 4096;
    private readonly object _cacheGate = new();
    private readonly Dictionary<RunKey, NativePositionedTextRun> _runs = [];
    private readonly Queue<RunKey> _runOrder = [];
    private readonly Dictionary<FontKey, IntPtr> _fonts = [];

    public bool TryPosition(
        in NativeTextRunPositionRequest request,
        out NativePositionedTextRun run)
    {
        run = null!;
        if (!IsEligible(in request))
        {
            return false;
        }

        var key = new RunKey(
            request.Text,
            request.FontSize,
            Math.Clamp(request.FontWeight, 1, 1000));
        lock (_cacheGate)
        {
            if (_runs.TryGetValue(key, out var cached))
            {
                if (GlyphsMatch(cached.Glyphs, request.ExpectedGlyphs))
                {
                    run = cached;
                    return true;
                }
                return false;
            }
        }

        NativePositionedTextRun positioned;
        try
        {
            positioned = Shape(key);
        }
        catch (Exception error) when (error is not OutOfMemoryException)
        {
            return false;
        }
        if (!GlyphsMatch(positioned.Glyphs, request.ExpectedGlyphs))
        {
            return false;
        }

        lock (_cacheGate)
        {
            if (_runs.TryGetValue(key, out var cached))
            {
                run = cached;
                return GlyphsMatch(cached.Glyphs, request.ExpectedGlyphs);
            }
            if (_runs.Count >= MaximumCachedRuns)
            {
                var oldest = _runOrder.Dequeue();
                _runs.Remove(oldest);
            }
            _runs.Add(key, positioned);
            _runOrder.Enqueue(key);
        }
        run = positioned;
        return true;
    }

    public bool IsEligible(in NativeTextRunPositionRequest request)
        => OperatingSystem.IsMacOS()
            && request.Slant == SKFontStyleSlant.Upright
            && (request.FeatureFlags & NativeTextShaping.TabularNumerals) == 0
            && request.Text.Length is > 0 and <= MaximumEligibleTextLength
            && NativeTextShaping.UsesMacSystemUiMetrics(
                request.FamilyList,
                request.WebTypefaces)
            && NativeTextShaping.UsesMacSystemUiPlatformAdvances(request.Text);

    private NativePositionedTextRun Shape(RunKey key)
    {
        using var fontLease = GetFont(new FontKey(key.FontSize, key.FontWeight));
        var font = fontLease.Handle;
        var text = CoreFoundation.CFStringCreateWithCharacters(
            IntPtr.Zero,
            key.Text,
            key.Text.Length);
        var attributes = text == IntPtr.Zero
            ? IntPtr.Zero
            : CoreFoundation.CFDictionaryCreate(
                IntPtr.Zero,
                [CoreText.FontAttribute],
                [font],
                1,
                IntPtr.Zero,
                IntPtr.Zero);
        var attributed = attributes == IntPtr.Zero
            ? IntPtr.Zero
            : CoreFoundation.CFAttributedStringCreate(
                IntPtr.Zero,
                text,
                attributes);
        var line = attributed == IntPtr.Zero
            ? IntPtr.Zero
            : CoreText.CTLineCreateWithAttributedString(attributed);
        try
        {
            if (line == IntPtr.Zero)
            {
                throw new InvalidOperationException("CoreText could not shape the text run.");
            }

            var glyphs = new List<ushort>();
            var positions = new List<SKPoint>();
            var runs = CoreText.CTLineGetGlyphRuns(line);
            var runCount = checked((int)CoreFoundation.CFArrayGetCount(runs));
            for (var runIndex = 0; runIndex < runCount; runIndex++)
            {
                var nativeRun = CoreFoundation.CFArrayGetValueAtIndex(runs, runIndex);
                var count = checked((int)CoreText.CTRunGetGlyphCount(nativeRun));
                var runGlyphs = new ushort[count];
                var runPositions = new CGPoint[count];
                CoreText.CTRunGetGlyphs(nativeRun, default, runGlyphs);
                CoreText.CTRunGetPositions(nativeRun, default, runPositions);
                for (var index = 0; index < count; index++)
                {
                    glyphs.Add(runGlyphs[index]);
                    positions.Add(new SKPoint(
                        checked((float)runPositions[index].X),
                        checked((float)runPositions[index].Y)));
                }
            }

            var width = checked((float)CoreText.CTLineGetTypographicBounds(
                line,
                out _,
                out _,
                out _));
            if (!float.IsFinite(width) || width < 0)
            {
                throw new InvalidOperationException("CoreText returned an invalid run width.");
            }
            return new NativePositionedTextRun(
                glyphs.ToArray(),
                positions.ToArray(),
                width);
        }
        finally
        {
            if (line != IntPtr.Zero) CoreFoundation.CFRelease(line);
            if (attributed != IntPtr.Zero) CoreFoundation.CFRelease(attributed);
            if (attributes != IntPtr.Zero) CoreFoundation.CFRelease(attributes);
            if (text != IntPtr.Zero) CoreFoundation.CFRelease(text);
        }
    }

    private FontLease GetFont(FontKey key)
    {
        lock (_cacheGate)
        {
            if (_fonts.TryGetValue(key, out var cached))
            {
                return new FontLease(cached, release: false);
            }
            var font = ObjectiveC.objc_msgSend_double_double(
                ObjectiveC.FontClass,
                ObjectiveC.SystemFontSelector,
                key.FontSize,
                SystemWeight(key.FontWeight));
            if (font == IntPtr.Zero)
            {
                throw new InvalidOperationException("Could not resolve the macOS system font.");
            }
            font = ObjectiveC.objc_retain(font);
            if (_fonts.Count < MaximumCachedFonts)
            {
                _fonts.Add(key, font);
                return new FontLease(font, release: false);
            }
            return new FontLease(font, release: true);
        }
    }

    private static bool GlyphsMatch(ushort[] actual, uint[] expected)
    {
        if (actual.Length != expected.Length) return false;
        for (var index = 0; index < actual.Length; index++)
        {
            if (actual[index] != expected[index]) return false;
        }
        return true;
    }

    private static double SystemWeight(int weight)
        => weight switch
        {
            <= 149 => -0.8,
            <= 249 => -0.6,
            <= 349 => -0.4,
            <= 449 => 0,
            <= 549 => 0.23,
            <= 649 => 0.3,
            <= 749 => 0.4,
            <= 849 => 0.56,
            _ => 0.62
        };

    private readonly record struct RunKey(string Text, float FontSize, int FontWeight);
    private readonly record struct FontKey(float FontSize, int FontWeight);

    private readonly struct FontLease(IntPtr handle, bool release) : IDisposable
    {
        internal IntPtr Handle { get; } = handle;

        public void Dispose()
        {
            if (release) ObjectiveC.objc_release(Handle);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct CGPoint(double X, double Y);

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct CFRange(nint Location, nint Length);

    private static class ObjectiveC
    {
        private const string Library = "/usr/lib/libobjc.A.dylib";
        internal static readonly IntPtr FontClass = objc_getClass("NSFont");
        internal static readonly IntPtr SystemFontSelector =
            sel_registerName("systemFontOfSize:weight:");

        [DllImport(Library)]
        private static extern IntPtr objc_getClass(string name);

        [DllImport(Library)]
        private static extern IntPtr sel_registerName(string name);

        [DllImport(Library, EntryPoint = "objc_msgSend")]
        internal static extern IntPtr objc_msgSend_double_double(
            IntPtr receiver,
            IntPtr selector,
            double value1,
            double value2);

        [DllImport(Library)]
        internal static extern IntPtr objc_retain(IntPtr value);

        [DllImport(Library)]
        internal static extern void objc_release(IntPtr value);
    }

    private static class CoreFoundation
    {
        private const string Library =
            "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation";
        [DllImport(Library)]
        internal static extern void CFRelease(IntPtr value);

        [DllImport(Library)]
        internal static extern IntPtr CFStringCreateWithCharacters(
            IntPtr allocator,
            [MarshalAs(UnmanagedType.LPWStr)] string characters,
            nint length);

        [DllImport(Library)]
        internal static extern IntPtr CFDictionaryCreate(
            IntPtr allocator,
            IntPtr[] keys,
            IntPtr[] values,
            nint count,
            IntPtr keyCallbacks,
            IntPtr valueCallbacks);

        [DllImport(Library)]
        internal static extern IntPtr CFAttributedStringCreate(
            IntPtr allocator,
            IntPtr value,
            IntPtr attributes);

        [DllImport(Library)]
        internal static extern nint CFArrayGetCount(IntPtr array);

        [DllImport(Library)]
        internal static extern IntPtr CFArrayGetValueAtIndex(IntPtr array, nint index);
    }

    private static class CoreText
    {
        private const string Library =
            "/System/Library/Frameworks/CoreText.framework/CoreText";
        internal static readonly IntPtr FontAttribute = ResolveAttribute("kCTFontAttributeName");

        [DllImport(Library)]
        internal static extern IntPtr CTLineCreateWithAttributedString(IntPtr attributedString);

        [DllImport(Library)]
        internal static extern IntPtr CTLineGetGlyphRuns(IntPtr line);

        [DllImport(Library)]
        internal static extern nint CTRunGetGlyphCount(IntPtr run);

        [DllImport(Library)]
        internal static extern void CTRunGetGlyphs(
            IntPtr run,
            CFRange range,
            [Out] ushort[] glyphs);

        [DllImport(Library)]
        internal static extern void CTRunGetPositions(
            IntPtr run,
            CFRange range,
            [Out] CGPoint[] positions);

        [DllImport(Library)]
        internal static extern double CTLineGetTypographicBounds(
            IntPtr line,
            out double ascent,
            out double descent,
            out double leading);

        private static IntPtr ResolveAttribute(string symbol)
        {
            var library = NativeLibrary.Load(Library);
            try
            {
                return Marshal.ReadIntPtr(NativeLibrary.GetExport(library, symbol));
            }
            finally
            {
                NativeLibrary.Free(library);
            }
        }
    }
}
