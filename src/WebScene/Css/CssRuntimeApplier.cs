using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Media;
using AngleSharp.Css.Dom;
using AngleSharp.Css.Parser;

namespace WebScene;

internal static class CssRuntimeApplier
{
    private sealed class SelectorSegment
    {
        public string? TypeToken;
        public string? Id;
        public List<string> Classes = new();
    }

    private sealed class Rule
    {
        public List<SelectorSegment> Segments = new();
        public List<(string name, string value)> Decls = new();
    }

    public static void ApplyCss(string css, Control root)
    {
        var rules = ParseCss(css);
        if (rules.Count == 0)
            return;

        foreach (var ctrl in Traverse(root))
        {
            foreach (var r in rules)
            {
                if (Matches(ctrl, r))
                {
                    ApplyDecls(ctrl, r.Decls);
                }
            }

            // Extend styling into inline TextElements for any TextBlock-like control
            if (ctrl is TextBlock tb)
            {
                foreach (var te in TraverseInlines(tb))
                {
                    foreach (var r in rules)
                    {
                        if (MatchesInline(te, r))
                        {
                            ApplyDeclsInline(te, r.Decls);
                        }
                    }
                }
            }
        }

        // Ensure inline style attributes take precedence over stylesheet rules
        ReapplyInlineStyles(root);
    }

    private static List<Rule> ParseCss(string css)
    {
        var list = new List<Rule>();
        try
        {
            var parser = new CssParser();
            var sheet = parser.ParseStyleSheet(css);
            if (sheet is null) return list;
            foreach (var rule in sheet.Rules)
            {
                if (rule is ICssStyleRule sr)
                {
                    var selectors = sr.SelectorText.Split(',').Select(s => s.Trim()).Where(s => s.Length > 0);
                    foreach (var sel in selectors)
                    {
                        var segments = ParseSelectorSegments(sel);
                        if (segments is null || segments.Count == 0) continue;
                        var ruleObj = new Rule { Segments = segments };
                        foreach (var prop in sr.Style)
                        {
                            var name = prop.Name;
                            var val = sr.Style.GetPropertyValue(name) ?? string.Empty;
                            ruleObj.Decls.Add((name, val));
                        }
                        list.Add(ruleObj);
                    }
                }
            }
        }
        catch
        {
            // ignore parse errors
        }
        return list;
    }

    private static List<SelectorSegment>? ParseSelectorSegments(string cssSelector)
    {
        var segments = new List<SelectorSegment>();
        int i = 0;
        while (i < cssSelector.Length)
        {
            // skip whitespace/combinators between segments
            while (i < cssSelector.Length && (char.IsWhiteSpace(cssSelector[i]) || cssSelector[i] == '>' || cssSelector[i] == '+' || cssSelector[i] == '~')) i++;
            if (i >= cssSelector.Length) break;
            int start = i;
            while (i < cssSelector.Length && !char.IsWhiteSpace(cssSelector[i]) && cssSelector[i] != '>' && cssSelector[i] != '+' && cssSelector[i] != '~' && cssSelector[i] != ',') i++;
            var part = cssSelector.Substring(start, i - start);

            string type = string.Empty;
            var classes = new List<string>();
            string id = string.Empty;
            int j = 0;
            while (j < part.Length)
            {
                char c = part[j];
                if (c == '.') { j++; var cls = ReadIdent(part, ref j); if (!string.IsNullOrWhiteSpace(cls)) classes.Add(cls); }
                else if (c == '#') { j++; id = ReadIdent(part, ref j); }
                else if (c == '[') { j++; while (j < part.Length && part[j] != ']') j++; if (j < part.Length) j++; }
                else if (c == ':') { j++; while (j < part.Length && (char.IsLetter(part[j]) || part[j] == '-')) j++; }
                else { type = ReadIdent(part, ref j); }
            }
            var token = MapTypeToAvalonia(type);
            if (string.IsNullOrEmpty(token) && classes.Count == 0 && string.IsNullOrEmpty(id))
                continue;
            segments.Add(new SelectorSegment { TypeToken = token, Id = id, Classes = classes });
        }
        return segments;
    }

