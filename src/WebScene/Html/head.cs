using Avalonia;
using Avalonia.Metadata;

namespace WebScene;

public class head : AvaloniaObject
{
    [Content]
    public content content { get; } = new content();
}
