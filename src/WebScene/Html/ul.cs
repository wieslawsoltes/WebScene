using Avalonia;
using Avalonia.Controls;
using Avalonia.Metadata;

namespace WebScene;

public class ul : Border
{
    protected override System.Type StyleKeyOverride => typeof(Border);

    public static readonly DirectProperty<ul, string?> idProperty =
        NameProperty.AddOwner<ul>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<ul>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<ul>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<ul>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<ul>();

    private readonly StackPanel _host = new StackPanel() { Orientation = Avalonia.Layout.Orientation.Vertical };

    static ul()
    {
        classProperty.Changed.AddClassHandler<ul>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<ul>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<ul>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<ul>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public ul()
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
