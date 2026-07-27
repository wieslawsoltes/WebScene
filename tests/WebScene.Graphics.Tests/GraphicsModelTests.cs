using System.IO;
using WebScene.Core;
using WebScene.Graphics;
using Xunit;

namespace WebScene.Graphics.Tests;

public sealed class GraphicsModelTests
{
    [Fact]
    public void CanvasPacketReaderPreservesPacketAndStringIdentity()
    {
        double[] values = [40, 1, 0, 22, 4, 1, 2, 3, 4];
        string[] strings = ["#123456"];
        var reader = new CanvasPacketReader(values, strings);

        Assert.True(reader.MoveNext());
        Assert.Equal(CanvasCommandOpcode.FillStyle, reader.CurrentOpcode);
        Assert.Equal("#123456", reader.ReadString(reader.CurrentArguments[0]));
        Assert.True(reader.MoveNext());
        Assert.Equal(CanvasCommandOpcode.FillRect, reader.CurrentOpcode);
        Assert.Equal(new double[] { 1, 2, 3, 4 }, reader.CurrentArguments.ToArray());
        Assert.False(reader.MoveNext());
    }

    [Fact]
    public void CanvasPacketReaderRejectsMalformedPayloadBeforeBackendReplay()
    {
        Assert.Throws<InvalidDataException>(
            () => ReadOnePacket(new double[] { 22, 3, 1, 2, 3 }));
    }

    [Theory]
    [InlineData(double.NaN, 0)]
    [InlineData(-1, 0)]
    [InlineData(1.5, 0)]
    [InlineData(1, double.PositiveInfinity)]
    [InlineData(1, -1)]
    public void CanvasPacketReaderRejectsInvalidHeaders(double opcode, double count)
        => Assert.Throws<InvalidDataException>(() => ReadOnePacket(new[] { opcode, count }));

    [Fact]
    public void CanvasPacketReaderValidatesVariablePayloadsStringsAndUnknownOpcodes()
    {
        Assert.Throws<InvalidDataException>(() => ReadOnePacket(new double[] { 999, 0 }));
        Assert.Throws<InvalidDataException>(() => ReadOnePacket(new double[] { 19, 0 }));
        Assert.Throws<InvalidDataException>(() => ReadOnePacket(new double[] { 19, 2, 2, 4 }));
        Assert.Throws<InvalidDataException>(() => ReadString(new double[] { 40, 1, 1 }, new[] { "only" }));

        var reader = new CanvasPacketReader(new double[] { 19, 3, 2, 4, 8 }, Array.Empty<string>());
        Assert.True(reader.MoveNext());
        Assert.Equal(new double[] { 2, 4, 8 }, reader.CurrentArguments.ToArray());
    }

    [Fact]
    public void CanvasPacketSchemaCoversEveryFixedArgumentFamily()
    {
        ReadOnePacket(new double[] { 1, 0 });
        ReadOnePacket(new double[] { 8, 1, 0 });
        ReadOnePacket(new double[] { 6, 2, 0, 0 });
        ReadOnePacket(new double[] { 25, 3, 0, 0, 0 });
        ReadOnePacket(new double[] { 17, 4, 0, 0, 0, 0 });
        ReadOnePacket(new double[] { 16, 5, 0, 0, 0, 0, 0 });
        ReadOnePacket(new double[] { 4, 6, 0, 0, 0, 0, 0, 0 });
    }

    [Fact]
    public void CanvasImageDataRequiresExactRgbaLength()
    {
        Assert.Throws<ArgumentException>(() => new CanvasImageDataModel(2, 2, new byte[15]));
        var image = new CanvasImageDataModel(2, 2, new byte[16]);
        Assert.Equal((2, 2, 16), (image.Width, image.Height, image.RgbaPixels.Length));
    }

