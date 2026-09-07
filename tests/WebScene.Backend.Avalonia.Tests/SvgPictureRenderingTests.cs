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

    [Fact]
    public void TradingViewOpacityPatternRepeatsAcrossTheEntireSwatch()
    {
        const string markup = """
            <svg width="8" height="8" fill="none" xmlns="http://www.w3.org/2000/svg"><path fill="#2A2E39" fill-opacity="0.4" d="M0 0h4v4H0zM4 4h4v4H4z"/></svg>
            """;
        const string resource = "webscene-bg-svg-v1\t0 0 8 8\trepeat\t0% 0%\t50%\t"
            + markup;
        var command = new SceneCommand { Width = 24, Height = 24 };
        var baseColor = new SKColor(5, 7, 12);
        using var bitmap = new SKBitmap(24, 24, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(baseColor);
        var renderer = new NativeCanvasSceneRenderer();

        renderer.DrawDomSvgBackgroundForTest(canvas, resource, command);
        canvas.Flush();

        var firstDarkSquare = bitmap.GetPixel(2, 2);
        var firstClearSquare = bitmap.GetPixel(8, 2);
        Assert.NotEqual(baseColor, firstDarkSquare);
        Assert.Equal(baseColor, firstClearSquare);
        Assert.Equal(firstDarkSquare, bitmap.GetPixel(14, 2));
        Assert.Equal(firstClearSquare, bitmap.GetPixel(20, 2));
        Assert.Equal(firstClearSquare, bitmap.GetPixel(2, 8));
        Assert.Equal(firstDarkSquare, bitmap.GetPixel(8, 8));
        Assert.Equal(firstDarkSquare, bitmap.GetPixel(20, 20));
    }

    [Theory]
    [InlineData("#9de640", 157, 230, 64)]
    [InlineData("#d19afc", 209, 154, 252)]
    public void ReleaseNoteSvgBackgroundPreservesExactSrgbFill(string fill, byte red, byte green, byte blue)
    {
        var markup = $"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"48\" height=\"20\"><rect width=\"48\" height=\"20\" fill=\"{fill}\"/></svg>";
        var resource = "webscene-bg-svg-v1\t0 0 48 20\tno-repeat\t0% 0%\t48px\t" + markup;
        using var bitmap = new SKBitmap(80, 28, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        var background = new SKColor(11, 24, 26);
        canvas.Clear(background);
        new NativeCanvasSceneRenderer().DrawDomSvgBackgroundForTest(canvas, resource,
            new SceneCommand { Width = 80, Height = 28 });
        canvas.Flush();
        for (var y = 1; y < 19; y++)
            for (var x = 1; x < 47; x++)
                Assert.Equal(new SKColor(red, green, blue), bitmap.GetPixel(x, y));
        Assert.Equal(background, bitmap.GetPixel(60, 10));
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
