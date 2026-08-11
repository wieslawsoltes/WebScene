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

    [Fact]
    public async Task MissingPackageReportsFailureAndRetainsReusableLifecycle()
    {
        var missingPackage = Path.Combine(
            Path.GetTempPath(),
            "webscene-missing-component-" + Guid.NewGuid().ToString("N"));
        var host = new WebSceneComponentHost
        {
            AutoMount = false,
            PackagePath = missingPackage
        };
        var transitions = new List<(
            WebSceneComponentHostState Previous,
            WebSceneComponentHostState Current)>();
        var diagnostics = new List<WebSceneSdkDiagnostic>();
        var mountFailures = 0;
        var unmounts = 0;
        host.StateChanged += (_, args) =>
            transitions.Add((args.PreviousState, args.State));
        host.DiagnosticReported += (_, args) => diagnostics.Add(args.Diagnostic);
        host.MountFailed += (_, _) => mountFailures++;
        host.ComponentUnmounted += (_, _) => unmounts++;

        try
        {
            var error = await Assert.ThrowsAsync<DirectoryNotFoundException>(
                () => host.MountAsync());

            Assert.Contains(missingPackage, error.Message, StringComparison.Ordinal);
            Assert.Equal(WebSceneComponentHostState.Faulted, host.State);
            Assert.Same(error, host.LastException);
            Assert.Null(host.ComponentPackage);
            Assert.Null(host.ComponentInstance);
            Assert.Null(host.CompatibilityReport);
            Assert.Equal(1, mountFailures);
            Assert.Contains(
                diagnostics,
                diagnostic => diagnostic.Code == "component.mount.failed"
                    && diagnostic.Severity == WebSceneDiagnosticSeverity.Error);
            Assert.Equal(
                [
                    (WebSceneComponentHostState.Idle, WebSceneComponentHostState.Mounting),
                    (WebSceneComponentHostState.Mounting, WebSceneComponentHostState.Faulted)
                ],
                transitions);

            await host.UnmountAsync();

            Assert.Equal(WebSceneComponentHostState.Idle, host.State);
            Assert.Equal(1, unmounts);
            Assert.Equal(
                (WebSceneComponentHostState.Faulted, WebSceneComponentHostState.Unmounting),
                transitions[^2]);
            Assert.Equal(
                (WebSceneComponentHostState.Unmounting, WebSceneComponentHostState.Idle),
                transitions[^1]);
        }
        finally
        {
            await host.DisposeAsync();
        }

        Assert.Equal(WebSceneComponentHostState.Disposed, host.State);
        await host.DisposeAsync();
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
