using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using AngleSharp.Css.Dom;
using AngleSharp.Css.Parser;

namespace WebScene;

internal static class CssConverter
{
    public static string? ConvertCssToXamlStyles(string css)
    {
        if (string.IsNullOrWhiteSpace(css))
            return null;

        try
        {
            var parser = new CssParser();
            var sheet = parser.ParseStyleSheet(css);
            if (sheet is null || sheet.Rules.Length == 0)
                return null;

            var sb = new StringBuilder();
            sb.Append("<Styles xmlns=\"https://github.com/avaloniaui\">\n");

            foreach (var rule in sheet.Rules)
            {
                if (rule is ICssStyleRule styleRule)
                {
                    var selectorText = styleRule.SelectorText;
                    if (string.IsNullOrWhiteSpace(selectorText))
                        continue;

                    var selectors = selectorText.Split(',').Select(s => s.Trim()).Where(s => s.Length > 0);
                    foreach (var sel in selectors)
                    {
                        var selector = MapSelectorString(sel);
                        if (string.IsNullOrWhiteSpace(selector))
                            continue;

                        sb.Append("  <Style Selector=\"");
                        sb.Append(EscapeAttribute(selector));
                        sb.Append("\">\n");

                        var decl = styleRule.Style;
                        foreach (var prop in decl)
                        {
                            var name = prop.Name;
                            var val = decl.GetPropertyValue(name) ?? string.Empty;
                            if (TryMapProperty(name, val, out var avaloniaProp, out var avaloniaVal))
                            {
                                sb.Append("    <Setter Property=\"");
                                sb.Append(EscapeAttribute(avaloniaProp));
                                sb.Append("\" Value=\"");
                                sb.Append(EscapeAttribute(avaloniaVal));
                                sb.Append("\"/>\n");
                            }
                        }

                        sb.Append("  </Style>\n");
                    }
                }
            }

            sb.Append("</Styles>\n");
            return sb.ToString();
        }
        catch
        {
            // Fallback: try simple conversion
            return ConvertCssToXamlStyles_Fallback(css);
        }
    }

    private static string? ConvertCssToXamlStyles_Fallback(string css)
    {
        css = RemoveComments(css);
        var rules = ParseRules(css);
        if (rules.Count == 0)
            return null;
        var sb = new StringBuilder();
        sb.Append("<Styles xmlns=\"https://github.com/avaloniaui\">\n");
        foreach (var rule in rules)
        {
            foreach (var sel in rule.Selectors)
            {
                var selector = MapSelectorString(sel);
                if (string.IsNullOrWhiteSpace(selector))
                    continue;
                sb.Append("  <Style Selector=\"");
                sb.Append(EscapeAttribute(selector));
                sb.Append("\">\n");
                foreach (var (prop, val) in rule.Declarations)
                {
                    if (TryMapProperty(prop, val, out var avaloniaProp, out var avaloniaVal))
                    {
                        sb.Append("    <Setter Property=\"");
                        sb.Append(EscapeAttribute(avaloniaProp));
                        sb.Append("\" Value=\"");
                        sb.Append(EscapeAttribute(avaloniaVal));
                        sb.Append("\"/>\n");
                    }
                }
                sb.Append("  </Style>\n");
            }
        }
        sb.Append("</Styles>\n");
        return sb.ToString();
    }

    private static string RemoveComments(string css)
    {
        var sb = new StringBuilder(css.Length);
        for (int i = 0; i < css.Length; i++)
        {
            if (i + 1 < css.Length && css[i] == '/' && css[i + 1] == '*')
            {
                i += 2;
                while (i + 1 < css.Length && !(css[i] == '*' && css[i + 1] == '/')) i++;
                i++;
                continue;
            }
            sb.Append(css[i]);
        }
        return sb.ToString();
    }

    private sealed class CssRule
    {
        public List<string> Selectors { get; } = new();
        public List<(string, string)> Declarations { get; } = new();
    }

