using WebScene.Backends;
using WebScene.Core;
using Xunit;

namespace WebScene.Backend.Abstractions.Tests;

public sealed class BackendManifestTests
{
    [Fact]
    public void ValidManifestRoundTripsAndResolvesCapabilities()
    {
        var manifest = WebSceneBackendManifestSerializer.Parse("""
            {
              "schemaVersion": "1.0",
              "id": "example.backend",
              "displayName": "Example Backend",
              "version": "1.0.0",
              "assembly": "Example.Backend",
              "backendType": "Example.Backend.Host",
              "maximumSupportLevel": "Component",
              "capabilities": ["DomProjection", "CssLayout", "Canvas2D"],
              "targetFrameworks": ["net8.0"],
              "platforms": ["headless"]
            }
            """);

        Assert.Equal(WebSceneBackendSupportLevel.Component, manifest.MaximumSupportLevel);
        Assert.Equal(
            WebSceneBackendCapabilities.DomProjection
            | WebSceneBackendCapabilities.CssLayout
            | WebSceneBackendCapabilities.Canvas2D,
            WebSceneBackendManifestSerializer.ResolveCapabilities(manifest));
        var roundTrip = WebSceneBackendManifestSerializer.Parse(
            WebSceneBackendManifestSerializer.Serialize(manifest));
        Assert.Equal(manifest.Id, roundTrip.Id);
        Assert.Equal(manifest.MaximumSupportLevel, roundTrip.MaximumSupportLevel);
        Assert.Equal(manifest.Capabilities, roundTrip.Capabilities);
        Assert.Equal(manifest.TargetFrameworks, roundTrip.TargetFrameworks);
        Assert.Equal(manifest.Platforms, roundTrip.Platforms);
    }

