using Microsoft.UI.Xaml;

namespace NativeRuntimeShowcase.Uno;

public sealed partial class App : Application
{
    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var window = new Window
        {
            Title = "HtmlML · Native Runtime Showcase · Uno",
            Content = new MainPage()
        };
        window.Activate();
    }
}
