using Avalonia;
using Avalonia.Controls.Documents;
using Avalonia.Metadata;

namespace WebScene;

public class em : Span
{
    public static readonly DirectProperty<em, string?> idProperty =
        StyledElement.NameProperty.AddOwner<em>(o => o.Name, (o, v) => o.Name = v);
    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<em>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<em>();

    static em()
    {
        classProperty.Changed.AddClassHandler<em>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<em>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
    }

    public em()
    {
        FontStyle = Avalonia.Media.FontStyle.Italic;
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