    [Fact]
    public void InvalidSchemaUnknownAndDuplicateCapabilitiesFailBeforeStartup()
    {
        var exception = Assert.Throws<InvalidDataException>(() => WebSceneBackendManifestSerializer.Parse("""
            {
              "schemaVersion": "2.0",
              "id": "example.backend",
              "displayName": "Example Backend",
              "version": "1.0.0",
              "assembly": "Example.Backend",
              "backendType": "Example.Backend.Host",
              "maximumSupportLevel": "Experimental",
              "capabilities": ["Canvas2D", "Canvas2D", "Unknown"],
              "targetFrameworks": ["net8.0"]
            }
            """));

        Assert.Contains("schemaVersion", exception.Message, StringComparison.Ordinal);
        Assert.Contains("duplicate 'Canvas2D'", exception.Message, StringComparison.Ordinal);
        Assert.Contains("Unknown capability 'Unknown'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void StreamReadAndValidationCoverThePublishedRuntimeContract()
    {
        using var stream = new MemoryStream(System.Text.Encoding.UTF8.GetBytes(
            WebSceneBackendManifestSerializer.Serialize(CreateManifest())));

        var manifest = WebSceneBackendManifestSerializer.Read(stream);

        Assert.Equal("example.backend", manifest.Id);
        Assert.True(WebSceneBackendManifestSerializer.Validate(manifest).IsValid);
        Assert.True(WebSceneBackendManifestSerializer.Validate(null).Errors.Count > 0);
        Assert.Throws<ArgumentNullException>(() => WebSceneBackendManifestSerializer.Read(null!));
        Assert.Throws<ArgumentException>(() => WebSceneBackendManifestSerializer.Parse(" "));
        Assert.Throws<System.Text.Json.JsonException>(() =>
            WebSceneBackendManifestSerializer.Read(new MemoryStream([])));
        Assert.Throws<InvalidDataException>(() => WebSceneBackendManifestSerializer.Serialize(null!));
    }

    [Fact]
    public void RuntimeValidationMatchesSchemaUniquenessAndRequiredValueRules()
    {
        var invalid = CreateManifest() with
        {
            SchemaVersion = "0",
            Id = " ",
            DisplayName = "",
            Version = "",
            Assembly = "",
            BackendType = "",
            MaximumSupportLevel = (WebSceneBackendSupportLevel)99,
            Capabilities = ["Canvas2D", "Canvas2D", "Canvas2D, Svg", "Nope"],
            TargetFrameworks = ["", "net8.0", "net8.0"],
            Platforms = [" ", "headless", "headless"]
        };

        var result = WebSceneBackendManifestSerializer.Validate(invalid);

        Assert.False(result.IsValid);
        var message = Assert.Throws<InvalidDataException>(result.ThrowIfInvalid).Message;
        Assert.Contains("schemaVersion", message, StringComparison.Ordinal);
        Assert.Contains("id is required", message, StringComparison.Ordinal);
        Assert.Contains("Unknown maximumSupportLevel", message, StringComparison.Ordinal);
        Assert.Contains("targetFrameworks values must not be empty", message, StringComparison.Ordinal);
        Assert.Contains("targetFrameworks contains duplicate", message, StringComparison.Ordinal);
        Assert.Contains("platforms values must not be empty", message, StringComparison.Ordinal);
        Assert.Contains("platforms contains duplicate", message, StringComparison.Ordinal);
        Assert.Contains("Unknown capability 'Canvas2D, Svg'", message, StringComparison.Ordinal);
    }

    [Fact]
    public void EmptyArraysAreRejected()
    {
        var result = WebSceneBackendManifestSerializer.Validate(CreateManifest() with
        {
            Capabilities = [],
            TargetFrameworks = []
        });

        Assert.Contains(result.Errors, error => error.Contains("capabilities", StringComparison.Ordinal));
        Assert.Contains(result.Errors, error => error.Contains("targetFrameworks", StringComparison.Ordinal));
    }

    [Fact]
    public void ContractVerifierAcceptsExactAdvancedClaimAndRejectsEveryMismatch()
    {
        var manifest = CreateManifest() with { MaximumSupportLevel = WebSceneBackendSupportLevel.Advanced };
        var backend = new ContractBackend(
            WebSceneBackendCapabilities.DomProjection | WebSceneBackendCapabilities.Canvas2D);

        WebSceneBackendContractVerifier.Verify(backend, manifest, WebSceneBackendSupportLevel.Advanced);
        Assert.Equal(backend.Capabilities, backend.LastEnsured);
        Assert.Throws<ArgumentNullException>(() =>
            WebSceneBackendContractVerifier.Verify(null!, manifest, WebSceneBackendSupportLevel.Component));
        Assert.Throws<InvalidDataException>(() =>
            WebSceneBackendContractVerifier.Verify(
                backend,
                manifest with { MaximumSupportLevel = WebSceneBackendSupportLevel.Component },
                WebSceneBackendSupportLevel.Application));
        Assert.Throws<InvalidDataException>(() =>
            WebSceneBackendContractVerifier.Verify(
                new ContractBackend(WebSceneBackendCapabilities.DomProjection),
                manifest,
                WebSceneBackendSupportLevel.Component));
        Assert.Throws<InvalidDataException>(() =>
            WebSceneBackendContractVerifier.Verify(
                backend,
                manifest with { Id = "" },
                WebSceneBackendSupportLevel.Component));
    }

    private static WebSceneBackendManifest CreateManifest() => new()
    {
        SchemaVersion = WebSceneBackendManifest.CurrentSchemaVersion,
        Id = "example.backend",
        DisplayName = "Example Backend",
        Version = "1.0.0",
        Assembly = "Example.Backend",
        BackendType = "Example.Backend.Host",
        MaximumSupportLevel = WebSceneBackendSupportLevel.Component,
        Capabilities = ["DomProjection", "Canvas2D"],
        TargetFrameworks = ["net8.0"],
        Platforms = ["headless"]
    };

    private sealed class ContractBackend(WebSceneBackendCapabilities capabilities) : IWebSceneBackendHost
    {
        public WebSceneBackendCapabilities LastEnsured { get; private set; }
        public WebSceneBackendState State => WebSceneBackendState.Mounted;
        public WebSceneBackendNode Root => default;
        public WebSceneBackendCapabilities Capabilities { get; } = capabilities;
        public IWebSceneHostServices Services => null!;
        public IWebSceneInputSource Input => null!;
        public IReadOnlyList<WebSceneBackendDiagnostic> Diagnostics => [];
        public void EnsureCapabilities(WebSceneBackendCapabilities required) => LastEnsured = required;
        public void Mount() => throw new NotSupportedException();
        public void Unmount() => throw new NotSupportedException();
        public WebSceneBackendNode CreateNode(in WebSceneBackendNodeDescriptor descriptor) => throw new NotSupportedException();
        public void Attach(WebSceneBackendNode parent, WebSceneBackendNode child, int index) => throw new NotSupportedException();
        public void Detach(WebSceneBackendNode node) => throw new NotSupportedException();
        public void Arrange(WebSceneBackendNode node, WebSceneRect bounds) => throw new NotSupportedException();
        public void SetVisible(WebSceneBackendNode node, bool visible) => throw new NotSupportedException();
        public void SetZIndex(WebSceneBackendNode node, int zIndex) => throw new NotSupportedException();
        public void Invalidate(WebSceneBackendNode node, WebSceneInvalidationKind kind) => throw new NotSupportedException();
        public WebSceneBackendNode? HitTest(WebScenePoint point) => throw new NotSupportedException();
        public void Dispose() { }
    }
}
