using System.Globalization;
using System.Text;

namespace WebScene.Css;

/// <summary>
/// Backend-neutral computed-value normalization applied after cascade and custom
/// property resolution. Backends consume the normalized longhands only.
/// </summary>
internal static class CssComputedValueNormalizer
{
    internal static bool TryExpandGridPlacementShorthand(
        string propertyName,
        string value,
        out string rowStart,
        out string columnStart,
        out string rowEnd,
        out string columnEnd)
    {
        rowStart = columnStart = rowEnd = columnEnd = "auto";
        var normalizedName = propertyName.Trim().ToLowerInvariant();
        if (normalizedName is not ("grid-area" or "grid-row" or "grid-column"))
        {
            return false;
        }

        var components = SplitTopLevelSlashComponents(value);
        var maximumComponents = normalizedName == "grid-area" ? 4 : 2;
        if (components.Count is 0 || components.Count > maximumComponents
            || components.Any(string.IsNullOrWhiteSpace))
        {
            return false;
        }

        var singleComponent = components.Count == 1
            ? components[0].Trim().ToLowerInvariant()
            : string.Empty;
        if (singleComponent is "inherit" or "initial" or "unset" or "revert" or "revert-layer")
        {
            rowStart = columnStart = rowEnd = columnEnd = singleComponent;
            return true;
        }

        if (normalizedName == "grid-area")
        {
            rowStart = components[0];
            columnStart = components.Count > 1 ? components[1] : "auto";
            rowEnd = components.Count > 2 ? components[2] : "auto";
            columnEnd = components.Count > 3 ? components[3] : "auto";
            return true;
        }

        if (normalizedName == "grid-row")
        {
            rowStart = components[0];
            rowEnd = components.Count > 1 ? components[1] : "auto";
            return true;
        }

        columnStart = components[0];
        columnEnd = components.Count > 1 ? components[1] : "auto";
        return true;
    }

    internal static void ExpandShorthands(CssPropertyValueStore values)
    {
        ArgumentNullException.ThrowIfNull(values);
        ExpandBox(values, "margin");
        ExpandBox(values, "padding");
        ExpandBox(values, "border-width", "border", "width");
        ExpandBox(values, "border-color", "border", "color");
        ExpandBox(values, "border-style", "border", "style");
        ExpandBorderRadius(values);
        ExpandBorderDeclaration(values, "border", null);
        foreach (var side in new[] { "top", "right", "bottom", "left" })
        {
            ExpandBorderDeclaration(values, $"border-{side}", side);
        }
        ExpandOutline(values);

        if (values.TryGetValue("background", out var background))
        {
            var tokens = SplitTokens(background);
            if (tokens.Count == 1)
            {
                values["background-color"] = tokens[0];
            }
        }

        ExpandFont(values);
        NormalizeLineHeightForAbsoluteFont(values);

        if (values.TryGetValue("inset", out var inset))
        {
            ExpandBoxTokens(values, SplitTokens(inset), "top", "right", "bottom", "left");
        }

        if (values.TryGetValue("overflow", out var overflow))
        {
            var tokens = SplitTokens(overflow);
            if (tokens.Count > 0)
            {
                values["overflow-x"] = tokens[0];
                values["overflow-y"] = tokens.Count > 1 ? tokens[1] : tokens[0];
            }
        }

        var hasGap = values.TryGetValue("gap", out var gap);
        if (!hasGap) hasGap = values.TryGetValue("grid-gap", out gap);
        if (hasGap)
        {
            var tokens = SplitTokens(gap);
            if (tokens.Count > 0)
            {
                values["row-gap"] = tokens[0];
                values["column-gap"] = tokens.Count > 1 ? tokens[1] : tokens[0];
            }
        }

        // Shorthands must be expanded before simple em values are converted:
        // the font-relative unit belongs to each computed longhand, and layout
        // must never silently retain the preceding percentage/length when a
        // newly authored em value is otherwise valid.
        NormalizeFontRelativeBoxLengths(values);

        if (values.TryGetValue("flex-flow", out var flexFlow))
        {
            foreach (var token in SplitTokens(flexFlow))
            {
                if (token is "row" or "row-reverse" or "column" or "column-reverse")
                {
                    values["flex-direction"] = token;
                }
                if (token is "nowrap" or "wrap" or "wrap-reverse")
                {
                    values["flex-wrap"] = token;
                }
            }
        }

        if (values.TryGetValue("flex", out var flex))
        {
            var tokens = SplitTokens(flex);
            if (tokens.Count == 1 && tokens[0] == "none")
            {
                values["flex-grow"] = "0";
                values["flex-shrink"] = "0";
                values["flex-basis"] = "auto";
            }
            else if (tokens.Count == 1 && tokens[0] == "auto")
            {
                values["flex-grow"] = "1";
                values["flex-shrink"] = "1";
                values["flex-basis"] = "auto";
            }
            else
            {
                if (tokens.Count > 0 && IsNumber(tokens[0])) values["flex-grow"] = tokens[0];
                if (tokens.Count > 1 && IsNumber(tokens[1])) values["flex-shrink"] = tokens[1];
                var basis = tokens.LastOrDefault(token => !IsNumber(token));
                if (!string.IsNullOrWhiteSpace(basis)) values["flex-basis"] = basis;
            }
        }
    }