    private static string ReadIdent(string s, ref int i)
    {
        int start = i;
        while (i < s.Length && (char.IsLetterOrDigit(s[i]) || s[i] == '_' || s[i] == '-')) i++;
        return s.Substring(start, i - start);
    }

    private static IEnumerable<Control> Traverse(Control root)
    {
        yield return root;
        if (root is Panel panel)
        {
            foreach (var child in panel.Children)
            {
                if (child is Control c)
                {
                    foreach (var x in Traverse(c)) yield return x;
                }
            }
        }
        else if (root is ContentControl cc && cc.Content is Control qc)
        {
            foreach (var x in Traverse(qc)) yield return x;
        }
        else if (root is Decorator dec && dec.Child is Control child)
        {
            foreach (var x in Traverse(child)) yield return x;
        }
    }

    private static string GetTypeToken(Control c)
    {
        // Return semantic html-like tokens for known text elements
        var n = c.GetType().Name;
        if (n is "h1" or "h2" or "h3" or "h4" or "h5" or "h6" or "p")
            return n;
        if (c is TextBlock) return "TextBlock";
        if (c is DockPanel) return "DockPanel";
        if (c is StackPanel) return "StackPanel";
        if (c is Image) return "Image";
        if (c is Separator) return "Separator";
        return c.GetType().Name;
    }

    private static bool Matches(Control c, Rule r)
    {
        if (r.Segments.Count == 0)
            return false;
        // Match last segment on current control
        if (!MatchesSimple(c, GetTypeToken(c), r.Segments[^1]))
            return false;
        // Walk ancestors for previous segments (descendant semantics)
        int si = r.Segments.Count - 2;
        var parent = c.Parent as Control;
        while (si >= 0 && parent is not null)
        {
            if (MatchesSimple(parent, GetTypeToken(parent), r.Segments[si]))
                si--;
            parent = parent.Parent as Control;
        }
        return si < 0;
    }

    private static bool MatchesSimple(Control c, string token, SelectorSegment seg)
    {
        if (!string.IsNullOrEmpty(seg.TypeToken) && !string.Equals(seg.TypeToken, token, StringComparison.Ordinal))
            return false;
        if (!string.IsNullOrEmpty(seg.Id))
        {
            if (!string.Equals(c.Name, seg.Id, StringComparison.Ordinal)) return false;
        }
        foreach (var cls in seg.Classes)
        {
            if (!c.Classes.Contains(cls)) return false;
        }
        return true;
    }

    // Reapply inline styles declared via style="..." so they win over CSS rules
    private static void ReapplyInlineStyles(Control root)
    {
        foreach (var ctrl in Traverse(root))
        {
            string? s = GetInlineStyleForControl(ctrl);
            if (!string.IsNullOrWhiteSpace(s))
            {
                HtmlElementBase.ApplyStyles(ctrl, s);
            }

            if (ctrl is TextBlock tb)
            {
                foreach (var te in TraverseInlines(tb))
                {
                    var inlineStyle = GetInlineStyleForTextElement(te);
                    if (!string.IsNullOrWhiteSpace(inlineStyle))
                    {
                        HtmlElementBase.ApplyStyles(te, inlineStyle);
                    }
                }
            }
        }
    }

