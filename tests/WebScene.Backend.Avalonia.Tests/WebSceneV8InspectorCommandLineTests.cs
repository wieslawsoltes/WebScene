using System.Net;
using WebScene.Diagnostics.Cdp;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class WebSceneV8InspectorCommandLineTests
{
    [Fact]
    public void InspectorRemainsOptIn()
    {
        var configuration = WebSceneV8InspectorCommandLine.Resolve(
            ["showcase"],
            _ => null);

        Assert.Null(configuration);
    }

    [Fact]
    public void InspectBreakParsesEphemeralLoopbackEndpoint()
    {
        var configuration = WebSceneV8InspectorCommandLine.Resolve(
            ["showcase", "--webscene-inspect-brk=127.0.0.1:0"],
            _ => null);

        Assert.NotNull(configuration);
        Assert.Equal(IPAddress.Loopback, configuration.Address);
        Assert.Equal(0, configuration.Port);
        Assert.True(configuration.WaitForDebugger);
        Assert.False(configuration.AllowRemoteConnections);
        Assert.True(configuration.CreateHostOptions().Enabled);
    }

    [Fact]
    public void EnvironmentCanConfigureIpv6AndRemoteOptIn()
    {
        var environment = new Dictionary<string, string>
        {
            ["WEBSCENE_INSPECT"] = "[::1]:9333",
            ["WEBSCENE_INSPECT_ALLOW_REMOTE"] = "true"
        };

        var configuration = WebSceneV8InspectorCommandLine.Resolve(
            ["showcase"],
            name => environment.GetValueOrDefault(name));

        Assert.NotNull(configuration);
        Assert.Equal(IPAddress.IPv6Loopback, configuration.Address);
        Assert.Equal(9333, configuration.Port);
        Assert.False(configuration.WaitForDebugger);
        Assert.True(configuration.AllowRemoteConnections);
    }

    [Fact]
    public void FalseEnvironmentSwitchDoesNotEnableInspector()
    {
        var configuration = WebSceneV8InspectorCommandLine.Resolve(
            ["showcase"],
            name => name == "WEBSCENE_INSPECT" ? "false" : null);

        Assert.Null(configuration);
    }

    [Fact]
    public void RemoteLaunchRequiresCallerKnownDiscoveryToken()
    {
        var missingToken = new Dictionary<string, string>
        {
            ["WEBSCENE_INSPECT"] = "0.0.0.0:9333",
            ["WEBSCENE_INSPECT_ALLOW_REMOTE"] = "true"
        };
        Assert.Throws<ArgumentException>(() =>
            WebSceneV8InspectorCommandLine.Resolve(
                ["showcase"],
                name => missingToken.GetValueOrDefault(name)));

        const string token = "0123456789abcdef0123456789abcdef";
        missingToken["WEBSCENE_INSPECT_TOKEN"] = token;
        var configuration = WebSceneV8InspectorCommandLine.Resolve(
            ["showcase"],
            name => missingToken.GetValueOrDefault(name));

        Assert.NotNull(configuration);
        Assert.Equal(token, configuration.AccessToken);
        Assert.Equal(token, configuration.CreateHostOptions().AccessToken);
    }
}
