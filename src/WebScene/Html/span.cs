using Avalonia;
using Avalonia.Controls.Documents;
using Avalonia.Metadata;

namespace WebScene;

public class span : Span
{
    public static readonly DirectProperty<span, string?> idProperty =
        StyledElement.NameProperty.AddOwner<span>(o => o.Name, (o, v) => o.Name = v);
    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<span>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<span>();

    static span()
    {
        classProperty.Changed.AddClassHandler<span>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<span>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
    }

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
