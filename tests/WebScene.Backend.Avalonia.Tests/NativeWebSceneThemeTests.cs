using Avalonia.Styling;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeWebSceneThemeTests
{
    [Fact]
    public void EffectiveAvaloniaThemeMapsToBrowserPreferredColorScheme()
    {
        Assert.Equal(
            NativePreferredColorScheme.Dark,
            NativeWebSceneView.ResolvePreferredColorScheme(ThemeVariant.Dark));
        Assert.Equal(
            NativePreferredColorScheme.Light,
            NativeWebSceneView.ResolvePreferredColorScheme(ThemeVariant.Light));
        Assert.Equal(
            NativePreferredColorScheme.Light,
            NativeWebSceneView.ResolvePreferredColorScheme(ThemeVariant.Default));
    }
}
