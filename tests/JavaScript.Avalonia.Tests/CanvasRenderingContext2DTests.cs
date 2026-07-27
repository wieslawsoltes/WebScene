using System;
using System.Linq;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using System.Runtime.ExceptionServices;
using Avalonia.Media.Immutable;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Avalonia.VisualTree;
using JavaScript.Avalonia;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public class CanvasRenderingContext2DTests
{
    [AvaloniaFact]
    public void ImageSmoothingStateValidatesQualityAndParticipatesInSaveRestore()
    {
        var context = new CanvasDrawingSurface().Context;

        Assert.True(context.imageSmoothingEnabled);
        Assert.Equal("low", context.imageSmoothingQuality);

        context.save();
        context.imageSmoothingEnabled = false;
        context.imageSmoothingQuality = "high";
        context.imageSmoothingQuality = "invalid";

        Assert.False(context.imageSmoothingEnabled);
        Assert.Equal("high", context.imageSmoothingQuality);

        context.restore();

        Assert.True(context.imageSmoothingEnabled);
        Assert.Equal("low", context.imageSmoothingQuality);
    }

    [AvaloniaFact]
    public void CanvasMembersIgnoreNonFiniteArguments()
    {
        var surface = new CanvasDrawingSurface();
        var context = surface.Context;

        context.fillRect(double.NaN, 0, 10, 10);
        context.strokeRect(0, 0, 0, 0);
        context.fillText("non-finite", 1, double.NaN);
        context.strokeText("non-finite", 1, double.PositiveInfinity);

        Assert.Empty(surface.Commands);
    }

    [AvaloniaFact]
    public void NegativeCanvasRectDimensionsNormalizeWithoutChangingCoverage()
    {
        var surface = new CanvasDrawingSurface();

        surface.Context.fillRect(10, 10, -4, -6);

        var command = Assert.IsType<FillRectCommand>(Assert.Single(surface.Commands));
        Assert.Equal(new Rect(6, 4, 4, 6), command.GetBounds());
    }

    [AvaloniaFact]
    public void AdjacentFillRectsWithMatchingStateShareOnePhysicalCommand()
    {
        var surface = new CanvasDrawingSurface();
        var context = surface.Context;

        context.fillStyle = "#ff0000";
        context.fillRect(1, 2, 3, 4);
        context.fillRect(10, 20, 30, 40);

        var command = Assert.IsType<FillRectCommand>(Assert.Single(surface.Commands));
        Assert.Equal(2, surface.LogicalCommandCount);
        Assert.Equal(2, command.RectangleCount);
        Assert.Equal(
            new[] { new Rect(1, 2, 3, 4), new Rect(10, 20, 30, 40) },
            command.Rectangles.ToArray());
        Assert.Equal(new Rect(1, 2, 39, 58), command.GetBounds());

        context.fillStyle = "#0000ff";
        context.fillRect(50, 60, 7, 8);

        Assert.Equal(2, surface.Commands.Count);
        Assert.Equal(3, surface.LogicalCommandCount);
    }

    [AvaloniaFact]
    public void CoalescedFillRectsAllRender()
    {
        var surface = new CanvasDrawingSurface
        {
            Width = 300,
            Height = 100,
            VirtualWidth = 300,
            VirtualHeight = 100
        };
        var window = new Window
        {
            Width = 300,
            Height = 100,
            Content = surface
        };
        var context = surface.Context;
        context.fillStyle = "#008000";
        for (var x = 0; x < 300; x++)
        {
            context.fillRect(x, x % 50, 1, 40);
        }

        var command = Assert.IsType<FillRectCommand>(Assert.Single(surface.Commands));
        Assert.Equal(300, command.RectangleCount);
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
        foreach (var x in new[] { 0, 49, 50, 149, 150, 249, 299 })
        {
            var pixel = ReadPixel(frame!, x, x % 50 + 10);
            Assert.True(
                pixel.G > 100 && pixel.R < 40 && pixel.B < 40,
                $"Expected coalesced rectangle {x} to render green, got {pixel}.");
        }
    }

    [AvaloniaFact]
    public void StrokeRectWithOneZeroDimensionRetainsLineGeometry()
    {
        var surface = new CanvasDrawingSurface();

        surface.Context.strokeRect(10, 10, 0, -6);

        var command = Assert.IsType<StrokeRectCommand>(Assert.Single(surface.Commands));
        Assert.Equal(new Rect(9.5, 3.5, 1, 7), command.GetBounds());
    }

    [Fact]
    public void CssRgbaCanvasColors_ParseWithoutExceptionFallback()
    {
        var exceptions = 0;
        EventHandler<FirstChanceExceptionEventArgs> handler = (_, _) => exceptions++;
        AppDomain.CurrentDomain.FirstChanceException += handler;
        try
        {
            var color = CanvasColorParser.ParseColor("rgba(247, 82, 95, 0.5)", Colors.Black);
            Assert.Equal(Color.FromArgb(128, 247, 82, 95), color);
        }
        finally
        {
            AppDomain.CurrentDomain.FirstChanceException -= handler;
        }

        Assert.Equal(0, exceptions);
    }

    [AvaloniaFact]
    public void TransformedFullCanvasClear_DropsSupersededRetainedCommands()
    {
        var surface = new CanvasDrawingSurface
        {
            VirtualWidth = 100,
            VirtualHeight = 80
        };
        var context = surface.Context;
        context.fillRect(0, 0, 100, 80);
        context.scale(2, 2);

        context.clearRect(0, 0, 50, 40);

        Assert.Empty(surface.Commands);
    }

    [AvaloniaFact]
    public void OpaqueFullCanvasFill_DropsSupersededRetainedCommands()
    {
        var surface = new CanvasDrawingSurface
        {
            VirtualWidth = 100,
            VirtualHeight = 80
        };
        var context = surface.Context;
        context.fillStyle = "#ff0000";
        context.fillRect(0, 0, 20, 20);
        context.fillStyle = "#ffffff";
        context.fillRect(0, 0, 100, 80);

        Assert.Single(surface.Commands);
        Assert.IsType<FillRectCommand>(surface.Commands[0]);
    }

    [AvaloniaFact]
    public void TranslucentFullCanvasFill_PreservesSupersededRetainedCommands()
    {
        var surface = new CanvasDrawingSurface
        {
            VirtualWidth = 100,
            VirtualHeight = 80
        };
        var context = surface.Context;
        context.fillRect(0, 0, 20, 20);
        context.globalAlpha = 0.5;
        context.fillRect(0, 0, 100, 80);

        Assert.Equal(2, surface.Commands.Count);
    }

    [AvaloniaFact]
    public void CopyModeOpaqueFullCanvasFill_DropsSupersededRetainedCommands()
    {
        var surface = new CanvasDrawingSurface
        {
            VirtualWidth = 100,
            VirtualHeight = 80
        };
        var context = surface.Context;
        context.fillRect(0, 0, 20, 20);
        context.globalCompositeOperation = "copy";
        context.fillRect(0, 0, 100, 80);

        Assert.Single(surface.Commands);
    }

    [AvaloniaFact]
    public void FillRect_UsesLinearGradientBrush()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        var gradient = ctx.createLinearGradient(0, 0, 100, 0);
        gradient.addColorStop(0, "#ff0000");
        gradient.addColorStop(1, "#0000ff");
        ctx.fillStyle = gradient;

        ctx.fillRect(0, 0, 50, 10);

        var command = Assert.IsType<FillRectCommand>(surface.Commands.Single());
        var brush = Assert.IsType<ImmutableLinearGradientBrush>(command.Snapshot.FillBrush);
        Assert.Equal(new RelativePoint(new Point(0, 0), RelativeUnit.Absolute), brush.StartPoint);
        Assert.Equal(new RelativePoint(new Point(100, 0), RelativeUnit.Absolute), brush.EndPoint);
        Assert.Equal(2, brush.GradientStops.Count);
        Assert.Equal(0, brush.GradientStops[0].Offset);
        Assert.Equal(Colors.Red, brush.GradientStops[0].Color);
        Assert.Equal(1, brush.GradientStops[1].Offset);
        Assert.Equal(Colors.Blue, brush.GradientStops[1].Color);

        var model = Assert.IsType<WebScene.Graphics.CanvasLinearGradientModel>(gradient.Model);
        Assert.Equal(new WebScene.Core.WebScenePoint(0, 0), model.Start);
        Assert.Equal(new WebScene.Core.WebScenePoint(100, 0), model.End);
        Assert.Equal(
            new[]
            {
                new WebScene.Graphics.CanvasGradientStop(0, WebScene.Core.WebSceneColor.FromRgb(255, 0, 0)),
                new WebScene.Graphics.CanvasGradientStop(1, WebScene.Core.WebSceneColor.FromRgb(0, 0, 255))
            },
            model.Stops);
    }

    [AvaloniaFact]
    public void RadialGradientBrushProjectsPortableGradientModel()
    {
        var gradient = new CanvasRadialGradient(1, 2, 3, 4, 5, 6);
        gradient.addColorStop(.25, "rgba(10, 20, 30, 0.5)");

        var model = Assert.IsType<WebScene.Graphics.CanvasRadialGradientModel>(gradient.Model);
        Assert.Equal(new WebScene.Core.WebScenePoint(1, 2), model.StartCenter);
        Assert.Equal(3, model.StartRadius);
        Assert.Equal(new WebScene.Core.WebScenePoint(4, 5), model.EndCenter);
        Assert.Equal(6, model.EndRadius);
        var stop = Assert.Single(model.Stops);
        Assert.Equal(.25, stop.Offset);
        Assert.Equal((byte)10, stop.Color.R);
        Assert.Equal((byte)20, stop.Color.G);
        Assert.Equal((byte)30, stop.Color.B);

        var brush = Assert.IsType<ImmutableRadialGradientBrush>(gradient.ToImmutableBrush());
        Assert.Equal(new RelativePoint(new Point(1, 2), RelativeUnit.Absolute), brush.GradientOrigin);
        Assert.Equal(new RelativePoint(new Point(4, 5), RelativeUnit.Absolute), brush.Center);
        Assert.Equal(new RelativeScalar(6, RelativeUnit.Absolute), brush.RadiusX);
    }

    [AvaloniaFact]
    public void DrawImage_RecordsDestinationRect()
    {
        using var bitmap = new WriteableBitmap(new PixelSize(12, 16), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Premul);
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.drawImage(bitmap, 5, 6, 30, 40);

        var command = Assert.IsType<DrawImageCommand>(surface.Commands.Single());
        var bounds = command.GetBounds();
        Assert.True(bounds.HasValue);
        Assert.Equal(new Rect(5, 6, 30, 40), bounds.Value);
    }

    [AvaloniaFact]
    public void CreatePattern_UsesTiledImageBrush()
    {
        using var bitmap = new WriteableBitmap(new PixelSize(4, 4), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Premul);
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        var pattern = ctx.createPattern(bitmap, "repeat");

        Assert.NotNull(pattern);
        ctx.fillStyle = pattern;
        ctx.fillRect(0, 0, 20, 20);

        var command = Assert.IsType<FillRectCommand>(surface.Commands.Single());
        var brush = Assert.IsAssignableFrom<ITileBrush>(command.Snapshot.FillBrush);
        Assert.Equal(TileMode.Tile, brush.TileMode);
        Assert.Equal(new RelativeRect(new Rect(0, 0, 4, 4), RelativeUnit.Absolute), brush.DestinationRect);
    }

    [AvaloniaFact]
    public void ImageData_RoundTripsPixelsForCanvasInterop()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;
        var imageData = ctx.createImageData(2, 1);
        imageData.data[0] = 10;
        imageData.data[1] = 20;
        imageData.data[2] = 30;
        imageData.data[3] = 255;
        imageData.data[4] = 40;
        imageData.data[5] = 50;
        imageData.data[6] = 60;
        imageData.data[7] = 128;

        ctx.putImageData(imageData, 0, 0);

        var copy = ctx.getImageData(0, 0, 2, 1);
        Assert.Equal(imageData.data, copy.data);
        Assert.True(ctx.TryGetRgbaPixels(out var width, out var height, out var pixels));
        Assert.Equal(2, width);
        Assert.Equal(1, height);
        Assert.Equal(imageData.data, pixels);
        Assert.Same(imageData.data, imageData.Model.RgbaPixels);
        Assert.Equal((2, 1), (imageData.Model.Width, imageData.Model.Height));

        var command = Assert.IsType<DrawRgbaPixelsCommand>(Assert.Single(surface.Commands));
        Assert.NotSame(imageData.data, command.ImageData.RgbaPixels);
        Assert.Equal(imageData.data, command.ImageData.RgbaPixels);
    }

    [AvaloniaFact]
    public void CanvasImageDataNormalizesInputToPortableModelLength()
    {
        var shortData = new CanvasImageData(1, 1, new byte[] { 1, 2 });
        var longData = new CanvasImageData(1, 1, new byte[] { 1, 2, 3, 4, 5 });

        Assert.Equal(new byte[] { 1, 2, 0, 0 }, shortData.data);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, longData.data);
        Assert.Equal(4, shortData.Model.RgbaPixels.Length);
        Assert.Equal(4, longData.Model.RgbaPixels.Length);
    }

    [AvaloniaFact]
    public void CanvasPathBuilderRetainsEveryOperationInPortableModel()
    {
        var builder = new CanvasPathBuilder();
        builder.MoveTo(1, 2);
        builder.LineTo(3, 4);
        builder.CubicBezierTo(5, 6, 7, 8, 9, 10);
        builder.QuadraticBezierTo(11, 12, 13, 14);
        builder.Arc(15, 16, 17, 18, 19, true);
        builder.ArcTo(20, 21, 22, 23, 24);
        builder.Rect(25, 26, 27, 28);
        builder.ClosePath();

        Assert.Equal(
            new[]
            {
                WebScene.Graphics.CanvasPathCommandKind.MoveTo,
                WebScene.Graphics.CanvasPathCommandKind.LineTo,
                WebScene.Graphics.CanvasPathCommandKind.CubicBezierTo,
                WebScene.Graphics.CanvasPathCommandKind.QuadraticBezierTo,
                WebScene.Graphics.CanvasPathCommandKind.Arc,
                WebScene.Graphics.CanvasPathCommandKind.ArcTo,
                WebScene.Graphics.CanvasPathCommandKind.Rect,
                WebScene.Graphics.CanvasPathCommandKind.ClosePath
            },
            builder.Model.Commands.Select(static command => command.Kind));
        Assert.Equal(17, builder.Model.Commands[4].Radius);
        Assert.True(builder.Model.Commands[4].Flag);
        Assert.Equal(24, builder.Model.Commands[5].Radius);
        Assert.True(builder.BuildGeometry().Bounds.Width > 0);

        var retained = builder.Model;
        builder.Clear();
        Assert.True(builder.IsEmpty);
        Assert.Equal(8, retained.Commands.Count);
    }

    [AvaloniaFact]
    public void RetainedCanvasStateAndClipArePortableAndDriveAvaloniaProjection()
    {
        var surface = new CanvasDrawingSurface();
        var context = surface.Context;
        var gradient = context.createLinearGradient(0, 0, 20, 0);
        gradient.addColorStop(0, "#102030");
        gradient.addColorStop(1, "#405060");
        context.fillStyle = gradient;
        context.strokeStyle = "rgba(1, 2, 3, 0.5)";
        context.globalAlpha = .75;
        context.globalCompositeOperation = "multiply";
        context.lineWidth = 3;
        context.lineCap = "round";
        context.lineJoin = "bevel";
        context.setLineDash(new double[] { 2, 4 });
        context.lineDashOffset = 1;
        context.textAlign = "center";
        context.textBaseline = "middle";
        context.imageSmoothingEnabled = false;
        context.imageSmoothingQuality = "high";
        context.shadowColor = "#112233";
        context.shadowBlur = 5;
        context.shadowOffsetX = 6;
        context.shadowOffsetY = 7;
        context.translate(8, 9);
        context.beginPath();
        context.rect(1, 2, 30, 40);
        context.clip();
        context.beginPath();
        context.fillRect(0, 0, 10, 10);

        var command = Assert.IsType<FillRectCommand>(Assert.Single(surface.Commands));
        var state = command.Snapshot.Model;
        Assert.IsType<WebScene.Graphics.CanvasGradientPaintModel>(state.FillStyle);
        var stroke = Assert.IsType<WebScene.Graphics.CanvasColorPaintModel>(state.StrokeStyle);
        Assert.Equal((byte)1, stroke.Color.R);
        Assert.Equal(.75, state.GlobalAlpha);
        Assert.Equal(WebScene.Graphics.CanvasCompositeOperation.Multiply, state.CompositeOperation);
        Assert.Equal(WebScene.Graphics.CanvasLineCap.Round, state.LineCap);
        Assert.Equal(WebScene.Graphics.CanvasLineJoin.Bevel, state.LineJoin);
        Assert.Equal(new double[] { 2, 4 }, state.LineDash);
        Assert.Equal(1, state.LineDashOffset);
        Assert.Equal(WebScene.Graphics.CanvasTextAlign.Center, state.TextAlign);
        Assert.Equal(WebScene.Graphics.CanvasTextBaseline.Middle, state.TextBaseline);
        Assert.False(state.ImageSmoothingEnabled);
        Assert.Equal(WebScene.Graphics.CanvasImageSmoothingQuality.High, state.ImageSmoothingQuality);
        Assert.Equal((5d, 6d, 7d), (state.Shadow.Blur, state.Shadow.OffsetX, state.Shadow.OffsetY));
        Assert.Equal(new WebScene.Graphics.GraphicsTransform(1, 0, 0, 1, 8, 9), state.Transform);
        var clip = Assert.Single(state.Clips);
        Assert.Equal(state.Transform, clip.Transform);
        Assert.Equal(WebScene.Graphics.CanvasPathCommandKind.Rect, Assert.Single(clip.Path.Commands).Kind);
        Assert.NotNull(command.Snapshot.ClipGeometry);
        Assert.Equal(new Rect(9, 11, 30, 40), command.Snapshot.ClipGeometry!.Bounds);
    }

    [AvaloniaFact]
    public void SaveRestoreIncludesPortableCompositeAndShadowState()
    {
        var context = new CanvasDrawingSurface().Context;
        context.globalCompositeOperation = "screen";
        context.shadowColor = "#abcdef";
        context.shadowBlur = 4;
        context.shadowOffsetX = 5;
        context.shadowOffsetY = 6;
        context.save();
        context.globalCompositeOperation = "copy";
        context.shadowColor = "transparent";
        context.shadowBlur = 0;
        context.shadowOffsetX = 0;
        context.shadowOffsetY = 0;

        context.restore();

        Assert.Equal("screen", context.globalCompositeOperation);
        Assert.Equal("#abcdef", context.shadowColor);
        Assert.Equal(4, context.shadowBlur);
        Assert.Equal(5, context.shadowOffsetX);
        Assert.Equal(6, context.shadowOffsetY);
    }

    [AvaloniaFact]
    public void RepeatedImmutableCanvasStateReusesPortableAndAvaloniaProjections()
    {
        var surface = new CanvasDrawingSurface();
        var context = surface.Context;
        context.beginPath();
        context.rect(0, 0, 20, 20);
        context.clip();

        context.fillRect(1, 1, 2, 2);
        context.strokeRect(4, 4, 2, 2);

        var fill = Assert.IsType<FillRectCommand>(surface.Commands[0]);
        var stroke = Assert.IsType<StrokeRectCommand>(surface.Commands[1]);
        Assert.Same(fill.Snapshot.Model, stroke.Snapshot.Model);
        Assert.Same(fill.Snapshot.ClipGeometry, stroke.Snapshot.ClipGeometry);
        Assert.Same(
            surface.PortableDisplayList.Commands[0].State,
            surface.PortableDisplayList.Commands[1].State);
    }

    [AvaloniaFact]
    public void PortableDisplayListMirrorsEveryRetainedCanvasCommandKind()
    {
        using var bitmap = new WriteableBitmap(
            new PixelSize(2, 2),
            new Vector(96, 96),
            PixelFormat.Bgra8888,
            AlphaFormat.Premul);
        var surface = new CanvasDrawingSurface();
        var context = surface.Context;

        context.fillRect(1, 1, 2, 2);
        context.fillRect(4, 4, 2, 2); // physically coalesced, logically retained twice
        context.strokeRect(7, 7, 2, 2);
        context.clearRect(10, 10, 2, 2);
        context.beginPath();
        context.rect(13, 13, 2, 2);
        context.fill();
        context.stroke();
        context.fillText("fill", 16, 16);
        context.strokeText("stroke", 19, 19);
        context.drawImage(bitmap, 22, 22, 2, 2);
        var imageData = context.createImageData(1, 1);
        imageData.data[3] = 255;
        context.putImageData(imageData, 25, 25);

        var commands = surface.PortableDisplayList.Commands;
        Assert.Equal(surface.LogicalCommandCount, commands.Count);
        Assert.Same(commands[0].State, commands[1].State);
        Assert.Same(
            Assert.IsType<FillRectCommand>(surface.Commands[0]).Snapshot.Model,
            commands[0].State);
        Assert.Equal(
            new[]
            {
                WebScene.Graphics.CanvasDisplayCommandKind.FillRectangle,
                WebScene.Graphics.CanvasDisplayCommandKind.FillRectangle,
                WebScene.Graphics.CanvasDisplayCommandKind.StrokeRectangle,
                WebScene.Graphics.CanvasDisplayCommandKind.ClearRectangle,
                WebScene.Graphics.CanvasDisplayCommandKind.FillPath,
                WebScene.Graphics.CanvasDisplayCommandKind.StrokePath,
                WebScene.Graphics.CanvasDisplayCommandKind.FillText,
                WebScene.Graphics.CanvasDisplayCommandKind.StrokeText,
                WebScene.Graphics.CanvasDisplayCommandKind.DrawImage,
                WebScene.Graphics.CanvasDisplayCommandKind.PutImageData
            },
            commands.Select(static command => command.Kind));
        Assert.NotNull(commands[4].Path);
        Assert.NotNull(commands[5].Path);
        Assert.Equal("fill", commands[6].Text);
        Assert.False(commands[8].Resource.IsEmpty);
        Assert.NotNull(commands[9].ImageData);
        Assert.Equal(new WebScene.Core.WebSceneRect(25, 25, 1, 1), commands[9].DestinationRectangle);
    }

    [AvaloniaFact]
    public void PortableDisplayListIsTheAvaloniaRenderAuthority()
    {
        var surface = new CanvasDrawingSurface
        {
            Width = 4,
            Height = 4,
            VirtualWidth = 4,
            VirtualHeight = 4
        };
        surface.PortableDisplayList.Add(new WebScene.Graphics.CanvasDisplayCommand(
            WebScene.Graphics.CanvasDisplayCommandKind.FillRectangle,
            WebScene.Graphics.CanvasStateModel.Default with
            {
                FillStyle = new WebScene.Graphics.CanvasColorPaintModel(
                    WebScene.Core.WebSceneColor.FromRgb(255, 0, 0))
            },
            Rectangle: new WebScene.Core.WebSceneRect(0, 0, 4, 4)));
        Assert.Empty(surface.Commands);

        var window = new Window { Width = 4, Height = 4, Content = surface };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixel = ReadPixel(frame, 2, 2);
            Assert.True(pixel.R > 250 && pixel.G < 4 && pixel.B < 4,
                $"Expected the portable-only retained command to render red, got {pixel}.");
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void SharedPortableCanvasPacketMatchesReferenceAndAvaloniaPixels()
    {
        double[] packet =
        [
            40, 1, 0,
            22, 4, 0, 0, 10, 10,
            46, 1, 0.5,
            40, 1, 1,
            22, 4, 5, 0, 5, 10
        ];
        string[] strings = ["#ff0000", "#0000ff"];
        var reference = new WebScene.Graphics.CanvasReferenceRenderer();
        reference.Replay(packet, strings, new WebScene.Core.WebSceneSize(10, 10));

        var surface = new CanvasDrawingSurface
        {
            Width = 10,
            Height = 10,
            VirtualWidth = 10,
            VirtualHeight = 10
        };
        Assert.Equal(5, AvaloniaCanvasBatchReplay.Replay(surface.Context, packet, strings));
        var window = new Window { Width = 10, Height = 10, Content = surface };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            AssertPixelWithin(reference.Surface.GetPixel(3, 5), ReadPixel(frame, 3, 5), 1);
            AssertPixelWithin(reference.Surface.GetPixel(7, 5), ReadPixel(frame, 7, 5), 1);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void PortableTextCommandRendersWithinReviewedHeadlessPixelTolerance()
    {
        var surface = new CanvasDrawingSurface
        {
            Width = 80,
            Height = 32,
            VirtualWidth = 80,
            VirtualHeight = 32
        };
        surface.PortableDisplayList.Add(new WebScene.Graphics.CanvasDisplayCommand(
            WebScene.Graphics.CanvasDisplayCommandKind.FillText,
            WebScene.Graphics.CanvasStateModel.Default with
            {
                FillStyle = new WebScene.Graphics.CanvasColorPaintModel(
                    WebScene.Core.WebSceneColor.FromRgb(255, 0, 0)),
                Font = "20px sans-serif",
                TextBaseline = WebScene.Graphics.CanvasTextBaseline.Top
            },
            Text: "R3",
            Origin: new WebScene.Core.WebScenePoint(2, 2)));
        Assert.Empty(surface.Commands);

        var window = new Window { Width = 80, Height = 32, Content = surface };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var painted = CountRedPixels(frame);

            // The headless backend and installed system face may differ slightly,
            // but this range detects missing, displaced, or grossly scaled text.
            Assert.InRange(painted, 20, 600);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void Path2DStoresSvgAndTransformedAddPathInPortableModel()
    {
        var source = new CanvasPath2D("M 0 0 L 10 0 L 0 10 Z");
        source.rect(20, 20, 5, 5);
        var target = new CanvasPath2D();
        target.addPath(source, 2, 0, 0, 2, 3, 4);
        var surface = new CanvasDrawingSurface();

        surface.Context.fill(target);

        var command = Assert.IsType<FillPathCommand>(Assert.Single(surface.Commands));
        var path = Assert.Single(surface.PortableDisplayList.Commands).Path!;
        var part = Assert.Single(path.Parts);
        Assert.Equal("M 0 0 L 10 0 L 0 10 Z", part.Path.SvgPathData);
        Assert.Equal(WebScene.Graphics.CanvasPathCommandKind.Rect, Assert.Single(part.Path.Commands).Kind);
        Assert.Equal(new WebScene.Graphics.GraphicsTransform(2, 0, 0, 2, 3, 4), part.Transform);
        Assert.True(command.Geometry.FillContains(new Point(5, 6)));
        Assert.True(command.Geometry.FillContains(new Point(45, 46)));
    }

    [AvaloniaFact]
    public void PutImageData_RendersPixelData()
    {
        var surface = new CanvasDrawingSurface
        {
            Width = 4,
            Height = 4
        };
        var window = new Window
        {
            Width = 4,
            Height = 4,
            Content = surface
        };
        var ctx = surface.Context;
        var imageData = ctx.createImageData(1, 1);
        imageData.data[0] = 255;
        imageData.data[1] = 0;
        imageData.data[2] = 0;
        imageData.data[3] = 255;

        ctx.putImageData(imageData, 1, 1);
        window.Show();

        using var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);

        var rendered = ReadPixel(frame!, 1, 1);
        Assert.True(rendered.R > 200, $"Expected putImageData to render a red pixel, but got {rendered}.");
    }

    [AvaloniaFact]
    public void DrawImage_FromCanvasDoesNotDuplicatePixelAndSurfacePaths()
    {
        var (host, _) = HostTestUtilities.CreateHost();
        var sourceCanvas = HostTestUtilities.GetElement(host.Document.createElement("canvas"));
        sourceCanvas.width = 4;
        sourceCanvas.height = 4;
        var sourceContext = Assert.IsType<CanvasRenderingContext2D>(sourceCanvas.getContext("2d"));
        var imageData = sourceContext.createImageData(1, 1);
        imageData.data[0] = 0;
        imageData.data[1] = 255;
        imageData.data[2] = 0;
        imageData.data[3] = 255;
        sourceContext.putImageData(imageData, 0, 0);

        var target = new CanvasDrawingSurface();
        var targetContext = target.Context;
        targetContext.drawImage(sourceCanvas, 0, 0);

        Assert.Single(target.Commands);
        Assert.IsType<DrawCanvasSurfaceCommand>(target.Commands[0]);
    }

    [AvaloniaFact]
    public void ScaleThenTranslate_ComposesUsingBrowserCanvasOrder()
    {
        var surface = new CanvasDrawingSurface();
        var context = surface.Context;
        context.scale(2, 2);
        context.translate(13, 462);
        context.fill(new CanvasPath2D("M 0 0 L 10 0 L 0 10 Z"));

        var command = Assert.IsType<FillPathCommand>(Assert.Single(surface.Commands));
        Assert.Equal(new Matrix(2, 0, 0, 2, 26, 924), command.Snapshot.Transform);
    }

    [AvaloniaFact]
    public void CanvasDimensionAssignment_ResetsContextStateAndCommands()
    {
        var (host, _) = HostTestUtilities.CreateHost();
        var canvas = HostTestUtilities.GetElement(host.Document.createElement("canvas"));
        canvas.width = 4;
        canvas.height = 4;
        var context = Assert.IsType<CanvasRenderingContext2D>(canvas.getContext("2d"));
        context.fillStyle = "#ff0000";
        context.fillRect(0, 0, 4, 4);

        canvas.width = 4;

        Assert.Equal(0, context.CommandCount);
        context.fillRect(0, 0, 1, 1);
        Assert.True(CanvasContextBridge.TryGetSurface(canvas.Control, out var surface));
        var command = Assert.IsType<FillRectCommand>(surface.Commands.Single());
        Assert.Equal(Colors.Black, Assert.IsAssignableFrom<ISolidColorBrush>(command.Snapshot.FillBrush).Color);
    }

    [AvaloniaFact]
    public void DomCanvas_ExplicitZeroBackingDimensionsDoNotRevertToDefaults()
    {
        var (host, _) = HostTestUtilities.CreateHost();
        var canvas = HostTestUtilities.GetElement(host.Document.createElement("canvas"));

        Assert.Equal(300, canvas.width);
        Assert.Equal(150, canvas.height);

        canvas.width = 0;
        canvas.height = 0;

        Assert.Equal(0, canvas.width);
        Assert.Equal(0, canvas.height);
        Assert.True(CanvasContextBridge.TryGetSurface(canvas.Control, out var surface));
        Assert.Equal(0, surface.VirtualWidth);
        Assert.Equal(0, surface.VirtualHeight);
    }

    [AvaloniaFact]
    public void ClearRect_RemovesOnlyEarlierPixelsAndAllowsLaterRedraw()
    {
        var surface = new CanvasDrawingSurface
        {
            Width = 10,
            Height = 10,
            VirtualWidth = 10,
            VirtualHeight = 10
        };
        var window = new Window
        {
            Width = 10,
            Height = 10,
            Background = Brushes.Lime,
            Content = surface
        };
        var context = surface.Context;
        context.fillStyle = "#ff0000";
        context.fillRect(0, 0, 10, 10);
        context.clearRect(2, 2, 6, 6);
        context.fillStyle = "#0000ff";
        context.fillRect(4, 4, 2, 2);

        window.Show();
        Dispatcher.UIThread.RunJobs();

        Assert.Contains(surface.Commands, command => command is CanvasClearRectCommand);
        using var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
        var untouched = ReadPixel(frame!, 1, 1);
        var cleared = ReadPixel(frame!, 3, 3);
        var redrawn = ReadPixel(frame!, 5, 5);
        Assert.True(untouched.R > 200 && untouched.G < 80, $"Expected untouched red pixel, got {untouched}.");
        Assert.True(cleared.G > 200 && cleared.R < 80, $"Expected cleared pixel to reveal the green backing, got {cleared}.");
        Assert.True(redrawn.B > 200 && redrawn.R < 80, $"Expected redraw after clear to be blue, got {redrawn}.");
    }

    [AvaloniaFact]
    public void DomCanvas_RendersInsideItsContainingPanelAtCssPixelSize()
    {
        var root = new CssLayoutPanel();
        var window = new Window
        {
            Width = 20,
            Height = 20,
            Content = root
        };
        var host = new AvaloniaBrowserHost(window);
        var body = HostTestUtilities.GetElement(host.Document.body);
        var canvas = HostTestUtilities.GetElement(host.Document.createElement("canvas"));

        body.appendChild(canvas);
        canvas.style.setProperty("width", "10px");
        canvas.style.setProperty("height", "10px");
        canvas.width = 20;
        canvas.height = 20;
        var context = Assert.IsType<CanvasRenderingContext2D>(canvas.getContext("2d"));
        context.fillStyle = "#ff0000";
        context.fillRect(0, 0, 20, 20);

        window.Show();
        Dispatcher.UIThread.RunJobs();

        Assert.Same(root, canvas.Control.Parent);
        Assert.True(CanvasContextBridge.TryGetSurface(canvas.Control, out var surface));
        Assert.Same(canvas.Control, surface);

        using var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
        var inside = ReadPixel(frame!, 8, 8);
        var outside = ReadPixel(frame!, 12, 12);
        Assert.True(inside.R > 200, $"Expected scaled in-tree canvas pixel to be red, got {inside}.");
        Assert.True(outside.G > 200 && outside.B > 200,
            $"Expected canvas clipping at its CSS bounds, got {outside}.");
    }

    [AvaloniaFact]
    public void DomCanvas_ZIndexControlsInTreePaintOrder()
    {
        var root = new CssLayoutPanel();
        var window = new Window
        {
            Width = 20,
            Height = 20,
            Content = root
        };
        var host = new AvaloniaBrowserHost(window);
        var body = HostTestUtilities.GetElement(host.Document.body);
        var top = HostTestUtilities.GetElement(host.Document.createElement("canvas"));
        var bottom = HostTestUtilities.GetElement(host.Document.createElement("canvas"));

        body.appendChild(top);
        body.appendChild(bottom);
        top.setAttribute("style", "position:absolute; left:0; top:0; width:20px; height:20px; z-index:10");
        bottom.setAttribute("style", "position:absolute; left:0; top:0; width:20px; height:20px; z-index:1");
        top.width = bottom.width = 20;
        top.height = bottom.height = 20;

        var topContext = Assert.IsType<CanvasRenderingContext2D>(top.getContext("2d"));
        var bottomContext = Assert.IsType<CanvasRenderingContext2D>(bottom.getContext("2d"));
        topContext.fillStyle = "#ff0000";
        topContext.fillRect(0, 0, 20, 20);
        bottomContext.fillStyle = "#0000ff";
        bottomContext.fillRect(0, 0, 20, 20);

        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
        var pixel = ReadPixel(frame!, 10, 10);
        Assert.True(pixel.R > 200 && pixel.B < 80,
            $"Expected the higher z-index canvas to paint on top, got {pixel}.");
    }

    [AvaloniaFact]
    public void MeasureText_ReturnsWidth()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.font = "18px Segoe UI";

        var metrics = Assert.IsType<CanvasTextMetrics>(ctx.measureText("xterm"));

        Assert.True(metrics.width > 0);
    }

    [AvaloniaFact]
    public void MeasureText_ResolvesCssSystemFontFallbackStack()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;
        ctx.font = "12px -apple-system, BlinkMacSystemFont, 'Trebuchet MS', Roboto, Ubuntu, sans-serif";

        var metrics = Assert.IsType<CanvasTextMetrics>(ctx.measureText("100.00"));
        Assert.True(metrics.width > 0);
        if (OperatingSystem.IsMacOS())
        {
            Assert.InRange(metrics.width, 38.0, 38.75);
        }
    }

    [AvaloniaFact]
    public void TransformAccessors_ReturnCanvasCompatibleMatrices()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.setTransform(2, 3, 4, 5, 6, 7);

        Assert.Equal(new[] { 2d, 3d, 4d, 5d, 6d, 7d }, ctx.mozCurrentTransform);
        Assert.Equal(new[] { -2.5d, 1.5d, 2d, -1d, 1d, -2d }, ctx.mozCurrentTransformInverse);

        var transform = Assert.IsType<CanvasDomMatrix>(ctx.getTransform());
        Assert.Equal(2, transform.a);
        Assert.Equal(3, transform.b);
        Assert.Equal(4, transform.c);
        Assert.Equal(5, transform.d);
        Assert.Equal(6, transform.e);
        Assert.Equal(7, transform.f);
    }

    [AvaloniaFact]
    public void StrokeText_RecordsTextCommand()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.strokeStyle = "#ff0000";
        ctx.strokeText("PDF.js", 12, 24);

        var command = Assert.IsType<StrokeTextCommand>(surface.Commands.Single());
        Assert.Equal(Colors.Red, Assert.IsAssignableFrom<ISolidColorBrush>(command.Snapshot.StrokeBrush).Color);
    }

    [AvaloniaFact]
    public void Font_ParsesCssFamilyListAndStyle()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.font = "italic bold 14px Menlo, Monaco, \"Courier New\", monospace";
        ctx.fillText("x", 0, 0);

        var command = Assert.IsType<FillTextCommand>(surface.Commands.Single());
        Assert.Equal(14, command.Snapshot.FontSize);
        Assert.Equal(FontStyle.Italic, command.Snapshot.Typeface.Style);
        Assert.Equal(FontWeight.Bold, command.Snapshot.Typeface.Weight);
        Assert.False(string.IsNullOrWhiteSpace(command.Snapshot.Typeface.FontFamily.Name));
        Assert.Equal(
            new[] { "Menlo", "Monaco", "Courier New", "monospace" },
            CssFontResolver.ParseFamilyList("Menlo, Monaco, \"Courier New\", monospace"));
    }

    [AvaloniaFact]
    public void Font_PreservesNumericWeightAndQuotedFamilyCommas()
    {
        var parsed = CssFontResolver.ParseFamilyList(
            "\"Missing, Family\", -apple-system, BlinkMacSystemFont, sans-serif");
        Assert.Equal(new[] { "Missing, Family", "-apple-system", "BlinkMacSystemFont", "sans-serif" }, parsed);

        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;
        ctx.font = "600 14px \"Missing, Family\", -apple-system, BlinkMacSystemFont, sans-serif";
        ctx.fillText("x", 0, 0);

        var command = Assert.IsType<FillTextCommand>(surface.Commands.Single());
        Assert.Equal(600, (int)command.Snapshot.Typeface.Weight);
        if (OperatingSystem.IsMacOS())
        {
            Assert.Equal(FontManager.Current.DefaultFontFamily.Name, command.Snapshot.Typeface.FontFamily.Name);
            Assert.InRange(command.Snapshot.FontWidthScale, 1.0, 1.02);
        }
    }

    [AvaloniaFact]
    public void Stroke_PreservesLineDashState()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.setLineDash(new[] { 4d, 2d });
        ctx.lineDashOffset = 1.5;
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(12, 0);
        ctx.stroke();

        var command = Assert.IsType<StrokePathCommand>(surface.Commands.Single());
        Assert.Equal(new[] { 4d, 2d }, command.Snapshot.LineDash);
        Assert.Equal(1.5, command.Snapshot.LineDashOffset);
    }

    [AvaloniaFact]
    public void BatchReplayOfRepeatedEqualLineDashReusesStateWithoutChangingStrokeOrder()
    {
        var surface = new CanvasDrawingSurface();
        double[] packet =
        [
            19, 3, 2, 4, 2,
            9, 0,
            11, 2, 0, 0,
            12, 2, 12, 0,
            20, 0,
            19, 3, 2, 4, 2,
            9, 0,
            11, 2, 0, 4,
            12, 2, 12, 4,
            20, 0
        ];

        Assert.Equal(10, AvaloniaCanvasBatchReplay.Replay(surface.Context, packet, []));

        var first = Assert.IsType<StrokePathCommand>(surface.Commands[0]);
        var second = Assert.IsType<StrokePathCommand>(surface.Commands[1]);
        Assert.Same(first.Snapshot.Model, second.Snapshot.Model);
        Assert.Equal(new[] { 0d, 4d }, new[] { first.Geometry.Bounds.Y, second.Geometry.Bounds.Y });
    }

    [AvaloniaFact]
    public void Arc_RendersCurvedCircleSegments()
    {
        var surface = new CanvasDrawingSurface
        {
            Width = 100,
            Height = 100
        };
        var window = new Window
        {
            Width = 100,
            Height = 100,
            Content = surface
        };
        var ctx = surface.Context;
        ctx.fillStyle = "#ff0000";
        ctx.beginPath();
        ctx.arc(50, 50, 30, 0, Math.PI * 2);
        ctx.fill();

        window.Show();

        var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
        using (frame)
        {
            var diagonalInsideCircle = ReadPixel(frame!, 68, 32);

            Assert.True(
                diagonalInsideCircle.R > 150,
                $"Expected a filled circular arc at the diagonal sample point, but got {diagonalInsideCircle}.");
        }
    }

    [AvaloniaFact]
    public void ArcTo_FourRoundedCornersFormCircularBadge()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        // This is the browser-observed construction used by a circular canvas
        // badge: four 90-degree arcTo corners with no straight segment between
        // them. A quadratic curve through each corner has the same bounds, but
        // bows outside the circle at the diagonals.
        ctx.beginPath();
        ctx.moveTo(15.5, 0);
        ctx.lineTo(15.5, 0);
        ctx.arcTo(31.5, 0, 31.5, 16, 16);
        ctx.lineTo(31.5, 16);
        ctx.arcTo(31.5, 32, 15.5, 32, 16);
        ctx.lineTo(15.5, 32);
        ctx.arcTo(-0.5, 32, -0.5, 16, 16);
        ctx.lineTo(-0.5, 16);
        ctx.arcTo(-0.5, 0, 15.5, 0, 16);
        ctx.closePath();
        ctx.fill();

        var command = Assert.IsType<FillPathCommand>(Assert.Single(surface.Commands));
        Assert.Equal(new Rect(-0.5, 0, 32, 32), command.Geometry.Bounds);

        // Center=(15.5,16), radius=16. This point is inside the quadratic
        // corner approximation but outside the browser's circular arc.
        Assert.False(command.Geometry.FillContains(new Point(27.4, 4.1)));
        Assert.True(command.Geometry.FillContains(new Point(26.5, 5)));
    }

    [AvaloniaFact]
    public void ArcTo_NearCircularOuterBadge_NormalizesShortStraightEdges()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        // Browser-observed outer badge path: a 34px square with
        // 16px corners. Chromium's antialiasing reads this as circular, while
        // retaining its 2px straight edges makes the WebScene badge look squared.
        ctx.beginPath();
        ctx.moveTo(14.5, -1);
        ctx.lineTo(16.5, -1);
        ctx.arcTo(32.5, -1, 32.5, 15, 16);
        ctx.lineTo(32.5, 17);
        ctx.arcTo(32.5, 33, 16.5, 33, 16);
        ctx.lineTo(14.5, 33);
        ctx.arcTo(-1.5, 33, -1.5, 17, 16);
        ctx.lineTo(-1.5, 15);
        ctx.arcTo(-1.5, -1, 14.5, -1, 16);
        ctx.closePath();
        ctx.fill();

        var command = Assert.IsType<FillPathCommand>(Assert.Single(surface.Commands));
        Assert.Equal(new Rect(-1.5, -1, 34, 34), command.Geometry.Bounds);

        // This point lies inside the old two-pixel top edge but outside the
        // exact radius-17 circle centered at (15.5, 16).
        Assert.False(command.Geometry.FillContains(new Point(14.5, -0.99)));
        Assert.True(command.Geometry.FillContains(new Point(15.5, 0)));
    }

    [AvaloniaFact]
    public void ArcTo_RoundedSquareWithLongerStraightEdgesKeepsCanvasGeometry()
    {
        var surface = new CanvasDrawingSurface();
        var ctx = surface.Context;

        ctx.beginPath();
        ctx.moveTo(16, 0);
        ctx.lineTo(20, 0);
        ctx.arcTo(36, 0, 36, 16, 16);
        ctx.lineTo(36, 20);
        ctx.arcTo(36, 36, 20, 36, 16);
        ctx.lineTo(16, 36);
        ctx.arcTo(0, 36, 0, 20, 16);
        ctx.lineTo(0, 16);
        ctx.arcTo(0, 0, 16, 0, 16);
        ctx.closePath();
        ctx.fill();

        var command = Assert.IsType<FillPathCommand>(Assert.Single(surface.Commands));
        Assert.Equal(new Rect(0, 0, 36, 36), command.Geometry.Bounds);
        Assert.True(command.Geometry.FillContains(new Point(17, 0.01)));
    }

    [AvaloniaFact]
    public void CanvasAdorner_DoesNotDrawOverSiblingControls()
    {
        var canvasTarget = new Border
        {
            Width = 80,
            Height = 40,
            Background = Brushes.White
        };
        var sibling = new Border
        {
            Width = 80,
            Height = 40,
            Background = Brushes.Lime
        };
        var root = new StackPanel
        {
            Children =
            {
                canvasTarget,
                sibling
            }
        };
        var window = new Window
        {
            Width = 100,
            Height = 100,
            Content = new VisualLayerManager { Child = root }
        };

        window.Show();

        var ctx = Assert.IsType<CanvasRenderingContext2D>(CanvasContextBridge.GetContext(canvasTarget, "2d"));
        ctx.fillStyle = "#ff0000";
        ctx.fillRect(0, 0, 80, 120);

        Assert.Single(window.GetVisualDescendants().OfType<CanvasDrawingSurface>());

        var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
        using (frame)
        {
            var belowTarget = ReadPixel(frame!, 10, 50);

            Assert.True(belowTarget.G > 180, $"Expected sibling to remain green, but got {belowTarget}.");
            Assert.True(belowTarget.R < 80, $"Expected canvas not to draw red over sibling, but got {belowTarget}.");
        }
    }

    [AvaloniaFact]
    public void WebGlSurface_UsesSkiaFallbackForRegularControlsByDefault()
    {
        var canvasTarget = new Border
        {
            Width = 80,
            Height = 40,
            Background = Brushes.White
        };
        var window = new Window
        {
            Width = 100,
            Height = 80,
            Content = new VisualLayerManager { Child = canvasTarget }
        };

        window.Show();

        var context = Assert.IsType<CanvasWebGlRenderingContext>(CanvasContextBridge.GetContext(canvasTarget, "webgl"));

        Assert.Null(canvasTarget.Child);
        Assert.Equal("not rendered", context.RenderBackend);
    }

    [AvaloniaFact]
    public void WebGlTexSubImage2D_UsesImageSourceOverloadArguments()
    {
        var context = new CanvasWebGlRenderingContext(new CanvasDrawingSurface());
        var texture = context.createTexture();
        var pixels = new byte[16];

        context.pixelStorei(context.UNPACK_ALIGNMENT, 1);
        context.pixelStorei(context.UNPACK_FLIP_Y_WEBGL, true);
        context.bindTexture(context.TEXTURE_2D, texture);
        context.texSubImage2D(context.TEXTURE_2D, 0, 0, 0, context.RGBA, context.UNSIGNED_BYTE, pixels);

        Assert.Equal(context.RGBA, texture.Format);
        Assert.Equal(context.UNSIGNED_BYTE, texture.Type);
        Assert.Same(pixels, texture.Pixels);
        Assert.Equal(1, texture.UnpackAlignment);
        Assert.True(texture.UnpackFlipY);
        Assert.True(texture.NativeDirty);
    }

    [AvaloniaFact]
    public void WebGlSurface_UsesDedicatedOpenGlTarget()
    {
        var canvasTarget = new CanvasOpenGlDrawingSurface
        {
            Width = 80,
            Height = 40
        };
        var window = new Window
        {
            Width = 100,
            Height = 80,
            Content = new VisualLayerManager { Child = canvasTarget }
        };

        window.Show();

        var context = Assert.IsType<CanvasWebGlRenderingContext>(CanvasContextBridge.GetContext(canvasTarget, "webgl"));

        Assert.Same(context, canvasTarget.Context);
        Assert.NotNull(canvasTarget.GetVisualParent());
        Assert.Empty(window.GetVisualDescendants().OfType<CanvasDrawingSurface>());
    }

    [AvaloniaFact]
    public void NativeWebGlSurface_QueuesDrawsWhileOpenGlInitializes()
    {
        var canvasTarget = new CanvasOpenGlDrawingSurface
        {
            Width = 80,
            Height = 40
        };
        var window = new Window
        {
            Width = 100,
            Height = 80,
            Content = new VisualLayerManager { Child = canvasTarget }
        };

        window.Show();

        var context = Assert.IsType<CanvasWebGlRenderingContext>(CanvasContextBridge.GetContext(canvasTarget, "webgl"));

        context.drawArrays(context.TRIANGLES, 0, 3);

        Assert.Equal(1, context.DrawCallCount);
        Assert.Equal(1, context.TriangleCount);
        Assert.Equal("Queued drawArrays mode 4 with 3 vertices", context.LastDrawStatus);
        Assert.Empty(window.GetVisualDescendants().OfType<CanvasDrawingSurface>());
    }

    private static Color ReadPixel(Bitmap bitmap, int x, int y)
    {
        var buffer = new byte[4];
        var handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(new PixelRect(x, y, 1, 1), handle.AddrOfPinnedObject(), buffer.Length, 4);
        }
        finally
        {
            handle.Free();
        }

        var format = bitmap.Format ?? PixelFormat.Bgra8888;
        if (format == PixelFormat.Bgra8888 || format == PixelFormat.Rgb32)
        {
            return Color.FromArgb(buffer[3], buffer[2], buffer[1], buffer[0]);
        }

        if (format == PixelFormat.Rgba8888)
        {
            return Color.FromArgb(buffer[3], buffer[0], buffer[1], buffer[2]);
        }

        throw new NotSupportedException($"Unsupported screenshot pixel format: {format}.");
    }

    private static void AssertPixelWithin(WebScene.Core.WebSceneColor expected, Color actual, int tolerance)
    {
        var within = Math.Abs(expected.A - actual.A) <= tolerance
                     && Math.Abs(expected.R - actual.R) <= tolerance
                     && Math.Abs(expected.G - actual.G) <= tolerance
                     && Math.Abs(expected.B - actual.B) <= tolerance;
        Assert.True(within, $"Expected {expected} within {tolerance} channel value(s), got {actual}.");
    }

    private static int CountRedPixels(Bitmap bitmap)
    {
        var stride = bitmap.PixelSize.Width * 4;
        var pixels = new byte[stride * bitmap.PixelSize.Height];
        var handle = GCHandle.Alloc(pixels, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(
                new PixelRect(bitmap.PixelSize),
                handle.AddrOfPinnedObject(),
                pixels.Length,
                stride);
        }
        finally
        {
            handle.Free();
        }

        var format = bitmap.Format ?? PixelFormat.Bgra8888;
        var redOffset = format == PixelFormat.Rgba8888 ? 0 : 2;
        var greenOffset = 1;
        var blueOffset = format == PixelFormat.Rgba8888 ? 2 : 0;
        var count = 0;
        for (var offset = 0; offset < pixels.Length; offset += 4)
        {
            var red = pixels[offset + redOffset];
            var green = pixels[offset + greenOffset];
            var blue = pixels[offset + blueOffset];
            if (red > 96 && red > green + 32 && red > blue + 32)
            {
                count++;
            }
        }

        return count;
    }
}
