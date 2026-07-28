using System.Collections.Frozen;
using System.Globalization;

namespace WebScene.Css;

/// <summary>
/// Browser-shaped CSSOM property exposure for the bounded WebScene component profile.
/// The catalog is deliberately independent of any JavaScript framework: it tells a
/// CSSStyleDeclaration proxy which IDL attributes are CSS properties and which names
/// must remain ordinary JavaScript expandos.
/// </summary>
public static class CssPropertyCatalog
{
    private static readonly FrozenSet<string> s_supported = CssKnownProperties.Names
        .Concat(new[]
        {
            "animation-delay", "animation-direction", "animation-duration", "animation-fill-mode",
            "animation-iteration-count", "animation-name", "animation-play-state", "animation-timing-function",
            "appearance", "background-attachment", "background-clip", "background-origin",
            "background-position", "background-position-x", "background-position-y", "background-repeat",
            "background-size", "border-bottom", "border-collapse", "border-left", "border-right",
            "border-spacing", "border-top", "clear", "column-count", "columns", "css-float",
            "empty-cells", "fill-opacity", "float", "font-stretch", "grid-area", "grid-column",
            "grid-column-end", "grid-column-start", "grid-row", "grid-row-end", "grid-row-start",
            "grid-gap", "inset-block", "inset-inline", "inset-inline-start", "inset-inline-end",
            "margin-block", "margin-inline", "margin-inline-start", "margin-inline-end",
            "padding-block", "padding-inline", "padding-inline-start", "padding-inline-end",
            "border-inline-start", "border-inline-end",
            "border-inline-start-width", "border-inline-end-width",
            "border-inline-start-color", "border-inline-end-color",
            "border-start-start-radius", "border-start-end-radius",
            "border-end-start-radius", "border-end-end-radius",
            "-moz-transform", "moz-transform", "-webkit-transform", "webkit-transform",
            "orphans", "resize", "table-layout", "text-decoration", "text-overflow", "vertical-align",
            "transition", "transition-delay", "transition-duration", "transition-property",
            "transition-timing-function", "transform-origin", "widows", "zoom"
        })
        .ToFrozenSet(StringComparer.OrdinalIgnoreCase);

    public static bool IsSupported(string? propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName) || propertyName.StartsWith("--", StringComparison.Ordinal))
        {
            return false;
        }

        return s_supported.Contains(Normalize(propertyName));
    }

    /// <summary>
    /// Returns whether a property stores a CSS color value rather than another
    /// value whose name merely contains the word "color".
    /// </summary>
    public static bool IsColorValueProperty(string? propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName)
            || propertyName.StartsWith("--", StringComparison.Ordinal))
        {
            return false;
        }

        var normalized = Normalize(propertyName);
        return normalized is "color" or "fill" or "stroke"
               || normalized.EndsWith("-color", StringComparison.Ordinal);
    }

    public static bool IsValidCssomValue(string propertyName, string value)
    {
        if (propertyName.StartsWith("--", StringComparison.Ordinal))
        {
            return true;
        }

        // An empty string removes an authored declaration. Other whitespace-only
        // strings are invalid CSS tokens and must leave the previous declaration.
        if (value.Length == 0)
        {
            return true;
        }
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var normalized = Normalize(propertyName);
        var trimmed = value.Trim();
        var normalizedValue = trimmed.ToLowerInvariant();
        if (normalizedValue is "inherit" or "initial" or "revert" or "revert-layer" or "unset"
            || trimmed.Contains("var(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("calc(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("min(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("max(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("clamp(", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return normalized switch
        {
            "position" => normalizedValue is "static" or "relative" or "absolute" or "fixed" or "sticky"
                or "-webkit-sticky",
            "font-size" => IsFontSize(normalizedValue),
            "letter-spacing" => normalizedValue == "normal"
                || !IsInvalidUnitlessLength(trimmed),
            _ => true
        };
    }

    private static string Normalize(string propertyName)
    {
        var normalized = propertyName.Trim();
        if (normalized.Equals("cssFloat", StringComparison.Ordinal))
        {
            return "float";
        }

        if (normalized.Contains('-'))
        {
            return normalized.ToLowerInvariant();
        }

        var builder = new System.Text.StringBuilder(normalized.Length + 4);
        for (var index = 0; index < normalized.Length; index++)
        {
            var character = normalized[index];
            if (char.IsUpper(character) && index > 0)
            {
                builder.Append('-');
            }
            builder.Append(char.ToLowerInvariant(character));
        }
        return builder.ToString();
    }

    private static bool IsFontSize(string value)
    {
        if (value is "xx-small" or "x-small" or "small" or "medium" or "large" or "x-large" or "xx-large"
            or "xxx-large" or "larger" or "smaller")
        {
            return true;
        }

        return !IsInvalidUnitlessLength(value);
    }

    private static bool IsInvalidUnitlessLength(string value)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var numeric)
           && double.IsFinite(numeric)
           && numeric != 0;
}
