using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace WebScene;

public class img : Image
{
    protected override System.Type StyleKeyOverride => typeof(Image);

    public static readonly DirectProperty<img, string?> idProperty =
        NameProperty.AddOwner<img>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<img>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<img>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<img>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<img>();

    public static readonly StyledProperty<string?> srcProperty =
        AvaloniaProperty.Register<img, string?>(nameof(src));

    public static readonly StyledProperty<string?> altProperty =
        AvaloniaProperty.Register<img, string?>(nameof(alt));

    static img()
    {
        classProperty.Changed.AddClassHandler<img>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<img>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<img>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<img>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
        srcProperty.Changed.AddClassHandler<img>((o, e) => o.UpdateSource());
    }

    public img()
    {
        Stretch = Avalonia.Media.Stretch.Uniform;
    }

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

    public string? id
    {
        get => Name;
        set => Name = value;
    }

    public string? src
    {
        get => GetValue(srcProperty);
        set => SetValue(srcProperty, value);
    }

    public string? alt
    {
        get => GetValue(altProperty);
        set => SetValue(altProperty, value);
    }

    private void UpdateSource()
    {
        var s = src;
        if (string.IsNullOrWhiteSpace(s))
        {
            Source = null;
            return;
        }

        try
        {
            if (Uri.TryCreate(s, UriKind.Absolute, out var uri))
            {
                if (uri.Scheme == "avares")
                {
                    using var stream = AssetLoader.Open(uri);
                    Source = new Bitmap(stream);
                    return;
                }
                if (uri.IsFile && File.Exists(uri.LocalPath))
                {
                    Source = new Bitmap(uri.LocalPath);
                    return;
                }
            }

            // Relative path or plain file path
            if (File.Exists(s))
            {
                Source = new Bitmap(s);
                return;
            }
        }
        catch
        {
            // ignore failures and leave Source as-is
        }
    }
}
