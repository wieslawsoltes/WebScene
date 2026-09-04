using System.Text;
using SkiaSharp;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

// The native image resource seam consumes SVG. Preserve encoded raster bytes
// inside that envelope instead of passing JPEG/PNG through UTF-8 decoding.
internal static class NativeImageResource
{
    internal const int MaximumEncodedBytes = 16 * 1024 * 1024;
    internal const long MaximumPixels = 16 * 1024 * 1024;

    internal static string ToMarkup(byte[] bytes)
    {
        if (bytes.Length > MaximumEncodedBytes)
            throw new InvalidDataException("Image resource exceeds the encoded size limit.");
        using var data = SKData.CreateCopy(bytes);
        using var codec = SKCodec.Create(data);
        if (codec is null) return Encoding.UTF8.GetString(bytes); // SVG remains unchanged.
        var info = codec.Info;
        if (info.Width <= 0 || info.Height <= 0 || (long)info.Width * info.Height > MaximumPixels)
            throw new InvalidDataException("Image resource exceeds the decoded size limit.");
        var mime = codec.EncodedFormat switch
        {
            SKEncodedImageFormat.Jpeg => "image/jpeg",
            SKEncodedImageFormat.Png => "image/png",
            SKEncodedImageFormat.Webp => "image/webp",
            SKEncodedImageFormat.Gif => "image/gif",
            _ => throw new InvalidDataException("Unsupported raster image format.")
        };
        return $"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" " +
            $"viewBox=\"0 0 {info.Width} {info.Height}\" width=\"{info.Width}\" height=\"{info.Height}\">" +
            $"<image width=\"{info.Width}\" height=\"{info.Height}\" xlink:href=\"data:{mime};base64,{Convert.ToBase64String(bytes)}\"/></svg>";
    }
}
