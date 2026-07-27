using Avalonia;
using Avalonia.Controls.Documents;
using Avalonia.Metadata;

namespace WebScene;

public class a : Span
{
    public static readonly StyledProperty<string?> hrefProperty =
        Avalonia.AvaloniaProperty.Register<a, string?>(nameof(href));

    public static readonly DirectProperty<a, string?> idProperty =
        StyledElement.NameProperty.AddOwner<a>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<a>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<a>();

    public string? href
    {
        get => GetValue(hrefProperty);
        set => SetValue(hrefProperty, value);
    }

    static a()
    {
        classProperty.Changed.AddClassHandler<a>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<a>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
    }

    public a() { }

    [Content]
    public InlineCollection content => Inlines;

    public string? id
    {
        get => Name;
        set => Name = value;
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
}