    internal static void NormalizeOverflow(CssPropertyValueStore values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var overflowX = values.TryGetValue("overflow-x", out var x)
            ? x.Trim().ToLowerInvariant()
            : "visible";
        var overflowY = values.TryGetValue("overflow-y", out var y)
            ? y.Trim().ToLowerInvariant()
            : "visible";
        var originalX = overflowX;
        var originalY = overflowY;

        if (originalX == "visible" && originalY is not "visible" and not "clip") overflowX = "auto";
        else if (originalX == "clip" && originalY is not "visible" and not "clip") overflowX = "hidden";
        if (originalY == "visible" && originalX is not "visible" and not "clip") overflowY = "auto";
        else if (originalY == "clip" && originalX is not "visible" and not "clip") overflowY = "hidden";

        values["overflow-x"] = overflowX;
        values["overflow-y"] = overflowY;
        values["overflow"] = string.Equals(overflowX, overflowY, StringComparison.Ordinal)
            ? overflowX
            : $"{overflowX} {overflowY}";
    }

    private static bool ExpandFont(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("font", out var font)) return false;
        var tokens = SplitTokens(font);
        var sizeIndex = -1;
        for (var index = 0; index < tokens.Count; index++)
        {
            var sizeToken = tokens[index].Split('/', 2)[0];
            if (CssLengthParser.TryParseAbsoluteLength(sizeToken, out _)
                || sizeToken.EndsWith("em", StringComparison.OrdinalIgnoreCase)
                || sizeToken.EndsWith("rem", StringComparison.OrdinalIgnoreCase)
                || sizeToken.EndsWith("%", StringComparison.OrdinalIgnoreCase))
            {
                sizeIndex = index;
                break;
            }
        }

        if (sizeIndex < 0 || sizeIndex + 1 >= tokens.Count) return false;
        var authoredSize = tokens[sizeIndex].Split('/', 2)[0];
        if (!CssLengthParser.TryParseAbsoluteLength(authoredSize, out _)) return false;

