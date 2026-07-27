using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Controls.Primitives;

namespace WebScene;

public class HtmlElementBase : StyledElement
{
    public static readonly StyledProperty<string?> classProperty =
        AvaloniaProperty.Register<HtmlElementBase, string?>(nameof(@class));

    public static readonly StyledProperty<string?> styleProperty =
        AvaloniaProperty.Register<HtmlElementBase, string?>(nameof(style));

    public static readonly StyledProperty<bool> disabledProperty =
        AvaloniaProperty.Register<HtmlElementBase, bool>(nameof(disabled));

    public static readonly StyledProperty<string?> titleProperty =
        AvaloniaProperty.Register<HtmlElementBase, string?>(nameof(title));

    public string? @class
    {
        get => GetValue(classProperty);
        set => SetValue(classProperty, value);
    }

    public string? style
    {
        get => GetValue(styleProperty);
        set => SetValue(styleProperty, value);
    }

    public bool disabled
    {
        get => GetValue(disabledProperty);
        set => SetValue(disabledProperty, value);
    }

    public string? title
    {
        get => GetValue(titleProperty);
        set => SetValue(titleProperty, value);
    }

    internal static void ApplyClasses(StyledElement element, string? classes)
    {
        element.Classes.Clear();
        if (string.IsNullOrWhiteSpace(classes))
            return;

        var parts = classes.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
        foreach (var cls in parts)
        {
            element.Classes.Add(cls);
        }
    }

    internal static void ApplyStyles(object element, string? styles)
    {
        if (string.IsNullOrWhiteSpace(styles))
            return;

        foreach (var item in styles.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            var kv = item.Split(':', 2, StringSplitOptions.RemoveEmptyEntries);
            if (kv.Length != 2)
                continue;

            var key = kv[0].Trim().ToLowerInvariant();
            var val = kv[1].Trim();

            switch (key)
            {
                case "font-size":
                    if (element is TextBlock tb && TryParseFontSize(val, out var fs))
                        tb.FontSize = fs;
                    else if (element is Avalonia.Controls.Documents.TextElement te && TryParseFontSize(val, out var fs2))
                        te.FontSize = fs2;
                    break;

                case "color":
                    if (element is TextBlock tbf && TryParseBrush(val, out var fg))
                        tbf.Foreground = fg;
                    else if (element is Avalonia.Controls.Documents.TextElement tef && TryParseBrush(val, out var fgi))
                        tef.Foreground = fgi;
                    break;

                case "background":
                    if (TryParseBrush(val, out var bg))
                    {
                        if (element is Panel p)
                            p.Background = bg;
                        else if (element is Avalonia.Controls.Primitives.TemplatedControl tc)
                            tc.Background = bg;
                        else if (element is canvas cv)
                            cv.Background = bg;
                        else if (element is Avalonia.Controls.Documents.TextElement teBg)
                            teBg.Background = bg;
                    }
                    break;

                case "text-align":
                    if (element is TextBlock tba && TryParseTextAlign(val, out var ta))
                        tba.TextAlignment = ta;
                    break;
            }
        }
    }

    internal static void ApplyDisabled(Control element, bool disabled)
    {
        element.IsEnabled = !disabled;
    }

    internal static void ApplyTitle(Control element, string? title)
    {
        if (title is null)
        {
            ToolTip.SetTip(element, null);
        }
        else
        {
            ToolTip.SetTip(element, title);
        }
    }

    private static bool TryParseFontSize(string s, out double size)
    {
        s = s.Trim().ToLowerInvariant();
        if (s.EndsWith("px", StringComparison.Ordinal))
            s = s[..^2];
        return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out size);
    }

    private static bool TryParseBrush(string s, out IBrush brush)
    {
        s = s.Trim();
        // Basic CSS color names and rgb/rgba support
        if (CssRuntimeApplier.TryParseRgbFunctions(s, out var col))
        {
            brush = new SolidColorBrush(col);
            return true;
        }
        if (CssRuntimeApplier.TryMapCssColorName(s, out var hex))
        {
            try { brush = Brush.Parse(hex); return true; } catch { }
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

    private static bool TryParseTextAlign(string s, out TextAlignment alignment)
    {
        switch (s.Trim().ToLowerInvariant())
        {
            case "left": alignment = TextAlignment.Left; return true;
            case "center": alignment = TextAlignment.Center; return true;
            case "right": alignment = TextAlignment.Right; return true;
            case "justify": alignment = TextAlignment.Justify; return true;
            default:
                alignment = TextAlignment.Left; return false;
        }
    }
}
