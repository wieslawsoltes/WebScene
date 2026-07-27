using System.Globalization;
using System.Text.RegularExpressions;

namespace WebScene.Css;

public enum CssMediaPointer
{
    None,
    Coarse,
    Fine
}

public enum CssMediaHover
{
    None,
    Hover
}

public enum CssPreferredColorScheme
{
    Light,
    Dark
}

public enum CssPreferredMotion
{
    NoPreference,
    Reduce
}

/// <summary>Portable environment used to evaluate CSS media queries.</summary>
public readonly record struct CssMediaEnvironment(
    double Width,
    double Height,
    double DevicePixelRatio,
    string MediaType = "screen",
    CssMediaPointer Pointer = CssMediaPointer.Fine,
    CssMediaHover Hover = CssMediaHover.Hover,
    CssPreferredColorScheme ColorScheme = CssPreferredColorScheme.Light,
    CssPreferredMotion Motion = CssPreferredMotion.NoPreference);

/// <summary>
/// Evaluates the bounded media-query profile supported by WebScene without depending on
/// a UI framework. Comma-separated branches are ORed and <c>and</c> conditions are ANDed.
/// </summary>
public static partial class CssMediaQueryEvaluator
{
    public static bool Matches(string? query, in CssMediaEnvironment environment)
    {
        if (string.IsNullOrWhiteSpace(query) || !IsValid(environment))
        {
            return false;
        }

        foreach (var branch in query.Split(','))
        {
            if (MatchesBranch(branch, environment))
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsValid(in CssMediaEnvironment environment)
        => double.IsFinite(environment.Width)
           && double.IsFinite(environment.Height)
           && double.IsFinite(environment.DevicePixelRatio)
           && environment.Width >= 0
           && environment.Height >= 0
           && environment.DevicePixelRatio > 0;

    private static bool MatchesBranch(string query, in CssMediaEnvironment environment)
    {
        var normalized = query.Trim().ToLowerInvariant();
        var negate = normalized.StartsWith("not ", StringComparison.Ordinal);
        if (negate)
        {
            normalized = normalized[4..].Trim();
        }

        if (normalized.StartsWith("only ", StringComparison.Ordinal))
        {
            normalized = normalized[5..].Trim();
        }

        var result = true;
        foreach (var condition in AndSeparator().Split(normalized))
        {
            var part = condition.Trim();
            if (part.Length == 0 || part == "all")
            {
                continue;
            }

            if (!part.StartsWith('('))
            {
                if (!string.Equals(part, environment.MediaType, StringComparison.OrdinalIgnoreCase))
                {
                    result = false;
                    break;
                }

                continue;
            }

            if (!MatchesCondition(part, environment))
            {
                result = false;
                break;
            }
        }

        return negate ? !result : result;
    }

    private static bool MatchesCondition(string condition, in CssMediaEnvironment environment)
    {
        if (!condition.StartsWith('(') || !condition.EndsWith(')'))
        {
            return false;
        }

        var expression = condition[1..^1].Trim();
        var separator = expression.IndexOf(':');
        var feature = (separator < 0 ? expression : expression[..separator]).Trim();
        var value = separator < 0 ? string.Empty : expression[(separator + 1)..].Trim();
        return feature switch
        {
            "hover" or "any-hover" => MatchesEnum(value, environment.Hover, "hover", CssMediaHover.Hover),
            "pointer" or "any-pointer" => MatchesEnum(value, environment.Pointer, "fine", CssMediaPointer.Fine)
                                               || MatchesEnum(value, environment.Pointer, "coarse", CssMediaPointer.Coarse)
                                               || MatchesEnum(value, environment.Pointer, "none", CssMediaPointer.None),
            "orientation" => value == (environment.Width >= environment.Height ? "landscape" : "portrait"),
            "prefers-reduced-motion" => value == (environment.Motion == CssPreferredMotion.Reduce ? "reduce" : "no-preference"),
            "prefers-color-scheme" => value == (environment.ColorScheme == CssPreferredColorScheme.Dark ? "dark" : "light"),
            "min-width" => TryParseCssPixels(value, out var minWidth) && environment.Width >= minWidth,
            "max-width" => TryParseCssPixels(value, out var maxWidth) && environment.Width <= maxWidth,
            "width" => TryParseCssPixels(value, out var exactWidth) && NearlyEqual(environment.Width, exactWidth),
            "min-height" => TryParseCssPixels(value, out var minHeight) && environment.Height >= minHeight,
            "max-height" => TryParseCssPixels(value, out var maxHeight) && environment.Height <= maxHeight,
            "height" => TryParseCssPixels(value, out var exactHeight) && NearlyEqual(environment.Height, exactHeight),
            "min-resolution" => TryParseResolution(value, out var minResolution) && environment.DevicePixelRatio >= minResolution,
            "max-resolution" => TryParseResolution(value, out var maxResolution) && environment.DevicePixelRatio <= maxResolution,
            "resolution" => TryParseResolution(value, out var resolution) && NearlyEqual(environment.DevicePixelRatio, resolution),
            "-webkit-min-device-pixel-ratio" => TryParseNumber(value, out var minRatio) && environment.DevicePixelRatio >= minRatio,
            "-webkit-max-device-pixel-ratio" => TryParseNumber(value, out var maxRatio) && environment.DevicePixelRatio <= maxRatio,
            _ => false
        };
    }

    private static bool MatchesEnum<T>(string value, T actual, string expectedText, T expected)
        where T : struct, Enum
        => value.Length == 0 ? EqualityComparer<T>.Default.Equals(actual, expected) : value == expectedText && EqualityComparer<T>.Default.Equals(actual, expected);

    private static bool TryParseCssPixels(string value, out double pixels)
    {
        var normalized = value.Trim();
        if (normalized.EndsWith("px", StringComparison.Ordinal))
        {
            normalized = normalized[..^2].Trim();
        }

        return TryParseNumber(normalized, out pixels);
    }

    private static bool TryParseResolution(string value, out double ratio)
    {
        var normalized = value.Trim();
        if (normalized.EndsWith("dppx", StringComparison.Ordinal))
        {
            return TryParseNumber(normalized[..^4].Trim(), out ratio);
        }

        if (normalized.EndsWith("dpi", StringComparison.Ordinal)
            && TryParseNumber(normalized[..^3].Trim(), out var dpi))
        {
            ratio = dpi / 96d;
            return true;
        }

        return TryParseNumber(normalized, out ratio);
    }

    private static bool TryParseNumber(string value, out double result)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out result)
           && double.IsFinite(result);

    private static bool NearlyEqual(double left, double right) => Math.Abs(left - right) < .01;

    [GeneratedRegex(@"\s+and\s+", RegexOptions.CultureInvariant)]
    private static partial Regex AndSeparator();
}
