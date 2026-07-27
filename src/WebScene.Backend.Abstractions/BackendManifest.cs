using System.Text.Json;
using System.Text.Json.Serialization;
using WebScene.Core;

namespace WebScene.Backends;

public enum WebSceneBackendSupportLevel
{
    Experimental = 0,
    Component = 1,
    Application = 2,
    Advanced = 3
}

/// <summary>Versioned, machine-readable support claim published by a backend package.</summary>
public sealed record WebSceneBackendManifest
{
    public const string CurrentSchemaVersion = "1.0";

    public required string SchemaVersion { get; init; }

    public required string Id { get; init; }

    public required string DisplayName { get; init; }

    public required string Version { get; init; }

    public required string Assembly { get; init; }

    public required string BackendType { get; init; }

    public required WebSceneBackendSupportLevel MaximumSupportLevel { get; init; }

    public required string[] Capabilities { get; init; }

    public required string[] TargetFrameworks { get; init; }

    public string[] Platforms { get; init; } = [];
}

public sealed record WebSceneBackendManifestValidationResult(IReadOnlyList<string> Errors)
{
    public bool IsValid => Errors.Count == 0;

    public void ThrowIfInvalid()
    {
        if (!IsValid)
        {
            throw new InvalidDataException("Invalid WebScene backend manifest: " + string.Join("; ", Errors));
        }
    }
}

public static class WebSceneBackendManifestSerializer
{
    private static readonly JsonSerializerOptions s_jsonOptions = new(JsonSerializerDefaults.Web)
    {
        AllowTrailingCommas = false,
        PropertyNameCaseInsensitive = false,
        ReadCommentHandling = JsonCommentHandling.Disallow,
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() }
    };

    public static WebSceneBackendManifest Parse(string json)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(json);
        var manifest = JsonSerializer.Deserialize<WebSceneBackendManifest>(json, s_jsonOptions)
                       ?? throw new InvalidDataException("The backend manifest was empty.");
        Validate(manifest).ThrowIfInvalid();
        return manifest;
    }

    public static WebSceneBackendManifest Read(Stream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        var manifest = JsonSerializer.Deserialize<WebSceneBackendManifest>(stream, s_jsonOptions)
                       ?? throw new InvalidDataException("The backend manifest was empty.");
        Validate(manifest).ThrowIfInvalid();
        return manifest;
    }

    public static string Serialize(WebSceneBackendManifest manifest)
    {
        Validate(manifest).ThrowIfInvalid();
        return JsonSerializer.Serialize(manifest, s_jsonOptions);
    }

    public static WebSceneBackendManifestValidationResult Validate(WebSceneBackendManifest? manifest)
    {
        var errors = new List<string>();
        if (manifest is null)
        {
            errors.Add("Manifest is required.");
            return new WebSceneBackendManifestValidationResult(errors);
        }

        if (!string.Equals(
                manifest.SchemaVersion,
                WebSceneBackendManifest.CurrentSchemaVersion,
                StringComparison.Ordinal))
        {
            errors.Add($"schemaVersion must be '{WebSceneBackendManifest.CurrentSchemaVersion}'.");
        }
        RequireValue(manifest.Id, "id", errors);
        RequireValue(manifest.DisplayName, "displayName", errors);
        RequireValue(manifest.Version, "version", errors);
        RequireValue(manifest.Assembly, "assembly", errors);
        RequireValue(manifest.BackendType, "backendType", errors);
        if (!Enum.IsDefined(manifest.MaximumSupportLevel))
        {
            errors.Add($"Unknown maximumSupportLevel '{manifest.MaximumSupportLevel}'.");
        }
        if (manifest.TargetFrameworks is null || manifest.TargetFrameworks.Length == 0)
        {
            errors.Add("targetFrameworks must contain at least one target.");
        }
        else
        {
            ValidateUniqueValues(manifest.TargetFrameworks, "targetFrameworks", errors);
        }
        if (manifest.Platforms is not null)
        {
            ValidateUniqueValues(manifest.Platforms, "platforms", errors);
        }
        if (manifest.Capabilities is null || manifest.Capabilities.Length == 0)
        {
            errors.Add("capabilities must contain at least one capability.");
        }
        else
        {
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var capability in manifest.Capabilities)
            {
                if (!seen.Add(capability))
                {
                    errors.Add($"capabilities contains duplicate '{capability}'.");
                    continue;
                }
                if (!TryParseCapability(capability, out _))
                {
                    errors.Add($"Unknown capability '{capability}'.");
                }
            }
        }
        return new WebSceneBackendManifestValidationResult(errors);
    }

    public static WebSceneBackendCapabilities ResolveCapabilities(WebSceneBackendManifest manifest)
    {
        Validate(manifest).ThrowIfInvalid();
        var capabilities = WebSceneBackendCapabilities.None;
        foreach (var value in manifest.Capabilities)
        {
            TryParseCapability(value, out var capability);
            capabilities |= capability;
        }
        return capabilities;
    }

    private static bool TryParseCapability(string? value, out WebSceneBackendCapabilities capability)
    {
        if (!Enum.TryParse(value, ignoreCase: false, out capability)
            || capability == WebSceneBackendCapabilities.None)
        {
            return false;
        }
        var raw = (ulong)capability;
        return (raw & (raw - 1)) == 0;
    }

    private static void RequireValue(string? value, string property, ICollection<string> errors)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            errors.Add($"{property} is required.");
        }
    }

    private static void ValidateUniqueValues(
        IEnumerable<string?> values,
        string property,
        ICollection<string> errors)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var value in values)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                errors.Add($"{property} values must not be empty.");
            }
            else if (!seen.Add(value))
            {
                errors.Add($"{property} contains duplicate '{value}'.");
            }
        }
    }
}

/// <summary>Shared profile preflight used by backend contract suites and component hosts.</summary>
public static class WebSceneBackendContractVerifier
{
    public static void Verify(
        IWebSceneBackendHost backend,
        WebSceneBackendManifest manifest,
        WebSceneBackendSupportLevel requiredSupportLevel)
    {
        ArgumentNullException.ThrowIfNull(backend);
        WebSceneBackendManifestSerializer.Validate(manifest).ThrowIfInvalid();
        if (manifest.MaximumSupportLevel < requiredSupportLevel)
        {
            throw new InvalidDataException(
                $"Backend manifest supports '{manifest.MaximumSupportLevel}', not required level '{requiredSupportLevel}'.");
        }

        var advertised = WebSceneBackendManifestSerializer.ResolveCapabilities(manifest);
        if (advertised != backend.Capabilities)
        {
            throw new InvalidDataException(
                $"Manifest capabilities '{advertised}' do not match runtime capabilities '{backend.Capabilities}'.");
        }
        backend.EnsureCapabilities(advertised);
    }
}
