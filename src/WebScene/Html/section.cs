using Avalonia;
using Avalonia.Controls;
using Avalonia.Metadata;

namespace WebScene;

public class section : Border
{
    protected override System.Type StyleKeyOverride => typeof(Border);

    public static readonly DirectProperty<section, string?> idProperty =
        NameProperty.AddOwner<section>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<section>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<section>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<section>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<section>();

    private readonly StackPanel _host = new StackPanel() { Orientation = Avalonia.Layout.Orientation.Vertical };

    static section()
    {
        classProperty.Changed.AddClassHandler<section>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<section>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<section>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<section>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public section()
    {
        DockPanel.SetDock(this, Dock.Top);
        Child = _host;
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

    [Content]
    public Controls content => _host.Children;
}
