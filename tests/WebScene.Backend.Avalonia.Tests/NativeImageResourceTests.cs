using System.Text;
using SkiaSharp;
using Svg.Skia;
using System.Net;
using System.Net.Sockets;
using WebScene.Core;
#if WEBSCENE_UNO
using WebScene.Backends.Uno.Native;
#else
using WebScene.Backends.Avalonia.Native;
#endif
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeImageResourceTests
{
    [Fact]
    public async Task HttpImageLoaderDoesNotDecodeBinaryAsText()
    {
        using var source = new SKBitmap(12, 8);
        source.Erase(SKColors.Red);
        using var image = SKImage.FromBitmap(source);
        using var encoded = image.Encode(SKEncodedImageFormat.Jpeg, 100);
        var bytes = encoded.ToArray();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var server = Task.Run(async () => {
            using var client = await listener.AcceptTcpClientAsync();
            await using var stream = client.GetStream();
            using var reader = new StreamReader(stream, Encoding.ASCII, false, leaveOpen: true);
            while (await reader.ReadLineAsync() is { Length: > 0 }) { }
            var headers = Encoding.ASCII.GetBytes($"HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {bytes.Length}\r\nConnection: close\r\n\r\n");
            await stream.WriteAsync(headers); await stream.WriteAsync(bytes);
        });
#if WEBSCENE_UNO
        var loader = new UnoResourceLoader();
#else
        var loader = new WebScene.Backends.Avalonia.AvaloniaResourceLoader();
#endif
        var resource = await Task.Run(() => loader.LoadText(new WebSceneResourceRequest(
            $"http://127.0.0.1:{port}/thumbnail.jpg", null, WebSceneResourceKind.Image)));
        await server.WaitAsync(TimeSpan.FromSeconds(10));
        Assert.Equal(NativeImageResource.ToMarkup(bytes), resource.Content);
    }

    [Theory]
    [InlineData(SKEncodedImageFormat.Jpeg)]
    [InlineData(SKEncodedImageFormat.Png)]
    [InlineData(SKEncodedImageFormat.Webp)]
    public void RasterBytesReachExistingSvgRenderer(SKEncodedImageFormat format)
    {
        using var source = new SKBitmap(12, 8);
        source.Erase(SKColors.Red);
        using var image = SKImage.FromBitmap(source);
        using var encoded = image.Encode(format, 100);
        var markup = NativeImageResource.ToMarkup(encoded.ToArray());
        Assert.Contains("viewBox=\"0 0 12 8\"", markup);
        using var svg = new SKSvg();
        using var stream = new MemoryStream(Encoding.UTF8.GetBytes(markup));
        svg.Load(stream);
        Assert.NotNull(svg.Picture);
        using var target = new SKBitmap(12, 8);
        using var canvas = new SKCanvas(target);
        canvas.Clear(SKColors.Transparent);
        canvas.DrawPicture(svg.Picture);
        var pixel = target.GetPixel(6, 4);
        Assert.True(pixel.Red > 240 && pixel.Green < 15 && pixel.Blue < 15 && pixel.Alpha == 255,
            $"Raster thumbnail did not survive the native SVG envelope: {pixel}");
    }

    [Fact]
    public void SvgAndMalformedTextKeepExistingNativeValidation()
    {
        const string svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 12 8'/>";
        Assert.Equal(svg, NativeImageResource.ToMarkup(Encoding.UTF8.GetBytes(svg)));
        Assert.Equal("not an image", NativeImageResource.ToMarkup(Encoding.UTF8.GetBytes("not an image")));
    }

    [Fact]
    public void OversizedEncodedResourcesAreRejected()
        => Assert.Throws<InvalidDataException>(() => NativeImageResource.ToMarkup(new byte[NativeImageResource.MaximumEncodedBytes + 1]));
}
