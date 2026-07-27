using Avalonia;

namespace WebScene;

public class app : Application
{
    private string? _name;

    public static readonly DirectProperty<app, string?> nameProperty =
        Application.NameProperty.AddOwner<app>(o => o.name, (o, v) => o.name = v);

    public string? name
    {
        get => _name;
        set => SetAndRaise(nameProperty, ref _name, value);
    }
}
