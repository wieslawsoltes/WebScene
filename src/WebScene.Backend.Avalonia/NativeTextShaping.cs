using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
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
using JavaScript.Avalonia;
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
}

public static class NativeTextShaping
{
    internal const uint TabularNumerals = 1u << 0;
    private static readonly ConcurrentDictionary<string, SKTypeface> Typefaces =
        new(StringComparer.Ordinal);
    private static readonly ConcurrentDictionary<string, SKTypeface> WebTypefaces =
        new(StringComparer.OrdinalIgnoreCase);

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
    {
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (WebTypefaces.TryGetValue(family, out var webTypeface))
            {
                return webTypeface;
            }
        }

        var requestedWeight = Math.Clamp(fontWeight, 1, 1000);
        var key = $"{familyList}\u001f{requestedWeight}";
        return Typefaces.GetOrAdd(key, _ =>
        {
            foreach (var rawFamily in familyList.Split(
                         ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
            {
                var family = rawFamily.Trim('"', '\'');
                if (family is "-apple-system" or "BlinkMacSystemFont" or "system-ui"
                    or "sans-serif")
                {
                    family = OperatingSystem.IsMacOS() ? ".AppleSystemUIFont" : "Arial";
                }
                else if (family == "serif") family = "Times New Roman";
                else if (family == "monospace") family = OperatingSystem.IsMacOS() ? "Menlo" : "Consolas";

                var candidate = SKTypeface.FromFamilyName(
                    family,
                    requestedWeight,
                    (int)SKFontStyleWidth.Normal,
                    SKFontStyleSlant.Upright);
                if (candidate is not null
                    && (string.Equals(candidate.FamilyName, family, StringComparison.OrdinalIgnoreCase)
                        || rawFamily is "-apple-system" or "BlinkMacSystemFont" or "system-ui"
                            or "sans-serif" or "serif" or "monospace"))
                {
                    return candidate;
                }
                candidate?.Dispose();
            }
            return SKTypeface.Default;
        });
    }

    internal static float ResolveWidthScale(string familyList, float fontSize, int fontWeight)
    {
        if (!OperatingSystem.IsMacOS() || !UsesMacSystemUiMetrics(familyList))
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

    internal static void DrawShapedText(
        SKCanvas canvas,
        SKShaper shaper,
        string text,
        float x,
        float baseline,
        SKPaint paint,
        uint featureFlags,
        float tabularDigitScale = 1f)
    {
        if (string.IsNullOrEmpty(text))
        {
            return;
        }
        if ((featureFlags & TabularNumerals) == 0)
        {
            DrawShapedTextRun(canvas, shaper, text, x, baseline, paint);
            return;
        }

        var tabularDigitWidth = shaper.Shape("0", paint).Width * tabularDigitScale;
        var cursor = x;
        for (var index = 0; index < text.Length;)
        {
            if (text[index] is >= '0' and <= '9')
            {
                var digit = text[index].ToString();
                DrawShapedTextRun(canvas, shaper, digit, cursor, baseline, paint);
                cursor += tabularDigitWidth;
                index++;
                continue;
            }
            var start = index++;
            while (index < text.Length && text[index] is not (>= '0' and <= '9')) index++;
            var segment = text[start..index];
            DrawShapedTextRun(canvas, shaper, segment, cursor, baseline, paint);
            cursor += shaper.Shape(segment, paint).Width;
        }
    }

    private static void DrawShapedTextRun(
        SKCanvas canvas,
        SKShaper shaper,
        string text,
        float x,
        float baseline,
        SKPaint paint)
    {
        var result = shaper.Shape(text, x, baseline, paint);
        if (result.Codepoints.Length == 0 || result.Points.Length == 0)
        {
            return;
        }

        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        using var builder = new SKTextBlobBuilder();
        var glyphCount = Math.Min(result.Codepoints.Length, result.Points.Length);
        var run = builder.AllocatePositionedRun(font, glyphCount);
        var glyphs = run.GetGlyphSpan();
        var positions = run.GetPositionSpan();
        for (var index = 0; index < glyphCount; index++)
        {
            glyphs[index] = (ushort)result.Codepoints[index];
            positions[index] = result.Points[index];
        }

        using var textBlob = builder.Build();
        if (textBlob is null)
        {
            return;
        }

        var xOffset = paint.TextAlign switch
        {
            SKTextAlign.Center => -result.Width * 0.5f,
            SKTextAlign.Right => -result.Width,
            _ => 0f,
        };
        canvas.DrawText(textBlob, xOffset, 0, paint);
    }

    internal static uint ResolveFeatureFlags(
        string text,
        string familyList,
        uint authoredFeatureFlags)
    {
        if ((authoredFeatureFlags & TabularNumerals) != 0)
        {
            return authoredFeatureFlags;
        }
        if (!OperatingSystem.IsMacOS() || !UsesMacSystemUiMetrics(familyList))
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

    internal static float ResolveTabularDigitScale(string familyList)
        => OperatingSystem.IsMacOS() && UsesMacSystemUiMetrics(familyList)
            ? 1.014f
            : 1f;

    private static bool UsesMacSystemUiMetrics(string familyList)
    {
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (WebTypefaces.ContainsKey(family))
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

    public static NativeTextMetrics Measure(
        string text,
        string familyList,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        uint featureFlags = 0)
    {
        var typeface = ResolveTypeface(familyList, fontWeight);
        using var paint = new SKPaint
        {
            IsAntialias = true,
            TextSize = fontSize,
            Typeface = typeface
        };
        using var shaper = new SKShaper(typeface);
        featureFlags = ResolveFeatureFlags(text, familyList, featureFlags);
        var shapedWidth = MeasureShapedWidth(
            shaper,
            text,
            paint,
            featureFlags,
            ResolveTabularDigitScale(familyList));
        paint.GetFontMetrics(out var fontMetrics);
        var graphemes = string.IsNullOrEmpty(text)
            ? 0
            : StringInfo.ParseCombiningCharacters(text).Length;
        var spaces = text.Count(character => character == ' ');
        return new NativeTextMetrics
        {
            StructSize = (uint)Marshal.SizeOf<NativeTextMetrics>(),
            AdvanceWidth = shapedWidth
                * ((featureFlags & TabularNumerals) != 0
                    ? ResolveWidthScale(familyList, fontSize, fontWeight)
                    : 1f)
                + Math.Max(0, graphemes - 1) * letterSpacing
                + spaces * wordSpacing,
            Ascent = -fontMetrics.Ascent,
            Descent = fontMetrics.Descent,
            Leading = fontMetrics.Leading
        };
    }
}
