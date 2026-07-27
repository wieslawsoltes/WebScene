using Avalonia;
using Avalonia.Metadata;

namespace WebScene;

public class script : AvaloniaObject
{
    public static readonly StyledProperty<string?> typeProperty =
        AvaloniaProperty.Register<script, string?>(nameof(type));

    public static readonly StyledProperty<string?> srcProperty =
        AvaloniaProperty.Register<script, string?>(nameof(src));

    [Content]
    public string? Text { get; set; }

    public string? type
    {
        get => GetValue(typeProperty);
        set => SetValue(typeProperty, value);
    }

    public string? src
    {
        get => GetValue(srcProperty);
        set => SetValue(srcProperty, value);
    }
}

