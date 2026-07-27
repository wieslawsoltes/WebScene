using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using WebScene;

namespace wwwroot;

public partial class main : app
{
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new index();
        }

        base.OnFrameworkInitializationCompleted();
    }
}
