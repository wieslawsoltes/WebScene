using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class SvgPictureRenderingTests
{
    [Fact]
    public void NonZeroViewBoxOriginCentersPictureExactlyOnce()
    {
        const string reportedMarkup = """
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="-1 -1 21 19" width="21" height="19">
              <path fill="#000" d="M2.5 1C1.67 1 1 1.67 1 2.5v12c0 .83.67 1.5 1.5 1.5h14c.83 0 1.5-.67 1.5-1.5v-12c0-.83-.67-1.5-1.5-1.5h-14ZM0 2.5A2.5 2.5 0 0 1 2.5 0h14A2.5 2.5 0 0 1 19 2.5v12a2.5 2.5 0 0 1-2.5 2.5h-14A2.5 2.5 0 0 1 0 14.5v-12Z"/>
            </svg>
            """;
        const string centeredReference = """
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 21 19" width="21" height="19">
              <path transform="translate(1 1)" fill="#000" d="M2.5 1C1.67 1 1 1.67 1 2.5v12c0 .83.67 1.5 1.5 1.5h14c.83 0 1.5-.67 1.5-1.5v-12c0-.83-.67-1.5-1.5-1.5h-14ZM0 2.5A2.5 2.5 0 0 1 2.5 0h14A2.5 2.5 0 0 1 19 2.5v12a2.5 2.5 0 0 1-2.5 2.5h-14A2.5 2.5 0 0 1 0 14.5v-12Z"/>
            </svg>
            """;

        using var reported = SharedSvgPictureCache.Acquire(reportedMarkup);
        using var reference = SharedSvgPictureCache.Acquire(centeredReference);
        Assert.NotNull(reported);
        Assert.NotNull(reference);
        using var actual = RenderLayoutIcon(
            reported!.Picture,
            [-1, -1, 21, 19]);
        using var expected = RenderLayoutIcon(
            reference!.Picture,
            [0, 0, 21, 19]);

        for (var y = 0; y < actual.Height; y++)
        {
            for (var x = 0; x < actual.Width; x++)
            {
                Assert.Equal(expected.GetPixel(x, y), actual.GetPixel(x, y));
            }
        }
    }

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

    private static SKBitmap RenderLayoutIcon(SKPicture picture, float[] viewBox)
    {
        var bitmap = new SKBitmap(29, 27);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.White);
        NativeCanvasSceneRenderer.DrawSvgPictureInViewport(
            canvas,
            picture,
            new SKRect(4, 4, 25, 23),
            viewBox,
            "xMidYMid meet");
        return bitmap;
    }
}
