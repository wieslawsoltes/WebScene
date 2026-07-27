using Avalonia;
using Avalonia.Metadata;

namespace WebScene;

public class style : AvaloniaObject
{
    public static readonly StyledProperty<string?> typeProperty =
        AvaloniaProperty.Register<style, string?>(nameof(type));

    [Content]
    public string? Text { get; set; }

    public string? type
    {
        get => GetValue(typeProperty);
        set => SetValue(typeProperty, value);
    }
}

