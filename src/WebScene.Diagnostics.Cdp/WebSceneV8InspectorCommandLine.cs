using System.Net;

namespace WebScene.Diagnostics.Cdp;

/// <summary>
/// Parsed, host-neutral WebScene V8 Inspector launch configuration.
/// </summary>
public sealed record WebSceneV8InspectorLaunchConfiguration(
    IPAddress Address,
    int Port,
    bool WaitForDebugger,
    bool AllowRemoteConnections)
{
    public WebSceneV8InspectorOptions CreateHostOptions()
        => new()
        {
            Enabled = true,
            Address = Address,
            Port = Port,
            WaitForDebugger = WaitForDebugger,
            AllowRemoteConnections = AllowRemoteConnections
        };
}

/// <summary>
/// Parses Node-style --webscene-inspect and --webscene-inspect-brk switches.
/// </summary>
public static class WebSceneV8InspectorCommandLine
{
    public static WebSceneV8InspectorLaunchConfiguration? Resolve(
        IReadOnlyList<string> arguments,
        Func<string, string?>? getEnvironmentVariable = null)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        getEnvironmentVariable ??= Environment.GetEnvironmentVariable;

        var enabled = arguments.Contains("--v8-inspector", StringComparer.Ordinal);
        var waitForDebugger = false;
        string? endpoint = null;
        foreach (var argument in arguments)
        {
            if (TryReadEndpoint(argument, "--webscene-inspect-brk", out var brk))
            {
                enabled = true;
                waitForDebugger = true;
                endpoint = brk ?? endpoint;
            }
            else if (TryReadEndpoint(argument, "--webscene-inspect", out var inspect))
            {
                enabled = true;
                endpoint = inspect ?? endpoint;
            }
        }

        enabled = enabled || IsTruthy(getEnvironmentVariable(
            "WEBSCENE_V8_INSPECTOR"));
        ApplyEnvironmentEndpoint(
            getEnvironmentVariable("WEBSCENE_INSPECT"),
            ref enabled,
            ref endpoint);
        var configuredBreak = getEnvironmentVariable("WEBSCENE_INSPECT_BRK");
        if (!IsFalsy(configuredBreak) && !string.IsNullOrWhiteSpace(configuredBreak))
        {
            enabled = true;
            waitForDebugger = true;
            if (!IsTruthy(configuredBreak)) endpoint = configuredBreak;
        }
        if (!enabled) return null;

        var address = IPAddress.Loopback;
        var port = ResolveLegacyPort(arguments, getEnvironmentVariable);
        if (!string.IsNullOrWhiteSpace(endpoint))
        {
            (address, port) = ParseEndpoint(endpoint);
        }
        var allowRemote = arguments.Contains(
                "--webscene-inspect-allow-remote",
                StringComparer.Ordinal)
            || IsTruthy(getEnvironmentVariable(
                "WEBSCENE_INSPECT_ALLOW_REMOTE"));
        return new WebSceneV8InspectorLaunchConfiguration(
            address,
            port,
            waitForDebugger,
            allowRemote);
    }

    private static void ApplyEnvironmentEndpoint(
        string? value,
        ref bool enabled,
        ref string? endpoint)
    {
        if (IsFalsy(value) || string.IsNullOrWhiteSpace(value)) return;
        enabled = true;
        if (!IsTruthy(value)) endpoint = value;
    }

    private static bool TryReadEndpoint(
        string argument,
        string option,
        out string? endpoint)
    {
        if (string.Equals(argument, option, StringComparison.Ordinal))
        {
            endpoint = null;
            return true;
        }
        var prefix = option + "=";
        if (argument.StartsWith(prefix, StringComparison.Ordinal))
        {
            endpoint = argument[prefix.Length..];
            if (string.IsNullOrWhiteSpace(endpoint))
            {
                throw new ArgumentException($"{option} requires an endpoint after '='.");
            }
            return true;
        }
        endpoint = null;
        return false;
    }

    private static int ResolveLegacyPort(
        IReadOnlyList<string> arguments,
        Func<string, string?> getEnvironmentVariable)
    {
        string? configured = null;
        for (var index = 0; index + 1 < arguments.Count; ++index)
        {
            if (arguments[index] == "--v8-inspector-port")
            {
                configured = arguments[index + 1];
                break;
            }
        }
        configured ??= getEnvironmentVariable("WEBSCENE_V8_INSPECTOR_PORT");
        if (string.IsNullOrWhiteSpace(configured)) return 9229;
        if (int.TryParse(configured, out var port) && port is >= 0 and <= 65535)
        {
            return port;
        }
        throw new ArgumentException(
            $"Invalid WebScene V8 Inspector port: '{configured}'.");
    }

    private static (IPAddress Address, int Port) ParseEndpoint(string endpoint)
    {
        if (int.TryParse(endpoint, out var portOnly)
            && portOnly is >= 0 and <= 65535)
        {
            return (IPAddress.Loopback, portOnly);
        }
        if (!Uri.TryCreate($"tcp://{endpoint}", UriKind.Absolute, out var uri)
            || !IPAddress.TryParse(uri.Host.Trim('[', ']'), out var address)
            || uri.Port is < 0 or > 65535)
        {
            throw new ArgumentException(
                $"Invalid WebScene Inspector endpoint: '{endpoint}'. "
                + "Use an IP address and port, for example 127.0.0.1:9229.");
        }
        return (address, uri.Port);
    }

    private static bool IsTruthy(string? value)
        => string.Equals(value, "1", StringComparison.Ordinal)
        || string.Equals(value, "true", StringComparison.OrdinalIgnoreCase);

    private static bool IsFalsy(string? value)
        => string.Equals(value, "0", StringComparison.Ordinal)
        || string.Equals(value, "false", StringComparison.OrdinalIgnoreCase);
}