    private static string? GetInlineStyleForControl(Control c)
    {
        return c switch
        {
            WebScene.body v => v.style,
            WebScene.div v => v.style,
            WebScene.p v => v.style,
            WebScene.h1 v => v.style,
            WebScene.h2 v => v.style,
            WebScene.h3 v => v.style,
            WebScene.h4 v => v.style,
            WebScene.h5 v => v.style,
            WebScene.h6 v => v.style,
            WebScene.ul v => v.style,
            WebScene.ol v => v.style,
            WebScene.li v => v.style,
            WebScene.img v => v.style,
            WebScene.hr v => v.style,
            WebScene.section v => v.style,
            WebScene.header v => v.style,
            WebScene.footer v => v.style,
            WebScene.nav v => v.style,
            WebScene.article v => v.style,
            WebScene.aside v => v.style,
            WebScene.canvas v => v.style,
            _ => null
        };
    }

    private static string? GetInlineStyleForTextElement(Avalonia.Controls.Documents.TextElement te)
    {
        return te switch
        {
            WebScene.span v => v.style,
            WebScene.strong v => v.style,
            WebScene.em v => v.style,
            WebScene.code v => v.style,
            WebScene.a v => v.style,
            _ => null
        };
    }

    // Inline matching/styling
    private static IEnumerable<Avalonia.Controls.Documents.TextElement> TraverseInlines(TextBlock tb)
    {
        foreach (var inline in tb.Inlines)
        {
            foreach (var te in TraverseInlineRecursive(inline))
                yield return te;
        }
    }

    private static IEnumerable<Avalonia.Controls.Documents.TextElement> TraverseInlineRecursive(Avalonia.Controls.Documents.Inline inline)
    {
        if (inline is Avalonia.Controls.Documents.TextElement te)
            yield return te;
        if (inline is Avalonia.Controls.Documents.Span sp)
        {
            foreach (var child in sp.Inlines)
            {
                foreach (var te2 in TraverseInlineRecursive(child))
                    yield return te2;
            }
        }
    }

    private static string GetInlineTypeToken(Avalonia.Controls.Documents.TextElement te)
    {
        var n = te.GetType().Name;
        // Map our WebScene inline types directly
        switch (n)
        {
            case "span":
            case "strong":
            case "em":
            case "code":
            case "a":
                return n;
            default:
                return string.Empty;
        }
    }

    private static bool MatchesInline(Avalonia.Controls.Documents.TextElement te, Rule r)
    {
        if (r.Segments.Count == 0)
            return false;
        var last = r.Segments[^1];
        if (!MatchesInlineSimple(te, last))
            return false;

        // Walk ancestors through owning TextBlock first, then Spans
        int si = r.Segments.Count - 2;
        Avalonia.Controls.Documents.Span? parentSpan = te.Parent as Avalonia.Controls.Documents.Span;
        Control? parentControl = te.Parent as Control;
        while (si >= 0)
        {
            var seg = r.Segments[si];
            bool matched = false;
            if (parentSpan is not null)
            {
                // inline ancestor
                if (MatchesInlineSpanSimple(parentSpan, seg))
                {
                    si--; matched = true;
                }
                parentSpan = parentSpan.Parent as Avalonia.Controls.Documents.Span;
                if (parentSpan is null && parentControl is null)
                {
                    parentControl = (te.Parent as Avalonia.Controls.Documents.Inline)?.Parent as Control;
                }
            }
            else if (parentControl is not null)
            {
                if (MatchesSimple(parentControl, GetTypeToken(parentControl), seg))
                {
                    si--; matched = true;
                }
                parentControl = parentControl.Parent as Control;
            }
            else break;
            if (!matched)
            {
                // keep climbing without consuming segment
                continue;
            }
        }
        return si < 0;
    }

    private static bool MatchesInlineSimple(Avalonia.Controls.Documents.TextElement te, SelectorSegment seg)
    {
        var token = GetInlineTypeToken(te);
        if (!string.IsNullOrEmpty(seg.TypeToken) && !string.Equals(seg.TypeToken, token, StringComparison.Ordinal))
            return false;
        // id
        var name = (te as Avalonia.StyledElement)?.Name;
        if (!string.IsNullOrEmpty(seg.Id) && !string.Equals(name, seg.Id, StringComparison.Ordinal))
            return false;
        // classes
        var classes = (te as Avalonia.StyledElement)?.Classes;
        if (classes is not null)
        {
            foreach (var cls in seg.Classes)
            {
                if (!classes.Contains(cls)) return false;
            }
        }
        return true;
    }

