using Avalonia;
using Avalonia.Metadata;

namespace WebScene;

public class title : AvaloniaObject
{
    public static readonly StyledProperty<string?> TextProperty =
        AvaloniaProperty.Register<title, string?>(nameof(Text));
    
    [Content]
    public string? Text
    {
        get => GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }
}
