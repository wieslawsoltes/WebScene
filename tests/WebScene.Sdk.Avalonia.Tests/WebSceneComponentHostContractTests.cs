using System.Reflection;
using Avalonia;
using WebScene.Backends.Native;
using WebScene.Core;
using Xunit;

namespace WebScene.Sdk.Avalonia.Tests;

public sealed class WebSceneComponentHostContractTests
{
    [Fact]
    public void ExposesBindableConfigurationAndExplicitLifecycle()
    {
        var type = typeof(WebSceneComponentHost);

        Assert.Equal(
            typeof(StyledProperty<string?>),
            type.GetField(nameof(WebSceneComponentHost.PackagePathProperty))?.FieldType);
        Assert.Equal(
            typeof(StyledProperty<bool>),
            type.GetField(nameof(WebSceneComponentHost.AutoMountProperty))?.FieldType);
        AssertMethod(type, nameof(WebSceneComponentHost.MountAsync));
        AssertMethod(type, nameof(WebSceneComponentHost.UnmountAsync));
        AssertMethod(type, nameof(WebSceneComponentHost.ReloadAsync));
        Assert.NotNull(type.GetEvent(nameof(WebSceneComponentHost.DiagnosticReported)));
        Assert.NotNull(type.GetProperty(nameof(WebSceneComponentHost.View)));
    }

    [Fact]
    public void NativeLoadOptionsAcceptAHostResourcePolicy()
    {
        var loader = new RecordingLoader();
        var options = new NativeWebSceneLoadOptions
        {
            Source = "https://component.webscene.invalid/",
            NativeLibraryPath = "/native/library",
            ResourceLoader = loader
        };

        Assert.Same(loader, options.ResourceLoader);
    }

    private static void AssertMethod(Type type, string name)
        => Assert.NotNull(type.GetMethod(
            name,
            BindingFlags.Instance | BindingFlags.Public));

    private sealed class RecordingLoader : IWebSceneResourceLoader
    {
        public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
            => throw new NotSupportedException();
    }
}