    [Fact]
    public void SvgSceneRetainsBackendNeutralData()
    {
        var root = new SvgSceneNode(1, SvgSceneNodeKind.Group);
        root.Add(new SvgSceneNode(2, SvgSceneNodeKind.Circle)
        {
            Bounds = new WebSceneRect(5, 6, 20, 20),
            Fill = new SvgPaint(WebSceneColor.FromRgb(10, 20, 30))
        });
        var scene = new SvgScene(new WebSceneRect(0, 0, 100, 80), root, Revision: 7);

        Assert.Equal(7, scene.Revision);
        Assert.Equal(SvgSceneNodeKind.Circle, scene.Root.Children[0].Kind);
    }

    [Fact]
    public void CanvasReferenceRendererProducesDeterministicPixels()
    {
        double[] packet =
        [
            40, 1, 0,
            22, 4, 0, 0, 4, 4,
            46, 1, 0.5,
            40, 1, 1,
            22, 4, 2, 0, 2, 4,
            24, 4, 0, 0, 1, 1
        ];
        var renderer = new CanvasReferenceRenderer();
        renderer.Replay(packet, new[] { "#ff0000", "#0000ff" }, new WebSceneSize(4, 4));

        Assert.Equal(WebSceneColor.Transparent, renderer.Surface.GetPixel(0, 0));
        Assert.Equal(WebSceneColor.FromRgb(255, 0, 0), renderer.Surface.GetPixel(1, 1));
        Assert.Equal(new WebSceneColor(255, 127, 0, 128), renderer.Surface.GetPixel(3, 1));
    }

    [Fact]
    public void SvgReferenceRendererProducesSelectedCircleAndRectangleFixture()
    {
        var root = new SvgSceneNode(1, SvgSceneNodeKind.Group);
        root.Add(new SvgSceneNode(2, SvgSceneNodeKind.Rectangle)
        {
            Bounds = new WebSceneRect(0, 0, 6, 6),
            Fill = new SvgPaint(WebSceneColor.FromRgb(0, 255, 0))
        });
        root.Add(new SvgSceneNode(3, SvgSceneNodeKind.Circle)
        {
            Bounds = new WebSceneRect(1, 1, 4, 4),
            Fill = new SvgPaint(WebSceneColor.FromRgb(255, 0, 0))
        });
        var renderer = new SvgReferenceRenderer();
        renderer.Render(new SvgScene(new WebSceneRect(0, 0, 6, 6), root, 1), new WebSceneSize(6, 6));

        Assert.Equal(WebSceneColor.FromRgb(0, 255, 0), renderer.Surface.GetPixel(0, 0));
        Assert.Equal(WebSceneColor.FromRgb(255, 0, 0), renderer.Surface.GetPixel(3, 3));
    }

