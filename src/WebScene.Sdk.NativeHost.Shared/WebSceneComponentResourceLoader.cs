using System.Text;
using WebScene.Core;

namespace WebScene.Sdk.NativeHost.Internal;

/// <summary>
/// Serves one immutable component package from an isolated virtual origin.
/// Requests cannot escape the assets declared by the component manifest.
/// </summary>
internal sealed class WebSceneComponentResourceLoader : IWebSceneResourceLoader
{
    private static readonly UTF8Encoding s_strictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private const string Shell = """
        <!doctype html>
        <html>
          <head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"></head>
          <body></body>
        </html>
        """;

    private readonly WebSceneComponentPackage _package;
    private readonly Uri _origin;

    public WebSceneComponentResourceLoader(
        WebSceneComponentPackage package,
        Guid instanceId)
    {
        _package = package ?? throw new ArgumentNullException(nameof(package));
        _origin = new Uri(
            $"https://{instanceId:N}.component.webscene.invalid/",
            UriKind.Absolute);
    }

    public string DocumentUrl => new Uri(_origin, "index.html").AbsoluteUri;

    public string GetAssetUrl(string path)
        => new Uri(_origin, NormalizeAssetPath(path)).AbsoluteUri;

    public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(request.Specifier);
        if (!TryResolve(request.Specifier, request.BaseAddress, out var resolved)
            || !HasSameOrigin(resolved))
        {
            throw new UnauthorizedAccessException(
                $"Component '{_package.Manifest.Id}' cannot load '{request.Specifier}'.");
        }

        var path = Uri.UnescapeDataString(resolved.AbsolutePath).TrimStart('/');
        if (path is "" or "index.html")
        {
            return new WebSceneTextResource(
                DocumentUrl,
                Shell,
                DocumentUrl,
                _origin.AbsoluteUri)
            {
                EntityTag = "\"webscene-component-shell-v1\""
            };
        }

        path = NormalizeAssetPath(path);
        var asset = _package.GetAsset(path);
        var entityTag = $"\"{asset.Sha256}\"";
        if (string.Equals(request.IfNoneMatch, entityTag, StringComparison.Ordinal))
        {
            return new WebSceneTextResource(
                GetAssetUrl(path),
                string.Empty,
                GetAssetUrl(path),
                new Uri(resolved, ".").AbsoluteUri)
            {
                EntityTag = entityTag,
                NotModified = true
            };
        }

        string content;
        try
        {
            content = s_strictUtf8.GetString(asset.Content.Span);
        }
        catch (DecoderFallbackException error)
        {
            throw new InvalidDataException(
                $"Component asset '{path}' is not UTF-8 text. Component Profile 1 "
                + "resource loading currently accepts text assets only.",
                error);
        }
        return new WebSceneTextResource(
            GetAssetUrl(path),
            content,
            GetAssetUrl(path),
            new Uri(resolved, ".").AbsoluteUri)
        {
            EntityTag = entityTag
        };
    }

    private static bool TryResolve(
        string specifier,
        string? baseAddress,
        out Uri resolved)
    {
        if (Uri.TryCreate(specifier, UriKind.Absolute, out resolved!))
        {
            return true;
        }
        return Uri.TryCreate(baseAddress, UriKind.Absolute, out var baseUri)
               && Uri.TryCreate(baseUri, specifier, out resolved!);
    }

    private bool HasSameOrigin(Uri uri)
        => string.Equals(uri.Scheme, _origin.Scheme, StringComparison.OrdinalIgnoreCase)
           && string.Equals(uri.Host, _origin.Host, StringComparison.OrdinalIgnoreCase)
           && uri.Port == _origin.Port;

    private static string NormalizeAssetPath(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var normalized = path.Replace('\\', '/').TrimStart('/');
        if (normalized.Split('/').Any(static segment => segment is "" or "." or ".."))
        {
            throw new InvalidDataException($"Invalid component asset path '{path}'.");
        }
        return normalized;
    }
}