    private static List<CssRule> ParseRules(string css)
    {
        var rules = new List<CssRule>();
        int i = 0;
        while (i < css.Length)
        {
            // read selector
            SkipWhitespace(css, ref i);
            int selStart = i;
            while (i < css.Length && css[i] != '{') i++;
            if (i >= css.Length) break;
            var selectorText = css.Substring(selStart, i - selStart).Trim();
            i++; // skip '{'

            // read declarations until '}'
            int declStart = i;
            while (i < css.Length && css[i] != '}') i++;
            var declText = (i <= css.Length ? css.Substring(declStart, i - declStart) : string.Empty);
            if (i < css.Length) i++; // skip '}'

            var rule = new CssRule();
            foreach (var s in selectorText.Split(',').Select(s => s.Trim()).Where(s => s.Length > 0))
                rule.Selectors.Add(s);

            foreach (var line in declText.Split(';'))
            {
                var kv = line.Split(':');
                if (kv.Length < 2) continue;
                var key = kv[0].Trim();
                var val = string.Join(":", kv.Skip(1)).Trim();
                if (key.Length == 0 || val.Length == 0) continue;
                rule.Declarations.Add((key, val));
            }

            if (rule.Selectors.Count > 0 && rule.Declarations.Count > 0)
                rules.Add(rule);
        }
        return rules;
    }

    private static void SkipWhitespace(string s, ref int i)
    {
        while (i < s.Length && char.IsWhiteSpace(s[i])) i++;
    }

    private static string MapSelectorString(string cssSelector)
    {
        // Map a possibly complex selector by splitting on combinators, mapping simple parts, and rejoining.
        var sb = new StringBuilder();
        int i = 0;
        while (i < cssSelector.Length)
        {
            if (char.IsWhiteSpace(cssSelector[i]))
            {
                // collapse whitespace to a single descendant combinator
                while (i < cssSelector.Length && char.IsWhiteSpace(cssSelector[i])) i++;
                sb.Append(' ');
                continue;
            }
            var c = cssSelector[i];
            if (c == '>' || c == '+' || c == '~' || c == ',')
            {
                // map all combinators to descendant for Avalonia selector simplicity
                sb.Append(' ');
                i++;
                continue;
            }

            int start = i;
            while (i < cssSelector.Length && !char.IsWhiteSpace(cssSelector[i]) && cssSelector[i] != '>' && cssSelector[i] != '+' && cssSelector[i] != '~' && cssSelector[i] != ',') i++;
            var part = cssSelector.Substring(start, i - start);
            var mapped = MapSimpleSelector(part);
            if (!string.IsNullOrWhiteSpace(mapped))
            {
                sb.Append(mapped);
            }
        }
        return sb.ToString().Trim();
    }

    private static string MapSimpleSelector(string cssSelector)
    {
        string type = string.Empty;
        var classes = new List<string>();
        string id = string.Empty;

        int i = 0;
        while (i < cssSelector.Length)
        {
            char c = cssSelector[i];
            if (c == '.')
            {
                i++;
                var cls = ReadIdent(cssSelector, ref i);
                if (!string.IsNullOrWhiteSpace(cls)) classes.Add(cls);
            }
            else if (c == '#')
            {
                i++;
                id = ReadIdent(cssSelector, ref i);
            }
            else if (c == '[')
            {
                // attribute selector - not mapped currently; skip until closing ']'
                i++;
                while (i < cssSelector.Length && cssSelector[i] != ']') i++;
                if (i < cssSelector.Length) i++;
            }
            else if (c == ':')
            {
                // pseudo-class/element - ignore
                i++;
                while (i < cssSelector.Length && (char.IsLetter(cssSelector[i]) || cssSelector[i] == '-')) i++;
            }
            else
            {
                // type name
                type = ReadIdent(cssSelector, ref i);
            }
        }

        var target = MapTypeToAvalonia(type);
        var sb = new StringBuilder();
        if (!string.IsNullOrEmpty(target))
            sb.Append(target);
        if (!string.IsNullOrEmpty(id))
        {
            sb.Append('#');
            sb.Append(id);
        }
        foreach (var cls in classes)
        {
            sb.Append('.');
            sb.Append(cls);
        }
        return sb.ToString();
    }

    private static string ReadIdent(string s, ref int i)
    {
        int start = i;
        while (i < s.Length && (char.IsLetterOrDigit(s[i]) || s[i] == '_' || s[i] == '-')) i++;
        return s.Substring(start, i - start);
    }

