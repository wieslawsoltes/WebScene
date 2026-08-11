using System.Globalization;
using WebScene.Core;

namespace WebScene.Css;

public static class CssColorParser
{
    /// <summary>
    /// Serializes authored hex and functional colors using the CSSOM form returned
    /// by browser inline-style declarations. Color keywords are intentionally not
    /// rewritten because browsers preserve their authored keyword spelling.
    /// </summary>
    public static bool TrySerializeSpecifiedColor(string? value, out string serialized)
    {
        serialized = string.Empty;
        var normalized = value?.Trim() ?? string.Empty;
        var isHex = normalized.StartsWith('#');
        var isFunctional = normalized.StartsWith("rgb(", StringComparison.OrdinalIgnoreCase)
                           || normalized.StartsWith("rgba(", StringComparison.OrdinalIgnoreCase);
        if ((!isHex && !isFunctional)
            || !TryParseColor(normalized, out var color))
        {
            return false;
        }

        if (color.A == byte.MaxValue)
        {
            serialized = $"rgb({color.R}, {color.G}, {color.B})";
            return true;
        }

        var alpha = color.A / 255d;
        if (isFunctional
            && normalized.StartsWith("rgba(", StringComparison.OrdinalIgnoreCase))
        {
            var parts = normalized[(normalized.IndexOf('(') + 1)..^1]
                .Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 4 && TryAlphaValue(parts[3], out var authoredAlpha))
            {
                alpha = authoredAlpha;
            }
        }

