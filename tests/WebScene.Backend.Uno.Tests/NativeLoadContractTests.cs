using WebScene.Backends.Native;
using WebScene.Backends.Uno.Native;
using Xunit;

namespace WebScene.Backend.Uno.Tests;

public sealed class NativeLoadContractTests
{
    [Fact]
    public void ViewRetainsLegacyAndOptionsLoadOverloads()
    {
        Assert.NotNull(typeof(UnoNativeWebSceneView).GetMethod(
            nameof(UnoNativeWebSceneView.LoadAsync),
            [
                typeof(string),
                typeof(string),
                typeof(string),
                typeof(CancellationToken)
            ]));
        Assert.NotNull(typeof(UnoNativeWebSceneView).GetMethod(
            nameof(UnoNativeWebSceneView.LoadAsync),
            [typeof(NativeWebSceneLoadOptions), typeof(CancellationToken)]));
    }
}
