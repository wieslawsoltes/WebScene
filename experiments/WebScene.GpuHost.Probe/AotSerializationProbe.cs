using System.Text.Json;
using SkiaSharp;
using WebScene.Backends.Avalonia;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;

// Runs in the published NativeAOT executable; no window, browser or GPU required.
internal static class AotSerializationProbe
{
    internal static int Run()
    {
        if (System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported
            || JsonSerializer.IsReflectionEnabledByDefault)
            throw new InvalidOperationException("This check requires NativeAOT with reflection JSON disabled.");
        using var surface = SKSurface.Create(new SKImageInfo(2, 2));
        surface.Canvas.Clear(SKColors.CornflowerBlue);
        using var image = surface.Snapshot();
        using var png = image.Encode(SKEncodedImageFormat.Png, 100);
        var state = NativeCanvasSceneRenderer.CanvasState.Default;
        state.GlobalAlpha = .375;
        state.LineDash = [2, 5];
        var checkpoint = new NativeCanvasSceneRenderer.RasterCheckpoint
        {
            Png = Convert.ToBase64String(png.ToArray()), State = state,
            Matrix = [1, 0, .25f, 0, 1, .5f, 0, 0, 1], Path = [[0, 1, 2]]
        };
        var bytes = JsonSerializer.SerializeToUtf8Bytes(checkpoint, CanvasCheckpointJsonContext.Default.RasterCheckpoint);
        var restored = JsonSerializer.Deserialize(bytes, CanvasCheckpointJsonContext.Default.RasterCheckpoint)!;
        using var decoded = SKBitmap.Decode(Convert.FromBase64String(restored.Png));
        if (restored.State.GlobalAlpha != .375 || !restored.State.LineDash.SequenceEqual(new double[] { 2, 5 })
            || restored.State.FillStyle != state.FillStyle || restored.Matrix[2] != .25f
            || restored.Path[0][2] != 2 || decoded.GetPixel(0, 0) != SKColors.CornflowerBlue)
            throw new InvalidOperationException("AOT canvas checkpoint lost pixels or replay state.");
        var directory = Path.Combine(Path.GetTempPath(), "webscene-aot-archive-" + Guid.NewGuid().ToString("N"));
        try
        {
            var address = new Uri("https://example.test/aot.html");
            var original = new WebSceneTextResource("aot", "<p>AOT ✓</p>", "aot.html", null) { EntityTag = "v1" };
            var archive = AvaloniaResourceArchive.CreateCapture(directory);
            archive.CaptureText(address, WebSceneResourceKind.Markup, default, original);
            archive.Flush();
            var replay = AvaloniaResourceArchive.OpenReplay(directory).ReplayText(address, WebSceneResourceKind.Markup, default);
            if (replay != original) throw new InvalidOperationException("AOT resource archive round trip failed.");
        }
        finally { if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true); }
        Console.WriteLine("NativeAOT checkpoint pixels/state and resource archive round trips passed.");
        return 0;
    }
}
