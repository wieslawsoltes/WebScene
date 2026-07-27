using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using Avalonia.Metadata;

namespace WebScene;

public class h2 : TextBlock
{
    protected override Type StyleKeyOverride => typeof(h2);

    public static readonly DirectProperty<h2, string?> idProperty =
        NameProperty.AddOwner<h2>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<h2>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<h2>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<h2>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<h2>();

    static h2()
    {
        classProperty.Changed.AddClassHandler<h2>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<h2>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<h2>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<h2>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public h2()
    {
        DockPanel.SetDock(this, Dock.Top);
        FontWeight = FontWeight.Bold;
        FontSize = 28;
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