        alpha = Math.Round(alpha, 3);
        serialized =
            $"rgba({color.R}, {color.G}, {color.B}, {alpha.ToString("0.###", CultureInfo.InvariantCulture)})";
        return true;
    }

    /// <summary>
    /// Parses CSS hex colors, the supported comma-separated rgb()/rgba() forms,
    /// and the bounded CSS named colors used by the component profile.
    /// </summary>
    public static bool TryParseColor(string? value, out WebSceneColor color)
    {
        var normalized = value?.Trim() ?? string.Empty;
        return TryParseHexColor(normalized, out color)
               || TryParseFunctionalColor(normalized, out color)
               || TryParseNamedColor(normalized, out color);
    }

    /// <summary>Parses the comma-separated rgb()/rgba() forms used by the supported component profile.</summary>
    public static bool TryParseFunctionalColor(string? value, out WebSceneColor color)
    {
        color = default;
        var normalized = value?.Trim() ?? string.Empty;
        var open = normalized.IndexOf('(');
        if (open <= 0 || !normalized.EndsWith(')'))
        {
            return false;
        }

        var function = normalized[..open].Trim().ToLowerInvariant();
        if (function is not ("rgb" or "rgba"))
        {
            return false;
        }

        var parts = normalized[(open + 1)..^1]
            .Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != (function == "rgba" ? 4 : 3))
        {
            return false;
        }

        if (!TryColorComponent(parts[0], out var red)
            || !TryColorComponent(parts[1], out var green)
            || !TryColorComponent(parts[2], out var blue))
        {
            return false;
        }

        var alpha = byte.MaxValue;
        if (parts.Length == 4 && !TryAlpha(parts[3], out alpha))
        {
            return false;
        }

        color = new WebSceneColor(alpha, red, green, blue);
        return true;
    }

    private static bool TryParseHexColor(string value, out WebSceneColor color)
    {
        color = default;
        if (value.Length is not (4 or 5 or 7 or 9) || value[0] != '#')
        {
            return false;
        }

        if (value.Length is 4 or 5)
        {
            byte alpha = 0;
            if (!TryHexNibble(value[1], out var red)
                || !TryHexNibble(value[2], out var green)
                || !TryHexNibble(value[3], out var blue)
                || (value.Length == 5 && !TryHexNibble(value[4], out alpha)))
            {
                return false;
            }

            color = new WebSceneColor(
                value.Length == 5 ? (byte)(alpha * 17) : byte.MaxValue,
                (byte)(red * 17),
                (byte)(green * 17),
                (byte)(blue * 17));
            return true;
        }

        byte a = 0;
        if (!TryHexByte(value, 1, out var r)
            || !TryHexByte(value, 3, out var g)
            || !TryHexByte(value, 5, out var b)
            || (value.Length == 9 && !TryHexByte(value, 7, out a)))
        {
            return false;
        }

        // CSS serializes eight digits as #RRGGBBAA. Avalonia's native parser
        // accepts #AARRGGBB, so this must be decoded before Brush.Parse sees it.
        color = new WebSceneColor(value.Length == 9 ? a : byte.MaxValue, r, g, b);
        return true;
    }

    private static bool TryParseNamedColor(string value, out WebSceneColor color)
    {
        if (value.Equals("transparent", StringComparison.OrdinalIgnoreCase))
        {
            color = new WebSceneColor(0, 0, 0, 0);
            return true;
        }

        // Keep the CSS basic palette, orange, and gray/grey aliases independent
        // of platform color converters so Avalonia and Uno resolve identical
        // component colors.
        var rgb = value.ToLowerInvariant() switch
        {
            "black" => 0x000000,
            "silver" => 0xc0c0c0,
            "gray" or "grey" => 0x808080,
            "white" => 0xffffff,
            "maroon" => 0x800000,
            "red" => 0xff0000,
            "purple" => 0x800080,
            "fuchsia" or "magenta" => 0xff00ff,
            "green" => 0x008000,
            "lime" => 0x00ff00,
            "olive" => 0x808000,
            "yellow" => 0xffff00,
            "navy" => 0x000080,
            "blue" => 0x0000ff,
            "teal" => 0x008080,
            "aqua" or "cyan" => 0x00ffff,
            "orange" => 0xffa500,
            "darkgray" or "darkgrey" => 0xa9a9a9,
            "dimgray" or "dimgrey" => 0x696969,
            "lightgray" or "lightgrey" => 0xd3d3d3,
            "lightslategray" or "lightslategrey" => 0x778899,
            "slategray" or "slategrey" => 0x708090,
            "darkslategray" or "darkslategrey" => 0x2f4f4f,
            _ => -1
        };
        if (rgb < 0)
        {
            color = default;
            return false;
        }

        color = new WebSceneColor(
            byte.MaxValue,
            (byte)(rgb >> 16),
            (byte)(rgb >> 8),
            (byte)rgb);
        return true;
    }

    private static bool TryHexByte(string value, int index, out byte result)
    {
        result = 0;
        if (!TryHexNibble(value[index], out var high)
            || !TryHexNibble(value[index + 1], out var low))
        {
            return false;
        }
        result = (byte)((high << 4) | low);
        return true;
    }

    private static bool TryHexNibble(char value, out byte result)
    {
        if (value is >= '0' and <= '9')
        {
            result = (byte)(value - '0');
            return true;
        }
        if (value is >= 'a' and <= 'f')
        {
            result = (byte)(value - 'a' + 10);
            return true;
        }
        if (value is >= 'A' and <= 'F')
        {
            result = (byte)(value - 'A' + 10);
            return true;
        }
        result = 0;
        return false;
    }

    private static bool TryColorComponent(string text, out byte component)
    {
        component = 0;
        var percent = text.EndsWith('%');
        var numberText = percent ? text[..^1] : text;
        if (!double.TryParse(numberText, NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
            || !double.IsFinite(number))
        {
            return false;
        }

        component = (byte)Math.Round(Math.Clamp(percent ? number * 2.55 : number, 0, 255));
        return true;
    }

    private static bool TryAlpha(string text, out byte alpha)
    {
        alpha = byte.MaxValue;
        if (!TryAlphaValue(text, out var normalized))
        {
            return false;
        }

        alpha = (byte)Math.Round(normalized * byte.MaxValue);
        return true;
    }

    private static bool TryAlphaValue(string text, out double alpha)
    {
        alpha = 1;
        var percent = text.EndsWith('%');
        var numberText = percent ? text[..^1] : text;
        if (!double.TryParse(numberText, NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
            || !double.IsFinite(number))
        {
            return false;
        }

        alpha = Math.Clamp(percent ? number / 100d : number, 0, 1);
        return true;
    }
}
