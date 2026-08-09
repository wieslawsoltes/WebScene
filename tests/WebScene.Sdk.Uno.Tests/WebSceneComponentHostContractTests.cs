using System.Reflection;
using Microsoft.UI.Xaml;
using WebScene.Backends.Uno.Native;
using Xunit;

namespace WebScene.Sdk.Uno.Tests;

public sealed class WebSceneComponentHostContractTests
{
    [Fact]
    public void ExposesBindableConfigurationAndExplicitLifecycle()
    {
        var type = typeof(WebSceneComponentHost);

        Assert.Equal(
            typeof(DependencyProperty),
            type.GetField(nameof(WebSceneComponentHost.PackagePathProperty))?.FieldType);
        Assert.Equal(
            typeof(DependencyProperty),
            type.GetField(nameof(WebSceneComponentHost.AutoMountProperty))?.FieldType);
        AssertMethod(type, nameof(WebSceneComponentHost.MountAsync));
        AssertMethod(type, nameof(WebSceneComponentHost.UnmountAsync));
        AssertMethod(type, nameof(WebSceneComponentHost.ReloadAsync));
        Assert.NotNull(type.GetEvent(nameof(WebSceneComponentHost.DiagnosticReported)));
        Assert.Equal(
            typeof(UnoNativeWebSceneView),
            type.GetProperty(nameof(WebSceneComponentHost.View))?.PropertyType);
    }

    [Fact]
    public void UnoViewExposesReusableUnloadLifecycle()
        => AssertMethod(typeof(UnoNativeWebSceneView), nameof(UnoNativeWebSceneView.UnloadAsync));

    private static void AssertMethod(Type type, string name)
        => Assert.NotNull(type.GetMethod(
            name,
            BindingFlags.Instance | BindingFlags.Public));
}