    [Fact]
    public void ReferenceSurfaceClipsBlendsAndValidatesCoordinates()
    {
        var surface = new ReferencePixelSurface(2, 2);
        surface.Fill(new WebSceneRect(-2, -2, 3, 3), WebSceneColor.FromRgb(255, 0, 0));
        surface.Fill(new WebSceneRect(0, 0, 1, 1), WebSceneColor.FromRgb(0, 0, 255), .5);
        surface.Fill(new WebSceneRect(1, 1, 1, 1), WebSceneColor.Transparent);
        surface.FillCircle(new WebSceneRect(0, 0, 0, 1), WebSceneColor.FromRgb(0, 255, 0));

        Assert.Equal(new WebSceneColor(255, 127, 0, 128), surface.GetPixel(0, 0));
        Assert.Equal(WebSceneColor.Transparent, surface.GetPixel(1, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => surface.GetPixel(2, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new ReferencePixelSurface(-1, 0));
    }

    [Fact]
    public void ReferenceCanvasValidatesFixtureProtocolAndRestoresState()
    {
        var renderer = new CanvasReferenceRenderer();
        Assert.Throws<InvalidOperationException>(() => _ = renderer.Surface);
        Assert.Throws<ArgumentOutOfRangeException>(() => renderer.Replay([], [], new WebSceneSize(1.5, 1)));
        Assert.Throws<NotSupportedException>(() => renderer.Replay(new double[] { 40, 1, 0 }, new[] { "red" }, new WebSceneSize(1, 1)));
        Assert.Throws<FormatException>(() => renderer.Replay(new double[] { 40, 1, 0 }, new[] { "#zz0000" }, new WebSceneSize(1, 1)));
        Assert.Throws<NotSupportedException>(() => renderer.Replay(new double[] { 9, 0 }, [], new WebSceneSize(1, 1)));

        renderer.Replay(
            new double[] { 40, 1, 0, 1, 0, 40, 1, 1, 2, 0, 22, 4, 0, 0, 1, 1 },
            new[] { "#ff0000", "#00ff0080" },
            new WebSceneSize(1, 1));
        Assert.Equal(WebSceneColor.FromRgb(255, 0, 0), renderer.Surface.GetPixel(0, 0));
    }

    [Fact]
    public void PortableModelsSnapshotAndRejectInvalidIdentityOrImageData()
    {
        var path = new CanvasPathModel();
        path.Add(new CanvasPathCommand(CanvasPathCommandKind.MoveTo, 1, 2));
        var snapshot = path.Snapshot();
        path.Add(new CanvasPathCommand(CanvasPathCommandKind.LineTo, 3, 4));
        Assert.Single(snapshot.Commands);
        Assert.Equal(2, path.Commands.Count);

        Assert.Throws<ArgumentOutOfRangeException>(() => new CanvasImageDataModel(-1, 0, []));
        Assert.Throws<ArgumentNullException>(() => new CanvasImageDataModel(0, 0, null!));
        Assert.Throws<ArgumentOutOfRangeException>(() => new SvgSceneNode(0, SvgSceneNodeKind.Group));
        var node = new SvgSceneNode(1, SvgSceneNodeKind.Group);
        Assert.Throws<ArgumentNullException>(() => node.Add(null!));
    }

    [Fact]
    public void CanvasStateAndDisplayListSnapshotsOwnMutablePathAndDashInputs()
    {
        var dash = new List<double> { 2, 3 };
        var clip = new CanvasPathModel();
        clip.Add(new CanvasPathCommand(CanvasPathCommandKind.Rect, 0, 0, 10, 10));
        var path = new CanvasPathModel();
        path.Add(new CanvasPathCommand(CanvasPathCommandKind.MoveTo, 1, 2));
        var pixels = new byte[] { 1, 2, 3, 4 };
        var state = CanvasStateModel.Default with
        {
            GlobalAlpha = .5,
            LineDash = dash,
            Clips = new[] { new CanvasClipModel(clip, GraphicsTransform.Identity) },
            FillStyle = new CanvasColorPaintModel(WebSceneColor.FromRgb(1, 2, 3))
        };
        var list = new CanvasDisplayListModel();
        list.Add(new CanvasDisplayCommand(
            CanvasDisplayCommandKind.FillPath,
            state,
            Path: path,
            ImageData: new CanvasImageDataModel(1, 1, pixels)));

        dash.Add(9);
        clip.Add(new CanvasPathCommand(CanvasPathCommandKind.ClosePath));
        path.Add(new CanvasPathCommand(CanvasPathCommandKind.LineTo, 3, 4));
        pixels[0] = 99;

        var command = Assert.Single(list.Commands);
        Assert.Equal(new double[] { 2, 3 }, command.State.LineDash);
        Assert.Single(command.State.Clips[0].Path.Commands);
        Assert.Single(command.Path!.Commands);
        Assert.Equal((byte)1, command.ImageData!.RgbaPixels[0]);
        Assert.Equal(.5, command.State.GlobalAlpha);

        var snapshot = list.Snapshot();
        Assert.Single(snapshot.Commands);
        Assert.Throws<ArgumentNullException>(() => list.Add(null!));
    }

    [Fact]
    public void RetainedDisplayListPathPreservesAlreadyImmutableCommandIdentity()
    {
        var state = CanvasStateModel.Default with
        {
            LineDash = new double[] { 2, 3 }
        };
        var command = new CanvasDisplayCommand(
            CanvasDisplayCommandKind.FillRectangle,
            state,
            Rectangle: new WebSceneRect(1, 2, 3, 4));
        var list = new CanvasDisplayListModel();

        list.AddRetained(command);

        Assert.Same(command, Assert.Single(list.Commands));
        Assert.Same(state, list.Commands[0].State);
        Assert.Throws<ArgumentNullException>(() => list.AddRetained(null!));
    }

    [Fact]
    public void CanvasPathSnapshotOwnsSvgDataAndTransformedParts()
    {
        var source = new CanvasPathModel("M 0 0 L 10 0 Z");
        source.Add(new CanvasPathCommand(CanvasPathCommandKind.Rect, 1, 2, 3, 4));
        var path = new CanvasPathModel();
        path.AddPart(source, new GraphicsTransform(2, 0, 0, 2, 5, 6));

        var snapshot = path.Snapshot();
        source.Add(new CanvasPathCommand(CanvasPathCommandKind.ClosePath));

        var part = Assert.Single(snapshot.Parts);
        Assert.Equal("M 0 0 L 10 0 Z", part.Path.SvgPathData);
        Assert.Single(part.Path.Commands);
        Assert.Equal(new GraphicsTransform(2, 0, 0, 2, 5, 6), part.Transform);
        Assert.Throws<ArgumentNullException>(() => path.AddPart(null!, GraphicsTransform.Identity));
    }

    [Fact]
    public void CanvasDefaultStateMatchesWebDefaults()
    {
        var state = CanvasStateModel.Default;
        Assert.Equal(1, state.GlobalAlpha);
        Assert.Equal(1, state.LineWidth);
        Assert.Equal(10, state.MiterLimit);
        Assert.Equal("10px sans-serif", state.Font);
        Assert.True(state.ImageSmoothingEnabled);
        Assert.Equal(CanvasCompositeOperation.SourceOver, state.CompositeOperation);
        Assert.Equal(GraphicsTransform.Identity, state.Transform);
        Assert.Equal(CanvasShadowModel.None, state.Shadow);
    }

    [Fact]
    public void PortableGraphicsRecordsExposeEveryBackendNeutralField()
    {
        var color = WebSceneColor.FromRgb(1, 2, 3);
        var stop = new CanvasGradientStop(.25, color);
        var linear = new CanvasLinearGradientModel(
            new WebScenePoint(1, 2), new WebScenePoint(3, 4), new[] { stop });
        var radial = new CanvasRadialGradientModel(
            new WebScenePoint(5, 6), 7, new WebScenePoint(8, 9), 10, new[] { stop });
        var pathCommand = new CanvasPathCommand(
            CanvasPathCommandKind.Arc, 1, 2, 3, 4, 5, 6, 7, true);
        var shadow = new CanvasShadowModel(color, 4, 5, 6);
        var state = CanvasStateModel.Default with
        {
            LineCap = CanvasLineCap.Round,
            LineJoin = CanvasLineJoin.Bevel,
            LineDashOffset = 2,
            TextAlign = CanvasTextAlign.Center,
            TextBaseline = CanvasTextBaseline.Middle,
            ImageSmoothingQuality = CanvasImageSmoothingQuality.High,
            FillStyle = new CanvasGradientPaintModel(linear),
            Shadow = shadow
        };
        var image = new CanvasImageDataModel(1, 1, [1, 2, 3, 4]);
        var command = new CanvasDisplayCommand(
            CanvasDisplayCommandKind.DrawImage,
            state,
            Rectangle: new WebSceneRect(1, 2, 3, 4),
            Text: "text",
            Origin: new WebScenePoint(5, 6),
            ImageData: image,
            SourceRectangle: new WebSceneRect(7, 8, 9, 10),
            DestinationRectangle: new WebSceneRect(11, 12, 13, 14),
            Resource: WebSceneBackendHandle.Create(image));
        var transform = new GraphicsTransform(1, 2, 3, 4, 5, 6);
        var svg = new SvgSceneNode(9, SvgSceneNodeKind.Path)
        {
            Transform = transform,
            Bounds = new WebSceneRect(1, 2, 3, 4),
            PathData = "M0 0",
            Text = "label",
            ResourceUri = "image.png",
            Resource = WebSceneBackendHandle.Create(image),
            Fill = new SvgPaint(color, .5),
            Stroke = new SvgPaint(color),
            StrokeWidth = 3
        };
        var request = new TextMeasureRequest("text", "sans", 12, 600);
        var decoded = new DecodedImage(1, 1, new byte[] { 1, 2, 3, 4 });
        var surface = new ReferencePixelSurface(1, 1);

        Assert.Equal((CanvasPathCommandKind.Arc, 1, 2, 3, 4, 5, 6, 7, true),
            (pathCommand.Kind, pathCommand.X1, pathCommand.Y1, pathCommand.X2, pathCommand.Y2,
                pathCommand.X3, pathCommand.Y3, pathCommand.Radius, pathCommand.Flag));
        Assert.Equal((.25, color), (stop.Offset, stop.Color));
        Assert.Equal((new WebScenePoint(1, 2), new WebScenePoint(3, 4)), (linear.Start, linear.End));
        Assert.Same(linear.Stops, ((CanvasGradientModel)linear).Stops);
        Assert.Equal((new WebScenePoint(5, 6), 7, new WebScenePoint(8, 9), 10),
            (radial.StartCenter, radial.StartRadius, radial.EndCenter, radial.EndRadius));
        Assert.Same(radial.Stops, ((CanvasGradientModel)radial).Stops);
        Assert.Same(linear, Assert.IsType<CanvasGradientPaintModel>(state.FillStyle).Gradient);
        Assert.Equal((color, 4, 5, 6), (shadow.Color, shadow.Blur, shadow.OffsetX, shadow.OffsetY));
        Assert.Equal((CanvasLineCap.Round, CanvasLineJoin.Bevel, 2, CanvasTextAlign.Center,
                CanvasTextBaseline.Middle, CanvasImageSmoothingQuality.High),
            (state.LineCap, state.LineJoin, state.LineDashOffset, state.TextAlign,
                state.TextBaseline, state.ImageSmoothingQuality));
        Assert.Equal((CanvasDisplayCommandKind.DrawImage, "text", new WebScenePoint(5, 6)),
            (command.Kind, command.Text, command.Origin));
        Assert.Equal((command.Rectangle, image, command.SourceRectangle, command.DestinationRectangle),
            (new WebSceneRect(1, 2, 3, 4), command.ImageData, new WebSceneRect(7, 8, 9, 10),
                new WebSceneRect(11, 12, 13, 14)));
        Assert.True(command.Resource.TryGet<CanvasImageDataModel>(out var retainedImage));
        Assert.Same(image, retainedImage);
        Assert.Equal((1d, 2d, 3d, 4d, 5d, 6d),
            (transform.M11, transform.M12, transform.M21, transform.M22, transform.M31, transform.M32));
        Assert.Equal((9L, SvgSceneNodeKind.Path, transform, "M0 0", "label", "image.png", 3d),
            (svg.Id, svg.Kind, svg.Transform, svg.PathData, svg.Text, svg.ResourceUri, svg.StrokeWidth));
        Assert.True(svg.Resource.TryGet<CanvasImageDataModel>(out var svgImage));
        Assert.Same(image, svgImage);
        Assert.Equal((new WebSceneRect(1, 2, 3, 4), color, .5, color),
            (svg.Bounds, svg.Fill!.Color, svg.Fill.Opacity, svg.Stroke!.Color));
        Assert.Equal(("text", "sans", 12d, 600),
            (request.Text, request.FontFamily, request.FontSize, request.FontWeight));
        Assert.Equal((1, 1, 4), (decoded.Width, decoded.Height, decoded.RgbaPixels.Length));
        Assert.Equal(4, surface.RgbaPixels.Length);
    }

    private static void ReadOnePacket(double[] values)
    {
        var reader = new CanvasPacketReader(values, Array.Empty<string>());
        reader.MoveNext();
    }

    private static void ReadString(double[] values, string[] strings)
    {
        var reader = new CanvasPacketReader(values, strings);
        Assert.True(reader.MoveNext());
        reader.ReadString(reader.CurrentArguments[0]);
    }
}