    private static string MapTypeToAvalonia(string type)
    {
        if (string.IsNullOrWhiteSpace(type))
            return string.Empty; // class-only or id-only selector

        type = type.ToLowerInvariant();
        return type switch
        {
            "h1" or "h2" or "h3" or "h4" or "h5" or "h6" or "p" => "TextBlock",
            "span" or "strong" or "em" or "code" or "a" => "TextElement",
            "div" or "body" or "li" => "DockPanel",
            "ul" or "ol" or "section" or "header" or "footer" or "nav" or "article" or "aside" => "StackPanel",
            "img" => "Image",
            "hr" => "Separator",
            _ => type // fallback to as-is
        };
    }

    private static bool TryMapProperty(string cssProp, string cssVal, out string avaloniaProp, out string avaloniaVal)
    {
        avaloniaProp = string.Empty;
        avaloniaVal = string.Empty;

        var key = cssProp.Trim().ToLowerInvariant();
        switch (key)
        {
            case "color":
                avaloniaProp = "Foreground";
                avaloniaVal = cssVal.Trim();
                return true;
            case "background":
            case "background-color":
                avaloniaProp = "Background";
                avaloniaVal = cssVal.Trim();
                return true;
            case "font-size":
                avaloniaProp = "FontSize";
                avaloniaVal = NormalizeNumber(cssVal);
                return true;
            case "font-weight":
                avaloniaProp = "FontWeight";
                avaloniaVal = MapFontWeight(cssVal);
                return true;
            case "text-align":
                avaloniaProp = "TextAlignment";
                avaloniaVal = MapTextAlign(cssVal);
                return true;
            case "margin":
                avaloniaProp = "Margin";
                avaloniaVal = MapThickness(cssVal);
                return true;
            case "padding":
                avaloniaProp = "Padding";
                avaloniaVal = MapThickness(cssVal);
                return true;
            case "width":
                avaloniaProp = "Width";
                avaloniaVal = NormalizeNumber(cssVal);
                return true;
            case "height":
                avaloniaProp = "Height";
                avaloniaVal = NormalizeNumber(cssVal);
                return true;
            default:
                return false;
        }
    }

    private static string NormalizeNumber(string s)
    {
        s = s.Trim();
        if (s.EndsWith("px", StringComparison.OrdinalIgnoreCase))
            s = s[..^2];
        // Avalonia XAML expects dot decimal separator
        if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var d))
            return d.ToString(CultureInfo.InvariantCulture);
        return s;
    }

    private static string MapFontWeight(string s)
    {
        s = s.Trim().ToLowerInvariant();
        return s switch
        {
            "normal" => "Normal",
            "bold" => "Bold",
            "bolder" => "Bold",
            "lighter" => "Light",
            "100" => "Thin",
            "200" => "ExtraLight",
            "300" => "Light",
            "400" => "Normal",
            "500" => "Medium",
            "600" => "SemiBold",
            "700" => "Bold",
            "800" => "ExtraBold",
            "900" => "Black",
            _ => s
        };
    }

    private static string MapTextAlign(string s)
    {
        s = s.Trim().ToLowerInvariant();
        return s switch
        {
            "left" => "Left",
            "center" => "Center",
            "right" => "Right",
            "justify" => "Justify",
            _ => s
        };
    }

    private static string MapThickness(string s)
    {
        // CSS: [all] | [v h] | [t h b] | [t r b l]
        var parts = s.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)
                     .Select(x => NormalizeNumber(x))
                     .ToArray();
        string t, r, b, l;
        switch (parts.Length)
        {
            case 1:
                t = r = b = l = parts[0];
                break;
            case 2:
                t = b = parts[0];
                r = l = parts[1];
                break;
            case 3:
                t = parts[0];
                r = l = parts[1];
                b = parts[2];
                break;
            default:
                t = parts.ElementAtOrDefault(0) ?? "0";
                r = parts.ElementAtOrDefault(1) ?? "0";
                b = parts.ElementAtOrDefault(2) ?? "0";
                l = parts.ElementAtOrDefault(3) ?? "0";
                break;
        }
        // Avalonia Thickness: left,top,right,bottom
        return string.Join(',', new[] { l, t, r, b });
    }

    private static string EscapeAttribute(string s)
    {
        return s.Replace("\"", "&quot;");
    }
}