    private static bool MatchesInlineSpanSimple(Avalonia.Controls.Documents.Span sp, SelectorSegment seg)
    {
        var token = GetInlineTypeToken(sp);
        if (!string.IsNullOrEmpty(seg.TypeToken) && !string.Equals(seg.TypeToken, token, StringComparison.Ordinal))
            return false;
        var name = (sp as Avalonia.StyledElement)?.Name;
        if (!string.IsNullOrEmpty(seg.Id) && !string.Equals(name, seg.Id, StringComparison.Ordinal))
            return false;
        var classes = (sp as Avalonia.StyledElement)?.Classes;
        if (classes is not null)
        {
            foreach (var cls in seg.Classes)
            {
                if (!classes.Contains(cls)) return false;
            }
        }
        return true;
    }

    private static void ApplyDecls(Control c, List<(string name, string value)> decls)
    {
        foreach (var (name, value) in decls)
        {
            switch (name.Trim().ToLowerInvariant())
            {
                case "color":
                    if (c is TextBlock tb && TryParseBrush(value, out var fg)) tb.Foreground = fg;
                    break;
                case "background":
                case "background-color":
                    if (TryParseBrush(value, out var bg))
                    {
                        if (c is Panel p) p.Background = bg;
                        else if (c is TemplatedControl tc) tc.Background = bg;
                        else if (c is Border bd) bd.Background = bg;
                        else if (c is canvas canv) canv.Background = bg;
                    }
                    break;
                case "font-size":
                    if (c is TextBlock tbf && TryParseDouble(value, out var fs)) tbf.FontSize = fs;
                    break;
                case "font-weight":
                    if (c is TextBlock tbw && TryParseFontWeight(value, out var fw)) tbw.FontWeight = fw;
                    break;
                case "text-align":
                    if (c is TextBlock tba && TryParseTextAlign(value, out var ta)) tba.TextAlignment = ta;
                    break;
                case "text-decoration":
                    if (c is TextBlock tbd)
                    {
                        var v = value.Trim().ToLowerInvariant();
                        if (v.Contains("underline")) tbd.TextDecorations = Avalonia.Media.TextDecorations.Underline;
                        else if (v.Contains("none")) tbd.TextDecorations = null;
                    }
                    break;
                case "margin":
                    if (TryParseThickness(value, out var m)) c.Margin = m;
                    break;
                case "padding":
                    if (c is ContentControl contentCtrl && TryParseThickness(value, out var pad)) contentCtrl.Padding = pad;
                    else if (c is Border bor && TryParseThickness(value, out var pad2)) bor.Padding = pad2;
                    break;
                case "width":
                    if (TryParseDouble(value, out var w)) c.Width = w;
                    break;
                case "height":
                    if (TryParseDouble(value, out var h)) c.Height = h;
                    break;
                case "line-height":
                    if (c is TextBlock tbl && TryParseDouble(value, out var lh)) tbl.LineHeight = lh;
                    break;
                case "border":
                    ApplyBorder(c, value);
                    break;
                case "border-color":
                    if (c is TemplatedControl tcc && TryParseBrush(value, out var bb)) tcc.BorderBrush = bb;
                    else if (c is Border bbc && TryParseBrush(value, out var bbb)) bbc.BorderBrush = bbb;
                    break;
                case "border-width":
                    if (c is TemplatedControl tcw && TryParseThickness(value, out var bt)) tcw.BorderThickness = bt;
                    else if (c is Border btw && TryParseThickness(value, out var bt2)) btw.BorderThickness = bt2;
                    break;
                case "border-radius":
                    if (c is Border crb && TryParseCornerRadius(value, out var cr)) crb.CornerRadius = cr;
                    break;
                case "visibility":
                    ApplyVisibility(c, value);
                    break;
                case "display":
                    ApplyDisplay(c, value);
                    break;
            }
        }
    }

