using Avalonia;
using Avalonia.Controls;
using Avalonia.Metadata;

namespace WebScene;

public class header : Border
{
    protected override System.Type StyleKeyOverride => typeof(Border);

    public static readonly DirectProperty<header, string?> idProperty =
        NameProperty.AddOwner<header>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<header>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<header>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<header>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<header>();

    private readonly StackPanel _host = new StackPanel() { Orientation = Avalonia.Layout.Orientation.Vertical };

    static header()
    {
        classProperty.Changed.AddClassHandler<header>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<header>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<header>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<header>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public header()
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
