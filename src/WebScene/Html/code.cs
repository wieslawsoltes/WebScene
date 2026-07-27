using Avalonia;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using Avalonia.Metadata;

namespace WebScene;

public class code : Span
{
    public static readonly DirectProperty<code, string?> idProperty =
        StyledElement.NameProperty.AddOwner<code>(o => o.Name, (o, v) => o.Name = v);
    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<code>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<code>();

    static code()
    {
        classProperty.Changed.AddClassHandler<code>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<code>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
    }

    public code()
    {
        FontFamily = new FontFamily("monospace");
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