    private static void ApplyDeclsInline(Avalonia.Controls.Documents.TextElement te, List<(string name, string value)> decls)
    {
        foreach (var (name, value) in decls)
        {
            switch (name.Trim().ToLowerInvariant())
            {
                case "color":
                    if (TryParseBrush(value, out var fg)) te.Foreground = fg;
                    break;
                case "background":
                case "background-color":
                    if (TryParseBrush(value, out var bg)) te.Background = bg;
                    break;
                case "font-size":
                    if (TryParseDouble(value, out var fs)) te.FontSize = fs;
                    break;
                case "font-weight":
                    if (TryParseFontWeight(value, out var fw)) te.FontWeight = fw;
                    break;
                case "font-style":
                    {
                        var v = value.Trim().ToLowerInvariant();
                        te.FontStyle = v == "italic" ? Avalonia.Media.FontStyle.Italic : Avalonia.Media.FontStyle.Normal;
                    }
                    break;
                case "font-family":
                    te.FontFamily = new FontFamily(value.Trim());
                    break;
                // text-decoration not supported on TextElement in this Avalonia version
                // underline can still be applied at TextBlock level via block selector
                case "text-decoration":
                    break;
            }
        }
    }

    private static void ApplyBorder(Control c, string value)
    {
        // very simple: try to find a color and a width token
        if (c is TemplatedControl tc)
        {
            if (TryParseBrush(value, out var brush)) tc.BorderBrush = brush;
        }
        else if (c is Border bd)
        {
            if (TryParseBrush(value, out var brush)) bd.BorderBrush = brush;
        }
        // extract first length
        var parts = value.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
        foreach (var p in parts)
        {
            if (TryParseThickness(p, out var th))
            {
                if (c is TemplatedControl tc2) { tc2.BorderThickness = th; break; }
                if (c is Border bd2) { bd2.BorderThickness = th; break; }
            }
            if (TryParseDouble(p, out var d))
            {
                if (c is TemplatedControl tc3) { tc3.BorderThickness = new Thickness(d); break; }
                if (c is Border bd3) { bd3.BorderThickness = new Thickness(d); break; }
            }
        }
    }

    private static void ApplyVisibility(Control c, string value)
    {
        var v = value.Trim().ToLowerInvariant();
        if (v == "hidden")
        {
            c.Opacity = 0;
            c.IsHitTestVisible = false;
        }
        else if (v == "visible")
        {
            c.Opacity = 1;
            c.IsHitTestVisible = true;
        }
    }

    private static void ApplyDisplay(Control c, string value)
    {
        var v = value.Trim().ToLowerInvariant();
        if (v == "none")
        {
            c.Opacity = 0;
            c.IsHitTestVisible = false;
            c.Width = 0;
            c.Height = 0;
            c.Margin = new Thickness(0);
        }
        else
        {
            // best effort restore
            c.Opacity = 1;
            c.IsHitTestVisible = true;
        }
    }

    private static string MapTypeToAvalonia(string type)
    {
        if (string.IsNullOrWhiteSpace(type))
            return string.Empty;
        switch (type.ToLowerInvariant())
        {
            case "h1":
            case "h2":
            case "h3":
            case "h4":
            case "h5":
            case "h6":
            case "p":
                return type.ToLowerInvariant();
            case "span":
            case "strong":
            case "em":
            case "code":
            case "a":
                return type.ToLowerInvariant();
            case "div":
            case "body":
            case "li":
                return "DockPanel";
            case "ul":
            case "ol":
            case "section":
            case "header":
            case "footer":
            case "nav":
            case "article":
            case "aside":
                return "StackPanel";
            case "img": return "Image";
            case "hr": return "Separator";
            default: return type;
        }
    }

