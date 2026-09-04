using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
#if !WEBSCENE_UNO
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
#endif
using WebScene.Core;
using WebScene.Css;
using WebScene.JavaScript.Interop;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

[StructLayout(LayoutKind.Sequential)]
public struct NativeTextMetrics
{
    public uint StructSize;
    public float AdvanceWidth;
    public float Ascent;
    public float Descent;
    public float Leading;
    public float ActualBoundingBoxLeft;
    public float ActualBoundingBoxRight;
    public float ActualBoundingBoxAscent;
    public float ActualBoundingBoxDescent;
}

public static class NativeTextShaping
{
    internal const string RasterizationModeEnvironmentVariable =
        "WEBSCENE_TEXT_RASTERIZATION";
    internal const uint TabularNumerals = 1u << 0;
    internal readonly record struct CanvasFontDescription(
        float Size,
        int Weight,
        SKFontStyleSlant Slant,
        string FamilyList);
    private static readonly ConcurrentDictionary<string, SKTypeface> Typefaces =
        new(StringComparer.Ordinal);
    private static readonly ConcurrentDictionary<string, SKTypeface> FallbackTypefaces =
        new(StringComparer.Ordinal);
    private static readonly object WebTypefaceCacheGate = new();
    private static readonly Dictionary<string, SharedWebTypeface> WebTypefaceCache =
        new(StringComparer.Ordinal);
    private static long _webTypefaceCacheHits;
    private static long _webTypefaceCacheMisses;
    private static readonly ConcurrentDictionary<string, SKTypeface> WebTypefaces =
        new(StringComparer.OrdinalIgnoreCase);
    private static readonly INativeTextRunPositioner TextRunPositioner =
        DefaultNativeTextRunPositioner.Instance;
    private static readonly string? ConfiguredRasterizationValue =
        Environment.GetEnvironmentVariable(RasterizationModeEnvironmentVariable);
    private static readonly NativeFontRasterizationMode ConfiguredRasterizationMode =
        ResolveConfiguredFontRasterizationMode(ConfiguredRasterizationValue);

    internal static NativeFontRasterizationMode ActiveFontRasterizationMode =>
        ConfiguredRasterizationMode;

    internal static NativeFontRasterizationMode? ResolveCssFontSmoothingRasterizationMode(
        string? value)
        => ResolveCssFontSmoothingRasterizationMode(value, OperatingSystem.IsMacOS());

    internal static NativeFontRasterizationMode? ResolveCssFontSmoothingRasterizationMode(
        string? value,
        bool isMacOS)
    {
        // Blink applies this non-standard property through its macOS
        // FontPlatformData path. Keep Windows and Linux on their native
        // rasterization profiles.
        if (!isMacOS) return null;
        // An explicit process profile is a diagnostic/host override. Otherwise
        // honor the inherited WebKit property per text run, as Chromium does.
        if (!string.IsNullOrWhiteSpace(ConfiguredRasterizationValue)) return null;
        return value?.Trim().ToLowerInvariant() switch
        {
            "antialiased" => NativeFontRasterizationMode.ChromiumAntialiased,
            "subpixel-antialiased" => NativeFontRasterizationMode.Chromium,
            _ => null
        };
    }

    internal sealed class WebTypefaceRegistry : IDisposable
    {
        private readonly object _gate = new();
        private readonly ConcurrentDictionary<string, FaceRegistration[]> _typefaces = new(StringComparer.OrdinalIgnoreCase);
        private readonly bool _instantiate = VariableFontInstancingEnabled;
        private int _references = 1;
        private bool _ownerDisposed;
        private volatile bool _disposed;
        internal WebTypefaceRegistry(bool? instantiate = null) => _instantiate = instantiate ?? VariableFontInstancingEnabled;

        internal bool Register(string family, ReadOnlySpan<byte> data, int? minimumWeight = null, int? maximumWeight = null, SKFontStyleSlant? slant = null)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(family);
            if (data.IsEmpty) return false;
            var normalizedFamily = family.Trim().Trim('"', '\'');
            lock (_gate)
            {
                ObjectDisposedException.ThrowIf(_ownerDisposed, this);
                var lease = AcquireWebTypeface(data);
                if (lease is null) return false;
                var min = Math.Clamp(minimumWeight ?? (int)(lease.Shared.Axis?.Minimum ?? lease.Typeface.FontWeight), 1, 1000);
                var max = Math.Clamp(maximumWeight ?? (int)(lease.Shared.Axis?.Maximum ?? lease.Typeface.FontWeight), min, 1000);
                var faceSlant = slant ?? lease.Typeface.FontSlant;
                _typefaces.TryGetValue(normalizedFamily, out var existing);
                existing ??= [];
                if (existing.Any(item => ReferenceEquals(item.Lease.Shared, lease.Shared) && item.Minimum == min && item.Maximum == max && item.Slant == faceSlant))
                {
                    lease.Dispose();
                    return true;
                }
                _typefaces[normalizedFamily] = [.. existing, new(lease, min, max, faceSlant)];
                return true;
            }
        }

        internal bool TryResolve(string family, out SKTypeface typeface) => TryResolve(family, 400, out typeface);
        internal bool TryResolve(string family, int weight, out SKTypeface typeface)
            => TryResolve(family, weight, SKFontStyleSlant.Upright, out typeface);
        internal bool TryResolve(string family, int weight, SKFontStyleSlant slant, out SKTypeface typeface)
        {
            if (!_disposed && _typefaces.TryGetValue(family, out var faces))
            {
                weight = Math.Clamp(weight, 1, 1000);
                var selected = faces[0];
                var best = int.MaxValue;
                foreach (var face in faces)
                {
                    var candidate = Math.Clamp(weight, face.Minimum, face.Maximum);
                    var rank = WeightRank(weight, candidate) + (face.Slant == slant ? 0 : 10000);
                    // Later declarations win ties, as for overlapping @font-face rules.
                    if (rank <= best) { selected = face; best = rank; }
                }
                typeface = _instantiate
                    ? selected.Lease.Shared.Resolve(Math.Clamp(weight, selected.Minimum, selected.Maximum))
                    : selected.Lease.Typeface;
                return true;
            }
            typeface = null!;
            return false;
        }

        internal static int WeightRank(int requested, int candidate)
        {
            if (requested == candidate) return 0;
            if (requested is >= 400 and <= 500)
                return candidate >= requested && candidate <= 500 ? candidate - requested
                    : candidate < requested ? 1000 + requested - candidate : 2000 + candidate - 500;
            return requested < 400
                ? candidate < requested ? requested - candidate : 1000 + candidate - requested
                : candidate > requested ? candidate - requested : 1000 + requested - candidate;
        }