        values["font-style"] = "normal";
        values["font-variant"] = "normal";
        values["font-weight"] = "normal";
        foreach (var token in tokens.Take(sizeIndex))
        {
            if (token is "italic" or "oblique" or "normal") values["font-style"] = token;
            else if (token is "small-caps") values["font-variant"] = token;
            else if (token is "bold" or "bolder" or "lighter"
                     || int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out _))
            {
                values["font-weight"] = token;
            }
        }

        var sizeAndLineHeight = tokens[sizeIndex].Split('/', 2);
        values["font-size"] = sizeAndLineHeight[0];
        var familyStart = sizeIndex + 1;
        if (sizeAndLineHeight.Length == 2) values["line-height"] = sizeAndLineHeight[1];
        else if (familyStart + 1 < tokens.Count && tokens[familyStart] == "/")
        {
            values["line-height"] = tokens[familyStart + 1];
            familyStart += 2;
        }
        else values["line-height"] = "normal";
        if (familyStart < tokens.Count) values["font-family"] = string.Join(" ", tokens.Skip(familyStart));
        return true;
    }

    private static void NormalizeLineHeightForAbsoluteFont(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("font-size", out var fontSize)
            || !CssLengthParser.TryParseAbsoluteLength(fontSize, out var fontSizePixels)
            || !values.TryGetValue("line-height", out var lineHeight)
            || string.Equals(lineHeight.Trim(), "normal", StringComparison.OrdinalIgnoreCase)) return;

        var normalized = lineHeight.Trim();
        double lineHeightPixels;
        if (normalized.EndsWith("em", StringComparison.OrdinalIgnoreCase)
            && !normalized.EndsWith("rem", StringComparison.OrdinalIgnoreCase)
            && double.TryParse(normalized[..^2], NumberStyles.Float, CultureInfo.InvariantCulture, out var em))
        {
            lineHeightPixels = fontSizePixels * em;
        }
        else if (double.TryParse(normalized, NumberStyles.Float, CultureInfo.InvariantCulture, out var multiplier))
        {
            lineHeightPixels = fontSizePixels * multiplier;
        }
        else return;
        values["line-height"] = lineHeightPixels.ToString("0.###", CultureInfo.InvariantCulture) + "px";
    }

    private static void NormalizeFontRelativeBoxLengths(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("font-size", out var fontSize)
            || !CssLengthParser.TryParseAbsoluteLength(fontSize, out var fontSizePixels))
        {
            return;
        }

        foreach (var property in new[]
                 {
                     "top", "right", "bottom", "left",
                     "width", "height",
                     "min-width", "min-height", "max-width", "max-height",
                     "margin-top", "margin-right", "margin-bottom", "margin-left",
                     "padding-top", "padding-right", "padding-bottom", "padding-left",
                     "row-gap", "column-gap", "flex-basis"
                 })
        {
            if (!values.TryGetValue(property, out var value)) continue;
            var normalized = value.Trim();
            if (!normalized.EndsWith("em", StringComparison.OrdinalIgnoreCase)
                || normalized.EndsWith("rem", StringComparison.OrdinalIgnoreCase)
                || !double.TryParse(
                    normalized.AsSpan(0, normalized.Length - 2),
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var em))
            {
                continue;
            }

            values[property] = (fontSizePixels * em).ToString("0.###", CultureInfo.InvariantCulture) + "px";
        }
    }

    private static List<string> SplitTopLevelSlashComponents(string value)
    {
        var components = new List<string>(4);
        var start = 0;
        var parenthesisDepth = 0;
        for (var index = 0; index < value.Length; index++)
        {
            switch (value[index])
            {
                case '(':
                    parenthesisDepth++;
                    break;
                case ')' when parenthesisDepth > 0:
                    parenthesisDepth--;
                    break;
                case '/' when parenthesisDepth == 0:
                    components.Add(value[start..index].Trim());
                    start = index + 1;
                    break;
            }
        }
        components.Add(value[start..].Trim());
        return components;
    }

    private static void ExpandBorderDeclaration(CssPropertyValueStore values, string shorthand, string? side)
    {
        if (!values.TryGetValue(shorthand, out var value)) return;
        var tokens = SplitTokens(value);
        var width = tokens.FirstOrDefault(token => token is "thin" or "medium" or "thick"
                                                        || CssLengthParser.TryParse(token, out _));
        var style = tokens.FirstOrDefault(token => token is
            "none" or "hidden" or "dotted" or "dashed" or "solid" or "double" or
            "groove" or "ridge" or "inset" or "outset");
        var color = tokens.FirstOrDefault(token => !string.Equals(token, width, StringComparison.Ordinal)
                                                   && !string.Equals(token, style, StringComparison.Ordinal));
        var sides = side is null ? new[] { "top", "right", "bottom", "left" } : new[] { side };
        foreach (var currentSide in sides)
        {
            if (width is not null) values[$"border-{currentSide}-width"] = width;
            if (style is not null) values[$"border-{currentSide}-style"] = style;
            if (color is not null) values[$"border-{currentSide}-color"] = color;
        }
    }

    private static void ExpandOutline(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("outline", out var shorthand)) return;
        var tokens = SplitTokens(shorthand);
        if (tokens.Count is < 1 or > 3) return;
        if (tokens.Count == 1
            && tokens[0].ToLowerInvariant() is "initial" or "inherit" or "unset" or "revert" or "revert-layer")
        {
            return;
        }

        string? width = null;
        string? style = null;
        string? color = null;
        foreach (var token in tokens)
        {
            if (IsOutlineWidth(token))
            {
                if (width is not null) return;
                width = token;
            }
            else if (IsOutlineStyle(token))
            {
                if (style is not null) return;
                style = token;
            }
            else if (color is null && IsPotentialOutlineColor(token))
            {
                color = token;
            }
            else
            {
                return;
            }
        }

        if (!values.ContainsKey("outline-color")) values["outline-color"] = color ?? "currentcolor";
        if (!values.ContainsKey("outline-style")) values["outline-style"] = style ?? "none";
        if (!values.ContainsKey("outline-width")) values["outline-width"] = width ?? "medium";
    }

    private static bool IsOutlineWidth(string token)
    {
        if (token.ToLowerInvariant() is "thin" or "medium" or "thick") return true;
        if (double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
        {
            return number == 0;
        }
        return CssLengthParser.TryParse(token, out var length)
               && length is { Unit: CssLayoutLengthUnit.Pixel, Value: >= 0 };
    }

    private static bool IsOutlineStyle(string token)
        => token.ToLowerInvariant() is "auto" or "none" or "hidden" or "dotted" or "dashed" or "solid" or "double" or
            "groove" or "ridge" or "inset" or "outset";

    private static bool IsPotentialOutlineColor(string token)
        => token.ToLowerInvariant() is "currentcolor" or "transparent"
           || CssColorParser.TryParseColor(token, out _)
           || (token.Length > 0 && (char.IsLetter(token[0]) || token[0] is '-' or '_'));

    private static void ExpandBorderRadius(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("border-radius", out var shorthand)) return;

        var slash = FindTopLevelSlash(shorthand);
        var horizontal = SplitTokens(slash < 0 ? shorthand : shorthand[..slash]);
        var vertical = slash < 0 ? horizontal : SplitTokens(shorthand[(slash + 1)..]);
        if (horizontal.Count is < 1 or > 4 || vertical.Count is < 1 or > 4) return;

        var horizontalCorners = ExpandCorners(horizontal);
        var verticalCorners = ExpandCorners(vertical);
        var names = new[]
        {
            "border-top-left-radius",
            "border-top-right-radius",
            "border-bottom-right-radius",
            "border-bottom-left-radius"
        };
        for (var index = 0; index < names.Length; index++)
        {
            // The cascade may already have selected an explicit corner
            // longhand over this shorthand. The cascade-aware caller expands
            // var()-backed shorthands into corner winners before reaching here.
            if (values.ContainsKey(names[index])) continue;
            values[names[index]] = string.Equals(
                horizontalCorners[index],
                verticalCorners[index],
                StringComparison.Ordinal)
                ? horizontalCorners[index]
                : $"{horizontalCorners[index]} {verticalCorners[index]}";
        }

        static string[] ExpandCorners(IReadOnlyList<string> tokens)
            =>
            [
                tokens[0],
                tokens.Count > 1 ? tokens[1] : tokens[0],
                tokens.Count > 2 ? tokens[2] : tokens[0],
                tokens.Count > 3 ? tokens[3] : tokens.Count > 1 ? tokens[1] : tokens[0]
            ];

        static int FindTopLevelSlash(string value)
        {
            var depth = 0;
            for (var index = 0; index < value.Length; index++)
            {
                if (value[index] == '(') depth++;
                else if (value[index] == ')') depth = Math.Max(0, depth - 1);
                else if (value[index] == '/' && depth == 0) return index;
            }
            return -1;
        }
    }

    private static void ExpandBox(CssPropertyValueStore values, string shorthand, string? prefix = null, string? suffix = null)
    {
        if (!values.TryGetValue(shorthand, out var value)) return;
        prefix ??= shorthand;
        var names = new[] { "top", "right", "bottom", "left" }
            .Select(side => suffix is null ? $"{prefix}-{side}" : $"{prefix}-{side}-{suffix}")
            .ToArray();
        ExpandBoxTokens(values, SplitTokens(value), names);
    }

    private static void ExpandBoxTokens(CssPropertyValueStore values, IReadOnlyList<string> tokens, params string[] names)
    {
        if (tokens.Count is < 1 or > 4 || names.Length != 4) return;
        var top = tokens[0];
        var right = tokens.Count > 1 ? tokens[1] : top;
        var bottom = tokens.Count > 2 ? tokens[2] : top;
        var left = tokens.Count > 3 ? tokens[3] : right;
        values[names[0]] = top;
        values[names[1]] = right;
        values[names[2]] = bottom;
        values[names[3]] = left;
    }

    private static List<string> SplitTokens(string value)
    {
        var result = new List<string>();
        var current = new StringBuilder();
        var depth = 0;
        foreach (var ch in value)
        {
            if (ch == '(') depth++;
            else if (ch == ')') depth--;
            if (char.IsWhiteSpace(ch) && depth == 0)
            {
                if (current.Length == 0) continue;
                result.Add(current.ToString());
                current.Clear();
            }
            else current.Append(ch);
        }
        if (current.Length > 0) result.Add(current.ToString());
        return result;
    }

    private static bool IsNumber(string value)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out _);
}
