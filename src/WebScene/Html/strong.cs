using Avalonia;
using Avalonia.Controls.Documents;
using Avalonia.Metadata;

namespace WebScene;

public class strong : Span
{
    public static readonly DirectProperty<strong, string?> idProperty =
        StyledElement.NameProperty.AddOwner<strong>(o => o.Name, (o, v) => o.Name = v);
    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<strong>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<strong>();

    static strong()
    {
        classProperty.Changed.AddClassHandler<strong>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<strong>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
    }

    public strong()
    {
        FontWeight = Avalonia.Media.FontWeight.Bold;
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
