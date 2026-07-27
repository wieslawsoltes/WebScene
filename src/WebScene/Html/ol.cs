using Avalonia;
using Avalonia.Controls;
using Avalonia.Metadata;

namespace WebScene;

public class ol : Border
{
    protected override System.Type StyleKeyOverride => typeof(Border);

    public static readonly DirectProperty<ol, string?> idProperty =
        NameProperty.AddOwner<ol>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<ol>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<ol>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<ol>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<ol>();

    private readonly StackPanel _host = new StackPanel() { Orientation = Avalonia.Layout.Orientation.Vertical };

    static ol()
    {
        classProperty.Changed.AddClassHandler<ol>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<ol>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<ol>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<ol>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public ol()
    {
        Margin = new Thickness(16, 0, 0, 0);
        Child = _host;
    }

    [Content]
    public Controls content => _host.Children;

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
