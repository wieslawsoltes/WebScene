using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class SvgPictureRenderingTests
{
    [Fact]
    public void GradientBackgroundDoesNotHideLaterForegroundPath()
    {
        const string markup = """
            <svg width="18" height="18" xmlns="http://www.w3.org/2000/svg">
              <path fill="url(#background)" d="M0 0h18v18H0z"/>
              <path d="M4 9l3 3 7-7 1.5 1.5L7 15 2.5 10.5z" fill="#fff"/>
              <defs><linearGradient id="background" x1="3" y1="3" x2="18" y2="18" gradientUnits="userSpaceOnUse"><stop stop-color="#1A1E21"/><stop offset="1" stop-color="#06060A"/></linearGradient></defs>
            </svg>
            """;

        using var lease = SharedSvgPictureCache.Acquire(markup);
        Assert.NotNull(lease);
        using var bitmap = new SKBitmap(18, 18);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.Transparent);
        canvas.DrawPicture(lease!.Picture);

        var hasBrightForeground = false;
        for (var y = 0; y < bitmap.Height && !hasBrightForeground; y++)
        {
            for (var x = 0; x < bitmap.Width; x++)
            {
                var pixel = bitmap.GetPixel(x, y);
                if (pixel.Red > 220 && pixel.Green > 220 && pixel.Blue > 220)
                {
                    hasBrightForeground = true;
                    break;
                }
            }
        }

        Assert.True(hasBrightForeground, "The SVG foreground path was absent from the rendered picture.");
    }
}