    private static bool TryParseBrush(string s, out IBrush brush)
    {
        s = s.Trim();
        // Named CSS colors
        if (TryMapCssColorName(s, out var hex))
        {
            try { brush = Brush.Parse(hex); return true; } catch { }
        }
        // rgb()/rgba()
        if (TryParseRgbFunctions(s, out var color))
        {
            brush = new SolidColorBrush(color);
            return true;
        }
        try
        {
            brush = Brush.Parse(s);
            return true;
        }
        catch
        {
            brush = Brushes.Transparent;
            return false;
        }
    }

    internal static bool TryMapCssColorName(string s, out string hex)
    {
        hex = string.Empty;
        switch (s.ToLowerInvariant())
        {
            case "black": hex = "#000000"; return true;
            case "white": hex = "#FFFFFF"; return true;
            case "red": hex = "#FF0000"; return true;
            case "lime": hex = "#00FF00"; return true;
            case "blue": hex = "#0000FF"; return true;
            case "yellow": hex = "#FFFF00"; return true;
            case "cyan":
            case "aqua": hex = "#00FFFF"; return true;
            case "magenta":
            case "fuchsia": hex = "#FF00FF"; return true;
            case "gray":
            case "grey": hex = "#808080"; return true;
            case "silver": hex = "#C0C0C0"; return true;
            case "maroon": hex = "#800000"; return true;
            case "olive": hex = "#808000"; return true;
            case "green": hex = "#008000"; return true;
            case "teal": hex = "#008080"; return true;
            case "navy": hex = "#000080"; return true;
            case "purple": hex = "#800080"; return true;
            case "orange": hex = "#FFA500"; return true;
            default: return false;
        }
    }

    internal static bool TryParseRgbFunctions(string s, out Avalonia.Media.Color color)
    {
        color = default;
        try
        {
            if (s.StartsWith("rgb(", StringComparison.OrdinalIgnoreCase) && s.EndsWith(")"))
            {
                var inner = s.Substring(4, s.Length - 5);
                var parts = inner.Split(',');
                if (parts.Length == 3)
                {
                    byte r = ParseByte(parts[0]);
                    byte g = ParseByte(parts[1]);
                    byte b = ParseByte(parts[2]);
                    color = Avalonia.Media.Color.FromRgb(r, g, b);
                    return true;
                }
            }
            else if (s.StartsWith("rgba(", StringComparison.OrdinalIgnoreCase) && s.EndsWith(")"))
            {
                var inner = s.Substring(5, s.Length - 6);
                var parts = inner.Split(',');
                if (parts.Length == 4)
                {
                    byte r = ParseByte(parts[0]);
                    byte g = ParseByte(parts[1]);
                    byte b = ParseByte(parts[2]);
                    double a = double.Parse(parts[3].Trim(), CultureInfo.InvariantCulture);
                    byte aa = (byte)Math.Round(a * 255);
                    color = Avalonia.Media.Color.FromArgb(aa, r, g, b);
                    return true;
                }
            }
        }
        catch { }
        return false;
    }

    private static byte ParseByte(string s)
    {
        s = s.Trim();
        if (s.EndsWith("%", StringComparison.Ordinal))
        {
            var pct = double.Parse(s[..^1], CultureInfo.InvariantCulture) / 100.0;
            return (byte)Math.Clamp((int)Math.Round(pct * 255), 0, 255);
        }
        return (byte)Math.Clamp(int.Parse(s, CultureInfo.InvariantCulture), 0, 255);
    }

