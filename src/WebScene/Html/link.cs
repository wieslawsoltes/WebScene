using Avalonia;

namespace WebScene;

public class link : AvaloniaObject
{
    public static readonly StyledProperty<string?> relProperty =
        AvaloniaProperty.Register<link, string?>(nameof(rel));

    public static readonly StyledProperty<string?> hrefProperty =
        AvaloniaProperty.Register<link, string?>(nameof(href));

    public static readonly StyledProperty<string?> typeProperty =
        AvaloniaProperty.Register<link, string?>(nameof(type));

    public string? rel
    {
        get => GetValue(relProperty);
        set => SetValue(relProperty, value);
    }

    public string? href
    {
        get => GetValue(hrefProperty);
        set => SetValue(hrefProperty, value);
    }

    public string? type
    {
        get => GetValue(typeProperty);
        set => SetValue(typeProperty, value);
    }
}

