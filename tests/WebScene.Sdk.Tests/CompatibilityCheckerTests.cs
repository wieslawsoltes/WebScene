using WebScene.Sdk;
using Xunit;

namespace WebScene.Sdk.Tests;

public sealed class CompatibilityCheckerTests
{
    [Fact]
    public void ReportsUnsupportedApisAndMissingCapabilitiesWithLocations()
    {
        var report = WebSceneCompatibilityChecker.Check(
            "// localStorage ignored in comments\nconst worker = new Worker('worker.js');\nwebscene.host.files.open({});",
            ComponentManifestTests.CreateManifest(),
            "app.ts");

        Assert.False(report.IsCompatible);
        Assert.DoesNotContain(report.Diagnostics, static value => value.Code == "WEBSCENE1002");
        var worker = Assert.Single(report.Diagnostics, static value => value.Code == "WEBSCENE1003");
        Assert.Equal(2, worker.Line);
        var files = Assert.Single(report.Diagnostics, static value => value.Code == "WEBSCENE2007");
        Assert.Equal(WebSceneComponentCapabilities.FileSelection, files.RequiredCapability);
    }

    [Fact]
    public void DeclaredHostCapabilitiesPassAndDirectNetworkingWarns()
    {
        var manifest = ComponentManifestTests.CreateManifest() with
        {
            Capabilities = [WebSceneComponentCapabilities.Dom, WebSceneComponentCapabilities.Networking]
        };
        var report = WebSceneCompatibilityChecker.Check(
            "webscene.host.network.request({ url: '/data' });\nfetch('/bypass');",
            manifest);

        Assert.True(report.IsCompatible);
        Assert.Single(report.Diagnostics, static value => value.Code == "WEBSCENE3001");
    }
}