    private static bool TryParseDouble(string s, out double d)
    {
        s = s.Trim();
        if (s.EndsWith("px", StringComparison.OrdinalIgnoreCase)) s = s[..^2];
        return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out d);
    }

    private static bool TryParseFontWeight(string s, out Avalonia.Media.FontWeight fw)
    {
        s = s.Trim().ToLowerInvariant();
        switch (s)
        {
            case "normal": fw = Avalonia.Media.FontWeight.Normal; return true;
            case "bold": fw = Avalonia.Media.FontWeight.Bold; return true;
            case "bolder": fw = Avalonia.Media.FontWeight.Bold; return true;
            case "lighter": fw = Avalonia.Media.FontWeight.Light; return true;
            case "100": fw = Avalonia.Media.FontWeight.Thin; return true;
            case "200": fw = Avalonia.Media.FontWeight.ExtraLight; return true;
            case "300": fw = Avalonia.Media.FontWeight.Light; return true;
            case "400": fw = Avalonia.Media.FontWeight.Normal; return true;
            case "500": fw = Avalonia.Media.FontWeight.Medium; return true;
            case "600": fw = Avalonia.Media.FontWeight.SemiBold; return true;
            case "700": fw = Avalonia.Media.FontWeight.Bold; return true;
            case "800": fw = Avalonia.Media.FontWeight.ExtraBold; return true;
            case "900": fw = Avalonia.Media.FontWeight.Black; return true;
            default:
                fw = Avalonia.Media.FontWeight.Normal; return false;
        }
    }

    private static bool TryParseTextAlign(string s, out TextAlignment ta)
    {
        s = s.Trim().ToLowerInvariant();
        switch (s)
        {
            case "left": ta = TextAlignment.Left; return true;
            case "center": ta = TextAlignment.Center; return true;
            case "right": ta = TextAlignment.Right; return true;
            case "justify": ta = TextAlignment.Justify; return true;
            default: ta = TextAlignment.Left; return false;
        }
    }

    private static bool TryParseThickness(string s, out Thickness th)
    {
        var parts = s.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)
                     .Select(x => NormalizeNumber(x))
                     .ToArray();
        string t, r, b, l;
        switch (parts.Length)
        {
            case 1: t = r = b = l = parts[0]; break;
            case 2: t = b = parts[0]; r = l = parts[1]; break;
            case 3: t = parts[0]; r = l = parts[1]; b = parts[2]; break;
            default:
                t = parts.ElementAtOrDefault(0) ?? "0";
                r = parts.ElementAtOrDefault(1) ?? "0";
                b = parts.ElementAtOrDefault(2) ?? "0";
                l = parts.ElementAtOrDefault(3) ?? "0";
                break;
        }
        th = new Thickness(ParseDouble(l), ParseDouble(t), ParseDouble(r), ParseDouble(b));
        return true;
    }

    private static string NormalizeNumber(string s)
    {
        s = s.Trim();
        if (s.EndsWith("px", StringComparison.OrdinalIgnoreCase)) s = s[..^2];
        return s;
    }
    private static double ParseDouble(string s) => double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var d) ? d : 0;

    private static bool TryParseCornerRadius(string s, out CornerRadius cr)
    {
        var parts = s.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)
                     .Select(x => NormalizeNumber(x))
                     .ToArray();
        double tl, tr, br, bl;
        switch (parts.Length)
        {
            case 1:
                tl = tr = br = bl = ParseDouble(parts[0]);
                break;
            case 2:
                tl = br = ParseDouble(parts[0]);
                tr = bl = ParseDouble(parts[1]);
                break;
            case 3:
                tl = ParseDouble(parts[0]);
                tr = bl = ParseDouble(parts[1]);
                br = ParseDouble(parts[2]);
                break;
            default:
                tl = ParseDouble(parts.ElementAtOrDefault(0) ?? "0");
                tr = ParseDouble(parts.ElementAtOrDefault(1) ?? "0");
                br = ParseDouble(parts.ElementAtOrDefault(2) ?? "0");
                bl = ParseDouble(parts.ElementAtOrDefault(3) ?? "0");
                break;
        }
        cr = new CornerRadius(tl, tr, br, bl);
        return true;
    }
}