        internal bool Contains(string family) => !_disposed && _typefaces.ContainsKey(family);
        internal IDisposable Retain()
        {
            lock (_gate)
            {
                ObjectDisposedException.ThrowIf(_disposed, this);
                _references++;
                return new RegistryReference(this);
            }
        }
        private void Release()
        {
            lock (_gate)
            {
                if (--_references != 0) return;
                _disposed = true;
                foreach (var faces in _typefaces.Values)
                    foreach (var face in faces) face.Lease.Dispose();
                _typefaces.Clear();
            }
        }
        public void Dispose()
        {
            lock (_gate)
            {
                if (_ownerDisposed) return;
                _ownerDisposed = true;
                Release();
            }
        }
        private sealed record FaceRegistration(WebTypefaceLease Lease, int Minimum, int Maximum, SKFontStyleSlant Slant);
        private sealed class RegistryReference(WebTypefaceRegistry registry) : IDisposable
        {
            private WebTypefaceRegistry? _registry = registry;
            public void Dispose() => Interlocked.Exchange(ref _registry, null)?.Release();
        }
    }

    public readonly record struct WebTypefaceCacheMetrics(
        int Entries,
        int References,
        long Hits,
        long Misses);

    internal static WebTypefaceRegistry CreateWebTypefaceRegistry(bool? instantiate = null) => new(instantiate);

    public static WebTypefaceCacheMetrics GetWebTypefaceCacheMetrics()
    {
        lock (WebTypefaceCacheGate)
        {
            return new WebTypefaceCacheMetrics(
                WebTypefaceCache.Count,
                WebTypefaceCache.Values.Sum(static entry => entry.ReferenceCount),
                Volatile.Read(ref _webTypefaceCacheHits),
                Volatile.Read(ref _webTypefaceCacheMisses));
        }
    }

    public static bool RegisterWebTypeface(string family, ReadOnlySpan<byte> data)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(family);
        if (data.IsEmpty) return false;

        using var fontData = SKData.CreateCopy(data);
        var typeface = SKTypeface.FromData(fontData);
        if (typeface is null) return false;

        var normalizedFamily = family.Trim().Trim('"', '\'');
        if (WebTypefaces.TryAdd(normalizedFamily, typeface)) return true;

        typeface.Dispose();
        return true;
    }

    public static SKTypeface ResolveTypeface(string familyList, int fontWeight)
        => ResolveTypeface(
            familyList,
            fontWeight,
            SKFontStyleSlant.Upright,
            null);

    internal static SKTypeface ResolveTypeface(
        string familyList,
        int fontWeight,
        WebTypefaceRegistry? registry)
        => ResolveTypeface(
            familyList,
            fontWeight,
            SKFontStyleSlant.Upright,
            registry);

    internal static SKTypeface ResolveTypeface(
        string familyList,
        int fontWeight,
        SKFontStyleSlant slant,
        WebTypefaceRegistry? registry)
    {
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (registry?.TryResolve(family, fontWeight, slant, out var scopedTypeface) == true)
            {
                return scopedTypeface;
            }
            if (WebTypefaces.TryGetValue(family, out var webTypeface))
            {
                return webTypeface;
            }
        }

        var requestedWeight = Math.Clamp(fontWeight, 1, 1000);
        var key = $"{familyList}\u001f{requestedWeight}\u001f{(int)slant}";
        return Typefaces.GetOrAdd(key, _ =>
        {
            foreach (var rawFamily in familyList.Split(
                         ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
            {
                var family = rawFamily.Trim('"', '\'');
                var genericFamily = family.ToLowerInvariant();
                if (genericFamily is "-apple-system" or "blinkmacsystemfont")
                {
                    if (OperatingSystem.IsMacOS()) family = ".AppleSystemUIFont";
                    else continue;
                }
                else if (genericFamily == "system-ui")
                    family = OperatingSystem.IsMacOS()
                        ? ".AppleSystemUIFont"
                        : OperatingSystem.IsWindows() ? "Segoe UI" : "sans-serif";
                else if (genericFamily == "sans-serif")
                    family = OperatingSystem.IsMacOS() ? "Helvetica" : "Arial";
                else if (genericFamily == "serif") family = "Times New Roman";
                else if (genericFamily == "monospace") family = OperatingSystem.IsMacOS() ? "Menlo" : "Consolas";

                var candidate = SKTypeface.FromFamilyName(
                    family,
                    requestedWeight,
                    (int)SKFontStyleWidth.Normal,
                    slant);
                if (candidate is not null
                    && (string.Equals(candidate.FamilyName, family, StringComparison.OrdinalIgnoreCase)
                        || genericFamily is "-apple-system" or "blinkmacsystemfont" or "system-ui"
                            or "sans-serif" or "serif" or "monospace"))
                {
                    return candidate;
                }
                candidate?.Dispose();
            }
            return SKTypeface.Default;
        });
    }

    internal readonly record struct FallbackTextRun(
        string Text,
        SKTypeface Typeface);

    internal readonly record struct FallbackLayoutRun(
        string Text,
        SKTypeface Typeface,
        float AdvanceWidth,
        float WidthScale);

    internal sealed record FallbackTextLayout(
        FallbackLayoutRun[] Runs,
        float AdvanceWidth,
        float Ascent,
        float Descent,
        float Leading,
        SKRect InkBounds);

    internal static bool TryResolveFallbackTextRuns(
        string text,
        string familyList,
        int fontWeight,
        SKFontStyleSlant slant,
        WebTypefaceRegistry? registry,
        out FallbackTextRun[] runs)
    {
        runs = [];
        if (string.IsNullOrEmpty(text)) return false;
        var primary = ResolveTypeface(familyList, fontWeight, slant, registry);
        if (primary.ContainsGlyphs(text)) return false;

        var starts = StringInfo.ParseCombiningCharacters(text);
        var resolved = new List<FallbackTextRun>(starts.Length);
        var builder = new StringBuilder();
        SKTypeface? activeTypeface = null;
        for (var index = 0; index < starts.Length; index++)
        {
            var start = starts[index];
            var end = index + 1 < starts.Length ? starts[index + 1] : text.Length;
            var element = text[start..end];
            var typeface = primary.ContainsGlyphs(element)
                ? primary
                : ResolveFallbackTypeface(
                    element,
                    familyList,
                    fontWeight,
                    slant);
            if (!ReferenceEquals(activeTypeface, typeface) && builder.Length > 0)
            {
                resolved.Add(new FallbackTextRun(builder.ToString(), activeTypeface!));
                builder.Clear();
            }
            activeTypeface = typeface;
            builder.Append(element);
        }
        if (builder.Length > 0)
        {
            resolved.Add(new FallbackTextRun(builder.ToString(), activeTypeface!));
        }
        if (resolved.Count == 1 && ReferenceEquals(resolved[0].Typeface, primary))
        {
            return false;
        }
        runs = resolved.ToArray();
        return true;
    }

    private static SKTypeface ResolveFallbackTypeface(
        string textElement,
        string familyList,
        int fontWeight,
        SKFontStyleSlant slant)
    {
        var codepoint = textElement.EnumerateRunes()
            .FirstOrDefault(static rune => rune.Value is not 0x200D
                and not 0xFE0E and not 0xFE0F).Value;
        if (codepoint == 0) return SKTypeface.Default;
        var key = $"{familyList}\u001f{fontWeight}\u001f{(int)slant}\u001f{codepoint:X}";
        return FallbackTypefaces.GetOrAdd(key, _ =>
        {
            if (OperatingSystem.IsMacOS() && IsEmojiCodepoint(codepoint))
            {
                var emoji = SKTypeface.FromFamilyName("Apple Color Emoji");
                if (emoji is not null && emoji.ContainsGlyph(codepoint)) return emoji;
                emoji?.Dispose();
            }
            var family = familyList.Split(',', StringSplitOptions.TrimEntries
                    | StringSplitOptions.RemoveEmptyEntries)
                .Select(static value => value.Trim('"', '\''))
                .FirstOrDefault();
            var matched = SKFontManager.Default.MatchCharacter(
                family,
                Math.Clamp(fontWeight, 1, 1000),
                (int)SKFontStyleWidth.Normal,
                slant,
                [CultureInfo.CurrentUICulture.TwoLetterISOLanguageName],
                codepoint);
            return matched ?? SKTypeface.Default;
        });
    }

    private static bool IsEmojiCodepoint(int value)
        => value is >= 0x1F000 and <= 0x1FAFF
            or >= 0x2600 and <= 0x27BF
            or >= 0x2300 and <= 0x23FF;

    internal static FallbackTextLayout LayoutFallbackTextRuns(
        FallbackTextRun[] runs,
        string familyList,
        float fontSize,
        int fontWeight,
        uint featureFlags,
        SKPaint paint,
        WebTypefaceRegistry? registry)
    {
        var layoutRuns = new FallbackLayoutRun[runs.Length];
        var advance = 0f;
        var ascent = 0f;
        var descent = 0f;
        var leading = 0f;
        var inkBounds = SKRect.Empty;
        var hasInk = false;
        for (var index = 0; index < runs.Length; index++)
        {
            var run = runs[index];
            paint.Typeface = run.Typeface;
            using var shaper = new SKShaper(run.Typeface);
            var runFeatures = ResolveFeatureFlags(
                run.Text,
                familyList,
                featureFlags,
                registry);
            var shapedWidth = MeasureShapedWidth(shaper, run.Text, paint, runFeatures);
            var widthScale = ResolveShapedWidthScale(
                run.Text,
                familyList,
                fontSize,
                fontWeight,
                paint,
                shapedWidth,
                runFeatures,
                registry);
            var runInk = MeasureShapedInkBounds(
                shaper,
                run.Text,
                paint,
                runFeatures,
                horizontalAdvanceScale: widthScale);
            if (!runInk.IsEmpty)
            {
                runInk.Offset(advance, 0);
                if (hasInk) inkBounds.Union(runInk);
                else
                {
                    inkBounds = runInk;
                    hasInk = true;
                }
            }
            paint.GetFontMetrics(out var metrics);
            ascent = Math.Max(ascent, -metrics.Ascent);
            descent = Math.Max(descent, metrics.Descent);
            leading = Math.Max(leading, metrics.Leading);
            var renderedAdvance = shapedWidth * widthScale;
            layoutRuns[index] = new FallbackLayoutRun(
                run.Text,
                run.Typeface,
                renderedAdvance,
                widthScale);
            advance += renderedAdvance;
        }
        return new FallbackTextLayout(
            layoutRuns,
            advance,
            ascent,
            descent,
            leading,
            inkBounds);
    }

    internal static bool TryParseCanvasFont(
        string shorthand,
        out CanvasFontDescription font)
    {
        font = new CanvasFontDescription(
            10,
            400,
            SKFontStyleSlant.Upright,
            "sans-serif");
        if (string.IsNullOrWhiteSpace(shorthand)) return false;

        var px = shorthand.IndexOf("px", StringComparison.OrdinalIgnoreCase);
        if (px <= 0) return false;
        var sizeStart = px - 1;
        while (sizeStart >= 0
               && (char.IsDigit(shorthand[sizeStart])
                   || shorthand[sizeStart] is '.' or '-' or '+'))
        {
            sizeStart--;
        }
        sizeStart++;
        if (!float.TryParse(
                shorthand.AsSpan(sizeStart, px - sizeStart),
                NumberStyles.Float,
                CultureInfo.InvariantCulture,
                out var size)
            || !float.IsFinite(size)
            || size <= 0)
        {
            return false;
        }

        var weight = 400;
        var slant = SKFontStyleSlant.Upright;
        foreach (var token in shorthand[..sizeStart].Split(
                     (char[]?)null,
                     StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (token.Equals("bold", StringComparison.OrdinalIgnoreCase)
                || token.Equals("bolder", StringComparison.OrdinalIgnoreCase))
            {
                weight = 700;
            }
            else if (token.Equals("lighter", StringComparison.OrdinalIgnoreCase))
            {
                weight = 300;
            }
            else if (token.Equals("italic", StringComparison.OrdinalIgnoreCase))
            {
                slant = SKFontStyleSlant.Italic;
            }
            else if (token.Equals("oblique", StringComparison.OrdinalIgnoreCase))
            {
                slant = SKFontStyleSlant.Oblique;
            }
            else if (int.TryParse(
                         token,
                         NumberStyles.Integer,
                         CultureInfo.InvariantCulture,
                         out var numericWeight)
                     && numericWeight is >= 1 and <= 1000)
            {
                weight = numericWeight;
            }
        }

        var familyStart = px + 2;
        while (familyStart < shorthand.Length && char.IsWhiteSpace(shorthand[familyStart]))
        {
            familyStart++;
        }
        if (familyStart < shorthand.Length && shorthand[familyStart] == '/')
        {
            familyStart++;
            while (familyStart < shorthand.Length && char.IsWhiteSpace(shorthand[familyStart]))
            {
                familyStart++;
            }
            while (familyStart < shorthand.Length && !char.IsWhiteSpace(shorthand[familyStart]))
            {
                familyStart++;
            }
            while (familyStart < shorthand.Length && char.IsWhiteSpace(shorthand[familyStart]))
            {
                familyStart++;
            }
        }
        if (familyStart >= shorthand.Length) return false;
        var familyList = shorthand[familyStart..].Trim();
        if (familyList.Length == 0) return false;

        font = new CanvasFontDescription(size, weight, slant, familyList);
        return true;
    }

    internal static float ResolveWidthScale(
        string familyList,
        float fontSize,
        int fontWeight,
        WebTypefaceRegistry? registry = null)
    {
        if (!OperatingSystem.IsMacOS()
            || !UsesMacSystemUiMetrics(familyList, registry))
        {
            return 1f;
        }

        // Keep the native Skia compositor in lockstep with the managed
        // Avalonia DOM/Canvas calibration for Blink's macOS system UI face.
        var size = Math.Clamp(fontSize, 8f, 24f);
        var weight = Math.Clamp(fontWeight, 100, 900);
        return Math.Clamp(
            1.0222f + (16f - size) * 0.0062f - (weight - 400f) * 0.000133f,
            0.96f,
            1.08f);
    }

    internal static float ResolveShapedWidthScale(
        string text,
        string familyList,
        float fontSize,
        int fontWeight,
        SKPaint paint,
        float shapedWidth,
        uint featureFlags,
        WebTypefaceRegistry? registry = null)
    {
        if ((featureFlags & TabularNumerals) != 0)
        {
            return ResolveWidthScale(
                familyList,
                fontSize,
                fontWeight,
                registry);
        }
        if (!OperatingSystem.IsMacOS()
            || !UsesMacSystemUiMetrics(familyList, registry)
            || shapedWidth <= 0
            || !UsesMacSystemUiPlatformAdvances(text))
        {
            return 1f;
        }

        // SkiaSharp 2.88's HarfBuzz bridge under-reports the hidden macOS
        // system face and does not reflect its selected weight in advances.
        // CoreText-backed Skia advances retain those axes. Keep HarfBuzz's
        // glyph selection and kerning, but scale the run to the platform
        // advance used by Blink's -apple-system face.
        var platformWidth = paint.MeasureText(text);
        if (!float.IsFinite(platformWidth) || platformWidth <= 0) return 1f;
        var size = Math.Clamp(fontSize, 8f, 24f);
        var weight = Math.Clamp(fontWeight, 100, 900);
        var platformCalibration = Math.Clamp(
            0.975f - (size - 14f) * 0.006f + (weight - 400f) * 0.00005f,
            0.93f,
            1.02f);
        // A standalone collapsed space is a real inline advance. The hidden
        // system face exposed to HarfBuzz under-reports it by roughly one
        // third on macOS, so the upper bound must admit the platform value.
        return Math.Clamp(platformWidth * platformCalibration / shapedWidth, 0.8f, 1.4f);
    }

    internal static bool UsesMacSystemUiPlatformAdvances(string text)
    {
        var sawCompatibleRune = false;
        foreach (var rune in text.EnumerateRunes())
        {
            var category = Rune.GetUnicodeCategory(rune);
            // U+2318 is rendered by the system UI face as a keyboard shortcut
            // glyph. Keep the surrounding run on browser-compatible platform
            // advances without admitting arbitrary OtherSymbol glyphs (which
            // may require a fallback or emoji typeface).
            if (rune.Value == 0x2318)
            {
                sawCompatibleRune = true;
                continue;
            }

            if (IsLatinLetter(rune, category))
            {
                sawCompatibleRune = true;
                continue;
            }

            // Punctuation is commonly split into a separate inline text run.
            // In particular, a typographic apostrophe must not switch an
            // otherwise Latin run back to HarfBuzz's narrower hidden-system-
            // font advances. Doing so makes adjacent fragments use visibly
            // different tracking and also makes bold fragments look heavier.
            if (category is UnicodeCategory.DecimalDigitNumber
                or UnicodeCategory.LetterNumber
                or UnicodeCategory.OtherNumber
                or UnicodeCategory.NonSpacingMark
                or UnicodeCategory.SpacingCombiningMark
                or UnicodeCategory.SpaceSeparator
                or UnicodeCategory.LineSeparator
                or UnicodeCategory.ParagraphSeparator
                or UnicodeCategory.ConnectorPunctuation
                or UnicodeCategory.DashPunctuation
                or UnicodeCategory.OpenPunctuation
                or UnicodeCategory.ClosePunctuation
                or UnicodeCategory.InitialQuotePunctuation
                or UnicodeCategory.FinalQuotePunctuation
                or UnicodeCategory.OtherPunctuation
                or UnicodeCategory.MathSymbol
                or UnicodeCategory.CurrencySymbol)
            {
                sawCompatibleRune = true;
                continue;
            }

            return false;
        }
        return sawCompatibleRune;
    }

    private static bool IsLatinLetter(Rune rune, UnicodeCategory category)
    {
        if (category is not (UnicodeCategory.UppercaseLetter
            or UnicodeCategory.LowercaseLetter
            or UnicodeCategory.TitlecaseLetter
            or UnicodeCategory.ModifierLetter
            or UnicodeCategory.OtherLetter))
        {
            return false;
        }
        var value = rune.Value;
        return value is (>= 'A' and <= 'Z') or (>= 'a' and <= 'z')
            or (>= 0x00C0 and <= 0x024F) // Latin-1 and Latin Extended A/B.
            or (>= 0x1E00 and <= 0x1EFF) // Latin Extended Additional.
            or (>= 0x2C60 and <= 0x2C7F) // Latin Extended C.
            or (>= 0xA720 and <= 0xA7FF) // Latin Extended D.
            or (>= 0xAB30 and <= 0xAB6F); // Latin Extended E.
    }

    internal static float MeasureShapedWidth(
        SKShaper shaper,
        string text,
        SKPaint paint,
        uint featureFlags,
        float tabularDigitScale = 1f)
    {
        if (string.IsNullOrEmpty(text))
        {
            return 0;
        }
        if ((featureFlags & TabularNumerals) == 0)
        {
            return shaper.Shape(text, paint).Width;
        }

        var tabularDigitWidth = shaper.Shape("0", paint).Width * tabularDigitScale;
        var width = 0f;
        for (var index = 0; index < text.Length;)
        {
            if (text[index] is >= '0' and <= '9')
            {
                width += tabularDigitWidth;
                index++;
                continue;
            }
            var start = index++;
            while (index < text.Length && text[index] is not (>= '0' and <= '9')) index++;
            width += shaper.Shape(text[start..index], paint).Width;
        }
        return width;
    }

    internal static bool TryPositionTextRun(
        SKShaper shaper,
        string text,
        string familyList,
        float fontSize,
        int fontWeight,
        SKFontStyleSlant slant,
        uint featureFlags,
        SKPaint paint,
        WebTypefaceRegistry? registry,
        out NativePositionedTextRun run)
    {
        run = null!;
        if (string.IsNullOrEmpty(text)) return false;
        var request = new NativeTextRunPositionRequest(
            text,
            familyList,
            fontSize,
            fontWeight,
            slant,
            featureFlags,
            [],
            registry,
            shaper.Typeface);
        if (!TextRunPositioner.IsEligible(in request)) return false;
        var shaped = shaper.Shape(text, 0, 0, paint);
        if (shaped.Codepoints.Length == 0
            || shaped.Codepoints.Length != shaped.Points.Length)
        {
            return false;
        }
        request = request with { ExpectedGlyphs = shaped.Codepoints };
        return TextRunPositioner.TryPosition(in request, out run);
    }

    internal static SKRect MeasureShapedInkBounds(
        SKShaper shaper,
        string text,
        SKPaint paint,
        uint featureFlags,
        float tabularDigitScale = 1f,
        float horizontalAdvanceScale = 1f)
    {
        if (string.IsNullOrEmpty(text)) return SKRect.Empty;
        if ((featureFlags & TabularNumerals) == 0)
        {
            return MeasureShapedTextRunBounds(
                shaper,
                text,
                0,
                paint,
                horizontalAdvanceScale);
        }

        var result = SKRect.Empty;
        var hasBounds = false;
        var tabularDigitWidth = shaper.Shape("0", paint).Width * tabularDigitScale;
        var cursor = 0f;
        for (var index = 0; index < text.Length;)
        {
            string segment;
            float advance;
            if (text[index] is >= '0' and <= '9')
            {
                segment = text[index].ToString();
                advance = tabularDigitWidth;
                index++;
            }
            else
            {
                var start = index++;
                while (index < text.Length && text[index] is not (>= '0' and <= '9')) index++;
                segment = text[start..index];
                advance = shaper.Shape(segment, paint).Width;
            }

            var bounds = MeasureShapedTextRunBounds(
                shaper,
                segment,
                cursor,
                paint,
                horizontalAdvanceScale);
            if (!bounds.IsEmpty)
            {
                if (hasBounds) result.Union(bounds);
                else
                {
                    result = bounds;
                    hasBounds = true;
                }
            }
            cursor += advance * horizontalAdvanceScale;
        }
        return result;
    }

    internal static SKRect MeasurePositionedInkBounds(
        NativePositionedTextRun positionedRun,
        SKPaint paint,
        float horizontalAdvanceScale = 1f)
    {
        if (positionedRun.Glyphs.Length == 0) return SKRect.Empty;
        using var font = paint.ToFont();
        using var builder = new SKTextBlobBuilder();
        var run = builder.AllocatePositionedRun(font, positionedRun.Glyphs.Length);
        positionedRun.Glyphs.AsSpan().CopyTo(run.GetGlyphSpan());
        var positions = run.GetPositionSpan();
        for (var index = 0; index < positionedRun.Positions.Length; index++)
        {
            positions[index] = new SKPoint(
                positionedRun.Positions[index].X * horizontalAdvanceScale,
                positionedRun.Positions[index].Y);
        }
        using var blob = builder.Build();
        return blob?.Bounds ?? SKRect.Empty;
    }

    private static SKRect MeasureShapedTextRunBounds(
        SKShaper shaper,
        string text,
        float x,
        SKPaint paint,
        float horizontalAdvanceScale)
    {
        var shaped = shaper.Shape(text, 0, 0, paint);
        if (shaped.Codepoints.Length == 0 || shaped.Points.Length == 0)
        {
            return SKRect.Empty;
        }

        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        using var builder = new SKTextBlobBuilder();
        var glyphCount = Math.Min(shaped.Codepoints.Length, shaped.Points.Length);
        var run = builder.AllocatePositionedRun(font, glyphCount);
        var glyphs = run.GetGlyphSpan();
        var positions = run.GetPositionSpan();
        for (var index = 0; index < glyphCount; index++)
        {
            glyphs[index] = (ushort)shaped.Codepoints[index];
            positions[index] = new SKPoint(
                x + shaped.Points[index].X * horizontalAdvanceScale,
                shaped.Points[index].Y);
        }
        using var blob = builder.Build();
        return blob?.Bounds ?? SKRect.Empty;
    }

    internal static void DrawShapedText(
        SKCanvas canvas,
        SKShaper shaper,
        string text,
        float x,
        float baseline,
        SKPaint paint,
        uint featureFlags,
        float tabularDigitScale = 1f,
        float horizontalAdvanceScale = 1f,
        float measuredWidth = float.NaN,
        float deviceScaleFactor = 1f,
        NativePositionedTextRun? positionedRun = null,
        NativeFontRasterizationMode? rasterizationMode = null,
        float letterSpacing = 0,
        float wordSpacing = 0)
    {
        if (string.IsNullOrEmpty(text))
        {
            return;
        }
        var unscaledWidth = float.IsFinite(measuredWidth)
            ? measuredWidth
            : MeasureShapedWidth(
                shaper,
                text,
                paint,
                featureFlags,
                tabularDigitScale);
        var cursor = paint.TextAlign switch
        {
            SKTextAlign.Center => x - unscaledWidth * horizontalAdvanceScale * .5f,
            SKTextAlign.Right => x - unscaledWidth * horizontalAdvanceScale,
            _ => x
        };
        if (positionedRun is not null)
        {
            DrawPositionedTextRun(
                canvas,
                shaper,
                positionedRun,
                cursor,
                baseline,
                paint,
                horizontalAdvanceScale,
                deviceScaleFactor,
                rasterizationMode,
                text,
                letterSpacing,
                wordSpacing);
            return;
        }
        if ((featureFlags & TabularNumerals) == 0)
        {
            DrawShapedTextRun(
                canvas,
                shaper,
                text,
                cursor,
                baseline,
                paint,
                horizontalAdvanceScale,
                deviceScaleFactor,
                rasterizationMode,
                letterSpacing,
                wordSpacing);
            return;
        }

        var tabularDigitWidth = shaper.Shape("0", paint).Width * tabularDigitScale;
        for (var index = 0; index < text.Length;)
        {
            if (text[index] is >= '0' and <= '9')
            {
                var digit = text[index].ToString();
                DrawShapedTextRun(
                    canvas,
                    shaper,
                    digit,
                    cursor,
                    baseline,
                    paint,
                    horizontalAdvanceScale,
                    deviceScaleFactor,
                    rasterizationMode);
                cursor += tabularDigitWidth * horizontalAdvanceScale;
                index++;
                continue;
            }
            var start = index++;
            while (index < text.Length && text[index] is not (>= '0' and <= '9')) index++;
            var segment = text[start..index];
            DrawShapedTextRun(
                canvas,
                shaper,
                segment,
                cursor,
                baseline,
                paint,
                horizontalAdvanceScale,
                deviceScaleFactor,
                rasterizationMode);
            cursor += shaper.Shape(segment, paint).Width * horizontalAdvanceScale;
        }
    }

    internal static float DrawCssSpacedText(
        SKCanvas canvas,
        string text,
        float x,
        float baseline,
        string familyList,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        uint featureFlags,
        SKPaint paint,
        WebTypefaceRegistry? registry,
        float deviceScaleFactor,
        NativeFontRasterizationMode? rasterizationMode)
    {
        if (string.IsNullOrEmpty(text)) return 0;

        var primaryTypeface = ResolveTypeface(familyList, fontWeight, registry);
        paint.Typeface = primaryTypeface;
        var hasFallback = TryResolveFallbackTextRuns(
            text,
            familyList,
            fontWeight,
            SKFontStyleSlant.Upright,
            registry,
            out _);
        if (!hasFallback)
        {
            using var primaryShaper = new SKShaper(primaryTypeface);
            var resolvedFeatures = ResolveFeatureFlags(
                text,
                familyList,
                featureFlags,
                registry);
            var positioned = TryPositionTextRun(
                primaryShaper,
                text,
                familyList,
                fontSize,
                fontWeight,
                SKFontStyleSlant.Upright,
                resolvedFeatures,
                paint,
                registry,
                out var positionedRun);
            var tabularDigitScale = ResolveTabularDigitScale(familyList, registry);
            var shapedWidth = positioned
                ? positionedRun.AdvanceWidth
                : MeasureShapedWidth(
                    primaryShaper,
                    text,
                    paint,
                    resolvedFeatures,
                    tabularDigitScale);
            var widthScale = positioned
                ? 1f
                : ResolveShapedWidthScale(
                    text,
                    familyList,
                    fontSize,
                    fontWeight,
                    paint,
                    shapedWidth,
                    resolvedFeatures,
                    registry);
            DrawShapedText(
                canvas,
                primaryShaper,
                text,
                x,
                baseline,
                paint,
                resolvedFeatures,
                tabularDigitScale,
                widthScale,
                shapedWidth,
                deviceScaleFactor,
                positioned ? positionedRun : null,
                rasterizationMode,
                letterSpacing,
                wordSpacing);
            return shapedWidth * widthScale + ResolveCssAdvanceSpacing(
                text,
                letterSpacing,
                wordSpacing);
        }

        var elementStarts = StringInfo.ParseCombiningCharacters(text);
        var cursor = x;
        for (var elementIndex = 0; elementIndex < elementStarts.Length; elementIndex++)
        {
            var start = elementStarts[elementIndex];
            var end = elementIndex + 1 < elementStarts.Length
                ? elementStarts[elementIndex + 1]
                : text.Length;
            var element = text[start..end];
            var elementAdvance = 0f;
            if (TryResolveFallbackTextRuns(
                    element,
                    familyList,
                    fontWeight,
                    SKFontStyleSlant.Upright,
                    registry,
                    out var fallbackRuns))
            {
                var layout = LayoutFallbackTextRuns(
                    fallbackRuns,
                    familyList,
                    fontSize,
                    fontWeight,
                    featureFlags,
                    paint,
                    registry);
                foreach (var run in layout.Runs)
                {
                    paint.Typeface = run.Typeface;
                    using var fallbackShaper = new SKShaper(run.Typeface);
                    var runFeatures = ResolveFeatureFlags(
                        run.Text,
                        familyList,
                        featureFlags,
                        registry);
                    var unscaledWidth = run.WidthScale > 0
                        ? run.AdvanceWidth / run.WidthScale
                        : run.AdvanceWidth;
                    DrawShapedText(
                        canvas,
                        fallbackShaper,
                        run.Text,
                        cursor + elementAdvance,
                        baseline,
                        paint,
                        runFeatures,
                        1f,
                        run.WidthScale,
                        unscaledWidth,
                        deviceScaleFactor,
                        positionedRun: null,
                        rasterizationMode);
                    elementAdvance += run.AdvanceWidth;
                }
            }
            else
            {
                var typeface = ResolveTypeface(familyList, fontWeight, registry);
                paint.Typeface = typeface;
                using var shaper = new SKShaper(typeface);
                var elementFeatures = ResolveFeatureFlags(
                    element,
                    familyList,
                    featureFlags,
                    registry);
                var positioned = TryPositionTextRun(
                    shaper,
                    element,
                    familyList,
                    fontSize,
                    fontWeight,
                    SKFontStyleSlant.Upright,
                    elementFeatures,
                    paint,
                    registry,
                    out var positionedRun);
                var tabularDigitScale = ResolveTabularDigitScale(familyList, registry);
                var shapedWidth = positioned
                    ? positionedRun.AdvanceWidth
                    : MeasureShapedWidth(
                        shaper,
                        element,
                        paint,
                        elementFeatures,
                        tabularDigitScale);
                var widthScale = positioned
                    ? 1f
                    : ResolveShapedWidthScale(
                        element,
                        familyList,
                        fontSize,
                        fontWeight,
                        paint,
                        shapedWidth,
                        elementFeatures,
                        registry);
                DrawShapedText(
                    canvas,
                    shaper,
                    element,
                    cursor,
                    baseline,
                    paint,
                    elementFeatures,
                    tabularDigitScale,
                    widthScale,
                    shapedWidth,
                    deviceScaleFactor,
                    positioned ? positionedRun : null,
                    rasterizationMode);
                elementAdvance = shapedWidth * widthScale;
            }

            cursor += elementAdvance;
            if (elementIndex + 1 < elementStarts.Length) cursor += letterSpacing;
            if (element.Contains(' ')) cursor += wordSpacing;
        }
        return cursor - x;
    }

    private static float ResolveCssAdvanceSpacing(
        string text,
        float letterSpacing,
        float wordSpacing)
    {
        var elementCount = StringInfo.ParseCombiningCharacters(text).Length;
        var spaceCount = 0;
        foreach (var character in text)
        {
            if (character == ' ') spaceCount++;
        }
        return Math.Max(0, elementCount - 1) * letterSpacing
            + spaceCount * wordSpacing;
    }

    private static void DrawPositionedTextRun(
        SKCanvas canvas,
        SKShaper shaper,
        NativePositionedTextRun positionedRun,
        float x,
        float baseline,
        SKPaint paint,
        float horizontalAdvanceScale,
        float deviceScaleFactor,
        NativeFontRasterizationMode? rasterizationMode,
        string text,
        float letterSpacing,
        float wordSpacing)
    {
        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        ApplyFontRasterizationProfile(
            font,
            deviceScaleFactor,
            ResolvePositionedRunRasterizationMode(
                positionedRun,
                deviceScaleFactor,
                rasterizationMode));
        using var builder = new SKTextBlobBuilder();
        var run = builder.AllocatePositionedRun(font, positionedRun.Glyphs.Length);
        positionedRun.Glyphs.AsSpan().CopyTo(run.GetGlyphSpan());
        var positions = run.GetPositionSpan();
        var spacingOffsets = ResolveCssGlyphSpacingOffsets(
            text,
            positionedRun.Glyphs.Length,
            positionedRun.Clusters,
            letterSpacing,
            wordSpacing);
        for (var index = 0; index < positionedRun.Positions.Length; index++)
        {
            positions[index] = new SKPoint(
                x + positionedRun.Positions[index].X * horizontalAdvanceScale
                    + spacingOffsets[index],
                baseline + positionedRun.Positions[index].Y);
        }
        using var textBlob = builder.Build();
        if (textBlob is not null) canvas.DrawText(textBlob, 0, 0, paint);
    }

    private static void DrawShapedTextRun(
        SKCanvas canvas,
        SKShaper shaper,
        string text,
        float x,
        float baseline,
        SKPaint paint,
        float horizontalAdvanceScale,
        float deviceScaleFactor,
        NativeFontRasterizationMode? rasterizationMode,
        float letterSpacing = 0,
        float wordSpacing = 0)
    {
        var result = shaper.Shape(text, 0, baseline, paint);
        if (result.Codepoints.Length == 0 || result.Points.Length == 0)
        {
            return;
        }

        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        ApplyFontRasterizationProfile(font, deviceScaleFactor, rasterizationMode);
        using var builder = new SKTextBlobBuilder();
        var glyphCount = Math.Min(result.Codepoints.Length, result.Points.Length);
        var run = builder.AllocatePositionedRun(font, glyphCount);
        var glyphs = run.GetGlyphSpan();
        var positions = run.GetPositionSpan();
        var spacingOffsets = ResolveCssGlyphSpacingOffsets(
            text,
            glyphCount,
            clusters: null,
            letterSpacing,
            wordSpacing);
        for (var index = 0; index < glyphCount; index++)
        {
            glyphs[index] = (ushort)result.Codepoints[index];
            positions[index] = new SKPoint(
                x + result.Points[index].X * horizontalAdvanceScale
                    + spacingOffsets[index],
                result.Points[index].Y);
        }

        using var textBlob = builder.Build();
        if (textBlob is null)
        {
            return;
        }

        canvas.DrawText(textBlob, 0, 0, paint);
    }

    private static float[] ResolveCssGlyphSpacingOffsets(
        string text,
        int glyphCount,
        uint[]? clusters,
        float letterSpacing,
        float wordSpacing)
    {
        var offsets = new float[glyphCount];
        if (glyphCount == 0 || (letterSpacing == 0 && wordSpacing == 0)) return offsets;
        var elementStarts = StringInfo.ParseCombiningCharacters(text);
        if (clusters is null && elementStarts.Length != glyphCount) return offsets;
        for (var glyphIndex = 0; glyphIndex < glyphCount; glyphIndex++)
        {
            var cluster = clusters is not null && glyphIndex < clusters.Length
                ? checked((int)clusters[glyphIndex])
                : elementStarts[glyphIndex];
            var precedingElements = 0;
            var precedingSpaces = 0;
            foreach (var elementStart in elementStarts)
            {
                if (elementStart >= cluster) break;
                precedingElements++;
                if (text[elementStart] == ' ') precedingSpaces++;
            }
            offsets[glyphIndex] = precedingElements * letterSpacing
                + precedingSpaces * wordSpacing;
        }
        return offsets;
    }

    internal readonly record struct FontRasterizationProfile(
        bool Subpixel,
        bool BaselineSnap,
        SKFontEdging Edging,
        SKFontHinting Hinting,
        bool LinearMetrics,
        bool EmbeddedBitmaps);

    internal enum NativeFontRasterizationMode
    {
        Current,
        Chromium,
        ChromiumGrayscale,
        ChromiumAntialiased
    }

    internal static FontRasterizationProfile ResolveFontRasterizationProfile(
        float deviceScaleFactor,
        NativeFontRasterizationMode? requestedMode = null)
    {
        var mode = requestedMode ?? ConfiguredRasterizationMode;
        return mode switch
        {
            NativeFontRasterizationMode.Chromium => new(
                Subpixel: true,
                BaselineSnap: false,
                Edging: SKFontEdging.SubpixelAntialias,
                Hinting: SKFontHinting.Normal,
                LinearMetrics: true,
                EmbeddedBitmaps: false),
            NativeFontRasterizationMode.ChromiumGrayscale => new(
                Subpixel: true,
                BaselineSnap: false,
                Edging: SKFontEdging.Antialias,
                Hinting: SKFontHinting.Normal,
                LinearMetrics: true,
                EmbeddedBitmaps: false),
            NativeFontRasterizationMode.ChromiumAntialiased => new(
                Subpixel: true,
                BaselineSnap: false,
                Edging: SKFontEdging.Antialias,
                Hinting: SKFontHinting.None,
                LinearMetrics: true,
                EmbeddedBitmaps: false),
            _ when float.IsFinite(deviceScaleFactor) && deviceScaleFactor >= 1.5f => new(
                Subpixel: true,
                BaselineSnap: false,
                Edging: SKFontEdging.Antialias,
                Hinting: SKFontHinting.Normal,
                LinearMetrics: false,
                EmbeddedBitmaps: false),
            _ => new(
                Subpixel: false,
                BaselineSnap: true,
                Edging: SKFontEdging.Antialias,
                Hinting: SKFontHinting.Normal,
                LinearMetrics: false,
                EmbeddedBitmaps: false)
        };
    }

    internal static void ApplyFontRasterizationProfile(
        SKFont font,
        float deviceScaleFactor,
        NativeFontRasterizationMode? requestedMode = null)
    {
        var profile = ResolveFontRasterizationProfile(
            deviceScaleFactor,
            requestedMode);
        font.Subpixel = profile.Subpixel;
        font.BaselineSnap = profile.BaselineSnap;
        font.Edging = profile.Edging;
        font.Hinting = profile.Hinting;
        font.LinearMetrics = profile.LinearMetrics;
        font.EmbeddedBitmaps = profile.EmbeddedBitmaps;
    }

    internal static NativeFontRasterizationMode ParseFontRasterizationMode(
        string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "chromium" or "chrome" => NativeFontRasterizationMode.Chromium,
            "chromium-grayscale" or "chrome-grayscale" =>
                NativeFontRasterizationMode.ChromiumGrayscale,
            "chromium-antialiased" or "chrome-antialiased" or "no-hint" =>
                NativeFontRasterizationMode.ChromiumAntialiased,
            _ => NativeFontRasterizationMode.Current
        };

    private static NativeFontRasterizationMode ResolveConfiguredFontRasterizationMode(
        string? value)
        => OperatingSystem.IsMacOS() && string.IsNullOrWhiteSpace(value)
            ? NativeFontRasterizationMode.Chromium
            : ParseFontRasterizationMode(value);

    internal static uint ResolveFeatureFlags(
        string text,
        string familyList,
        uint authoredFeatureFlags,
        WebTypefaceRegistry? registry = null)
    {
        if ((authoredFeatureFlags & TabularNumerals) != 0)
        {
            return authoredFeatureFlags;
        }
        if (!OperatingSystem.IsMacOS()
            || !UsesMacSystemUiMetrics(familyList, registry))
        {
            return authoredFeatureFlags;
        }

        var sawDigit = false;
        foreach (var character in text)
        {
            if (character is >= '0' and <= '9')
            {
                sawDigit = true;
                continue;
            }
            if (character is ' ' or '.' or ',' or '+' or '-' or '\u2212'
                or '(' or ')' or '/' or '%' or ':')
            {
                continue;
            }
            return authoredFeatureFlags;
        }
        return sawDigit ? authoredFeatureFlags | TabularNumerals : authoredFeatureFlags;
    }

    internal static float ResolveTabularDigitScale(
        string familyList,
        WebTypefaceRegistry? registry = null)
        => OperatingSystem.IsMacOS()
            && UsesMacSystemUiMetrics(familyList, registry)
            ? 1.014f
            : 1f;

    internal static bool UsesMacSystemUiMetrics(
        string familyList,
        WebTypefaceRegistry? registry)
    {
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (registry?.Contains(family) == true
                || WebTypefaces.ContainsKey(family))
            {
                return false;
            }
            if (string.Equals(family, "-apple-system", StringComparison.OrdinalIgnoreCase)
                || string.Equals(family, "BlinkMacSystemFont", StringComparison.OrdinalIgnoreCase)
                || string.Equals(family, "system-ui", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
            if (family.Equals("sans-serif", StringComparison.OrdinalIgnoreCase)
                || family.Equals("serif", StringComparison.OrdinalIgnoreCase)
                || family.Equals("monospace", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            using var installed = SKTypeface.FromFamilyName(family);
            if (installed is not null
                && string.Equals(installed.FamilyName, family, StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
        }
        return false;
    }

    internal static NativeFontRasterizationMode? ResolvePositionedRunRasterizationMode(
        NativePositionedTextRun positionedRun,
        float deviceScaleFactor,
        NativeFontRasterizationMode? requestedMode)
    {
        if (requestedMode is not null
            || !OperatingSystem.IsWindows()
            || positionedRun.FaceIdentity is null
            || !float.IsFinite(deviceScaleFactor)
            || deviceScaleFactor > 1f
            || !string.IsNullOrWhiteSpace(ConfiguredRasterizationValue))
        {
            return requestedMode;
        }
        // At 100% scaling Chromium's Windows canvas/DOM oracle uses grayscale
        // antialiasing with subpixel positioning and linear metrics. Higher
        // scale profiles retain the host defaults demonstrated by their own
        // oracle rows. Keep this attached to verified DirectWrite runs only;
        // HarfBuzz fallback and explicit process settings remain authoritative.
        return NativeFontRasterizationMode.ChromiumGrayscale;
    }

    internal static bool UsesWindowsSystemUiMetrics(
        string familyList,
        WebTypefaceRegistry? registry)
    {
        if (registry is null
            && familyList.Equals("system-ui", StringComparison.OrdinalIgnoreCase)
            && !WebTypefaces.ContainsKey("system-ui"))
        {
            return true;
        }
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (registry?.Contains(family) == true
                || WebTypefaces.ContainsKey(family))
            {
                return false;
            }
            if (family.Equals("system-ui", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
            if (family.Equals("-apple-system", StringComparison.OrdinalIgnoreCase)
                || family.Equals("BlinkMacSystemFont", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            return false;
        }
        return false;
    }

    public static NativeTextMetrics Measure(
        string text,
        string familyList,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        uint featureFlags = 0)
        => Measure(
            text,
            familyList,
            fontSize,
            fontWeight,
            letterSpacing,
            wordSpacing,
            featureFlags,
            null);

    internal static NativeTextMetrics Measure(
        string text,
        string familyList,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        uint featureFlags,
        WebTypefaceRegistry? registry)
    {
        var typeface = ResolveTypeface(familyList, fontWeight, registry);
        using var paint = new SKPaint
        {
            IsAntialias = true,
            TextSize = fontSize,
            Typeface = typeface
        };
        featureFlags = ResolveFeatureFlags(
            text,
            familyList,
            featureFlags,
            registry);
        if (TryResolveFallbackTextRuns(
                text,
                familyList,
                fontWeight,
                SKFontStyleSlant.Upright,
                registry,
                out var fallbackRuns))
        {
            var layout = LayoutFallbackTextRuns(
                fallbackRuns,
                familyList,
                fontSize,
                fontWeight,
                featureFlags,
                paint,
                registry);
            var fallbackGraphemes = StringInfo.ParseCombiningCharacters(text).Length;
            var fallbackSpacing = Math.Max(0, fallbackGraphemes - 1) * letterSpacing
                + text.Count(character => character == ' ') * wordSpacing;
            return new NativeTextMetrics
            {
                StructSize = (uint)Marshal.SizeOf<NativeTextMetrics>(),
                AdvanceWidth = layout.AdvanceWidth + fallbackSpacing,
                Ascent = layout.Ascent,
                Descent = layout.Descent,
                Leading = layout.Leading,
                ActualBoundingBoxLeft = -layout.InkBounds.Left,
                ActualBoundingBoxRight = layout.InkBounds.Right + fallbackSpacing,
                ActualBoundingBoxAscent = -layout.InkBounds.Top,
                ActualBoundingBoxDescent = layout.InkBounds.Bottom
            };
        }
        using var shaper = new SKShaper(typeface);
        var positioned = TryPositionTextRun(
            shaper,
            text,
            familyList,
            fontSize,
            fontWeight,
            SKFontStyleSlant.Upright,
            featureFlags,
            paint,
            registry,
            out var positionedRun);
        var tabularDigitScale = ResolveTabularDigitScale(familyList, registry);
        var shapedWidth = positioned
            ? positionedRun.AdvanceWidth
            : MeasureShapedWidth(
                shaper,
                text,
                paint,
                featureFlags,
                tabularDigitScale);
        paint.GetFontMetrics(out var fontMetrics);
        var graphemes = string.IsNullOrEmpty(text)
            ? 0
            : StringInfo.ParseCombiningCharacters(text).Length;
        var spaces = text.Count(character => character == ' ');
        var widthScale = positioned
            ? 1f
            : ResolveShapedWidthScale(
                text,
                familyList,
                fontSize,
                fontWeight,
                paint,
                shapedWidth,
                featureFlags,
                registry);
        var inkBounds = positioned
            ? MeasurePositionedInkBounds(positionedRun, paint)
            : MeasureShapedInkBounds(
                shaper,
                text,
                paint,
                featureFlags,
                tabularDigitScale,
                widthScale);
        var spacing = Math.Max(0, graphemes - 1) * letterSpacing
            + spaces * wordSpacing;
        return new NativeTextMetrics
        {
            StructSize = (uint)Marshal.SizeOf<NativeTextMetrics>(),
            AdvanceWidth = shapedWidth * widthScale + spacing,
            Ascent = -fontMetrics.Ascent,
            Descent = fontMetrics.Descent,
            Leading = fontMetrics.Leading,
            // Canvas TextMetrics distances are signed. In particular, the
            // left distance is negative when the ink begins to the right of
            // the alignment point; clamping it changes browser layout code
            // that derives tooltip and label bounds from these fields.
            ActualBoundingBoxLeft = -inkBounds.Left,
            ActualBoundingBoxRight = inkBounds.Right + spacing,
            ActualBoundingBoxAscent = -inkBounds.Top,
            ActualBoundingBoxDescent = inkBounds.Bottom
        };
    }

    private static WebTypefaceLease? AcquireWebTypeface(ReadOnlySpan<byte> data)
    {
        var contentHash = Convert.ToHexString(SHA256.HashData(data));
        lock (WebTypefaceCacheGate)
        {
            if (WebTypefaceCache.TryGetValue(contentHash, out var cached))
            {
                cached.ReferenceCount++;
                Interlocked.Increment(ref _webTypefaceCacheHits);
                return new WebTypefaceLease(contentHash, cached);
            }

            using var fontData = SKData.CreateCopy(data);
            var typeface = SKTypeface.FromData(fontData);
            if (typeface is null) return null;
            var shared = new SharedWebTypeface(typeface);
            WebTypefaceCache.Add(contentHash, shared);
            Interlocked.Increment(ref _webTypefaceCacheMisses);
            return new WebTypefaceLease(contentHash, shared);
        }
    }

    private static void ReleaseWebTypeface(string contentHash)
    {
        lock (WebTypefaceCacheGate)
        {
            if (!WebTypefaceCache.TryGetValue(contentHash, out var cached)) return;
            cached.ReferenceCount--;
            if (cached.ReferenceCount > 0) return;
            WebTypefaceCache.Remove(contentHash);
            cached.Dispose();
        }
    }

    // Enabled by default; an explicit zero is the diagnostic rollback switch.
    internal static readonly bool VariableFontInstancingEnabled =
        ResolveVariableFontInstancingEnabled(Environment.GetEnvironmentVariable("WEBSCENE_VARIABLE_FONT_INSTANCING"));
    internal static bool ResolveVariableFontInstancingEnabled(string? value) => value != "0";
    internal readonly record struct VariableFontMetrics(long Conversions, long Hits, long Failures, double Milliseconds, int Instances, long Bytes);
    private static long _instanceConversions, _instanceHits, _instanceFailures, _instanceTicks, _instanceBytes;
    private static int _instanceCount;
    internal static VariableFontMetrics GetVariableFontMetrics()
    {
        lock (WebTypefaceCacheGate)
            return new(_instanceConversions, Interlocked.Read(ref _instanceHits), _instanceFailures,
                _instanceTicks * 1000d / Stopwatch.Frequency, _instanceCount, _instanceBytes);
    }
    internal static (int PerFont, int Total, long Bytes) InstanceLimits = (64, 256, 64L * 1024 * 1024);
    internal static Func<SKTypeface, float, byte[]> InstanceFactory = NativeVariableFontInstancer.Instantiate;

    private sealed class SharedWebTypeface(SKTypeface typeface) : IDisposable
    {
        internal SKTypeface Typeface { get; } = typeface;
        internal NativeVariableFontInstancer.WeightAxis? Axis { get; } = NativeVariableFontInstancer.ReadWeightAxis(typeface);
        internal int ReferenceCount { get; set; } = 1;
        private readonly ConcurrentDictionary<float, SKTypeface> _variants = new();
        private readonly List<SKTypeface> _ownedVariants = [];
        private long _bytes;
        private bool _saturated;
        private bool _unavailable;
        internal SKTypeface Resolve(int weight)
        {
            if (Axis is not { } axis) return Typeface;
            var coordinate = Math.Clamp(weight, axis.Minimum, axis.Maximum);
            if (_variants.TryGetValue(coordinate, out var found))
            {
                Interlocked.Increment(ref _instanceHits);
                return found;
            }
            lock (WebTypefaceCacheGate)
            {
                if (_variants.TryGetValue(coordinate, out found)) return found;
                if (_saturated || _unavailable) return Typeface;
                var start = Stopwatch.GetTimestamp();
                try
                {
                    if (_ownedVariants.Count >= InstanceLimits.PerFont || _instanceCount >= InstanceLimits.Total
                        || _instanceBytes >= InstanceLimits.Bytes)
                    {
                        _saturated = true;
                        throw new InvalidOperationException("Variable-font instance cache limit reached.");
                    }
                    _instanceConversions++;
                    var bytes = InstanceFactory(Typeface, coordinate);
                    if (_instanceBytes + bytes.Length > InstanceLimits.Bytes)
                    {
                        _saturated = true;
                        throw new InvalidOperationException("Variable-font instance byte limit reached.");
                    }
                    using var data = SKData.CreateCopy(bytes);
                    var result = SKTypeface.FromData(data)
                        ?? throw new InvalidOperationException("Skia could not load the instantiated font.");
                    if (result.GetTableSize(0x66766172) != 0 || result.GlyphCount != Typeface.GlyphCount)
                    {
                        result.Dispose();
                        throw new InvalidOperationException("Font instantiation did not preserve a complete static face.");
                    }
                    _ownedVariants.Add(result);
                    _bytes += bytes.Length;
                    _instanceBytes += bytes.Length;
                    _instanceCount++;
                    _variants[coordinate] = result;
                    return result;
                }
                catch (Exception error)
                {
                    if (error is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
                        _unavailable = true;
                    _instanceFailures++;
                    _variants[coordinate] = Typeface;
                    Console.Error.WriteLine($"[WebScene font instancing] {Typeface.FamilyName} wght={coordinate}: {error.Message}");
                    return Typeface;
                }
                finally { _instanceTicks += Stopwatch.GetTimestamp() - start; }
            }
        }
        public void Dispose()
        {
            foreach (var variant in _ownedVariants) variant.Dispose();
            _instanceCount -= _ownedVariants.Count;
            _instanceBytes -= _bytes;
            _ownedVariants.Clear();
            _variants.Clear();
            Typeface.Dispose();
        }
    }

    private sealed class WebTypefaceLease(
        string contentHash,
        SharedWebTypeface shared) : IDisposable
    {
        private string? _contentHash = contentHash;
        internal SharedWebTypeface Shared { get; } = shared;
        internal SKTypeface Typeface => Shared.Typeface;

        public void Dispose()
        {
            var hash = Interlocked.Exchange(ref _contentHash, null);
            if (hash is not null) ReleaseWebTypeface(hash);
        }
    }
}
