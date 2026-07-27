using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using Avalonia.Metadata;

namespace WebScene;

public class p : TextBlock
{
    protected override Type StyleKeyOverride => typeof(p);
    
    public static readonly DirectProperty<p, string?> idProperty =
        NameProperty.AddOwner<p>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<p>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<p>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<p>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<p>();

    static p()
    {
        classProperty.Changed.AddClassHandler<p>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<p>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<p>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<p>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public p()
    {
        DockPanel.SetDock(this, Dock.Top);
        FontWeight = FontWeight.Normal;
        FontSize = 16;
    }

    [Content]
    public InlineCollection content => Inlines;

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
}
