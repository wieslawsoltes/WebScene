using System.Text;
using System.Text.Json;

namespace WebScene.Backends.Avalonia.Native;

internal readonly record struct NativeDownloadRequest(
    string SuggestedFileName,
    string ContentType,
    byte[]? Bytes,
    Uri? RemoteUri,
    uint? CanvasNodeId);

internal readonly record struct NativeClipboardWriteRequest(
    string ContentType,
    byte[]? Bytes,
    uint? CanvasNodeId);

internal static class NativeHostRequest
{
    public static bool TryGetKind(string request, out string kind)
    {
        kind = string.Empty;
        try
        {
            using var document = JsonDocument.Parse(request);
            if (!document.RootElement.TryGetProperty("kind", out var property)
                || property.ValueKind != JsonValueKind.String)
            {
                return false;
            }
            kind = property.GetString() ?? string.Empty;
            return kind.Length > 0;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    public static bool TryGetExternalUri(string request, out Uri? uri)
    {
        uri = null;
        try
        {
            using var document = JsonDocument.Parse(request);
            return document.RootElement.TryGetProperty("kind", out var kind)
                && kind.GetString() == "openExternalUrl"
                && document.RootElement.TryGetProperty("url", out var url)
                && Uri.TryCreate(url.GetString(), UriKind.Absolute, out uri)
                && uri.Scheme is "http" or "https";
        }
        catch (JsonException)
        {
            return false;
        }
    }

    public static bool TryGetDownload(string request, out NativeDownloadRequest download)
    {
        download = default;
        try
        {
            using var document = JsonDocument.Parse(request);
            var root = document.RootElement;
            if (!root.TryGetProperty("kind", out var kind)
                || kind.GetString() != "download")
            {
                return false;
            }
            var suggested = root.TryGetProperty("suggestedFileName", out var name)
                ? name.GetString() : null;
            suggested = SafeFileName(suggested);
            if (root.TryGetProperty("canvasNodeId", out var canvasNodeId)
                && canvasNodeId.TryGetUInt32(out var nodeId))
            {
                download = new NativeDownloadRequest(
                    suggested,
                    "image/png",
                    null,
                    null,
                    nodeId);
                return true;
            }
            if (!root.TryGetProperty("url", out var urlProperty))
            {
                return false;
            }
            var url = urlProperty.GetString() ?? string.Empty;
            if (TryDecodeDataUrl(url, out var contentType, out var bytes))
            {
                // A canvas-backed Blob has no encoded bytes in the V8 realm;
                // it must arrive with canvasNodeId so the host captures the
                // retained compositor layer. Never offer a zero-byte PNG.
                if (bytes.Length == 0
                    && string.Equals(
                        contentType,
                        "image/png",
                        StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }
                download = new NativeDownloadRequest(
                    suggested,
                    contentType,
                    bytes,
                    null,
                    null);
                return true;
            }
            if (Uri.TryCreate(url, UriKind.Absolute, out var remote)
                && remote.Scheme is "http" or "https")
            {
                download = new NativeDownloadRequest(
                    suggested,
                    "application/octet-stream",
                    null,
                    remote,
                    null);
                return true;
            }
            return false;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    public static bool TryGetClipboardWrite(
        string request,
        out NativeClipboardWriteRequest clipboardWrite)
    {
        clipboardWrite = default;
        try
        {
            using var document = JsonDocument.Parse(request);
            var root = document.RootElement;
            if (!root.TryGetProperty("kind", out var kind)
                || kind.GetString() != "writeClipboard"
                || !root.TryGetProperty("contentType", out var contentTypeProperty))
            {
                return false;
            }
            var contentType = contentTypeProperty.GetString() ?? string.Empty;
            if (contentType.Length == 0) return false;
            if (root.TryGetProperty("canvasNodeId", out var canvasNodeId)
                && canvasNodeId.TryGetUInt32(out var nodeId))
            {
                clipboardWrite = new NativeClipboardWriteRequest(
                    contentType,
                    null,
                    nodeId);
                return true;
            }
            if (!root.TryGetProperty("url", out var url)
                || !TryDecodeDataUrl(
                    url.GetString() ?? string.Empty,
                    out var decodedContentType,
                    out var bytes))
            {
                return false;
            }
            if (!string.Equals(
                    contentType,
                    decodedContentType,
                    StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
            clipboardWrite = new NativeClipboardWriteRequest(
                contentType,
                bytes,
                null);
            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    internal static bool TryDecodeDataUrl(
        string url,
        out string contentType,
        out byte[] bytes)
    {
        contentType = string.Empty;
        bytes = [];
        if (!url.StartsWith("data:", StringComparison.OrdinalIgnoreCase)) return false;
        var comma = url.IndexOf(',');
        if (comma < 5) return false;
        var metadata = url[5..comma];
        var base64 = metadata.EndsWith(";base64", StringComparison.OrdinalIgnoreCase);
        contentType = metadata.Split(';', 2)[0];
        if (contentType.Length == 0) contentType = "text/plain";
        try
        {
            bytes = base64
                ? Convert.FromBase64String(url[(comma + 1)..])
                : Encoding.UTF8.GetBytes(Uri.UnescapeDataString(url[(comma + 1)..]));
            return true;
        }
        catch (FormatException)
        {
            return false;
        }
    }

    private static string SafeFileName(string? value)
    {
        var fileName = Path.GetFileName(string.IsNullOrWhiteSpace(value) ? "download" : value);
        foreach (var invalid in Path.GetInvalidFileNameChars())
        {
            fileName = fileName.Replace(invalid, '_');
        }
        return string.IsNullOrWhiteSpace(fileName) ? "download" : fileName;
    }
}
