using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-independent regression spike for string-bearing Canvas2D commands that
/// cross the numeric batch-buffer boundary.
/// </summary>
public static class V8CanvasStringBoundaryProbe
{
    internal static int Run()
    {
        BenchmarkApp.EnsureInitialized();
        var window = new Window
        {
            Width = 320,
            Height = 240,
            Content = new CssLayoutPanel { ClipToBounds = true }
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        try
        {
            using var host = new AvaloniaBrowserHost(window);
            using var runtime = new ClearScriptV8Runtime(
                host,
                new ClearScriptV8RuntimeOptions
                {
                    EnableCanvasBatching = true,
                    SharedCache = null
                });

            runtime.Execute("""
                const canvas = document.createElement('canvas');
                document.body.appendChild(canvas);
                const context = canvas.getContext('2d');

                // A zero-argument command occupies two numeric values. Leave two
                // values free, then enqueue a three-value string state command.
                for (let index = 0; index < 8191; index++) context.beginPath();
                context.fillStyle = '#123456';
                __webSceneFlushCanvases();

                // Leave four values free, then enqueue a five-value text command.
                for (let index = 0; index < 8190; index++) context.beginPath();
                context.fillText('packet-boundary', 1, 1);
                __webSceneFlushCanvases();
                globalThis.canvasStringBoundaryPassed = true;
                """, "v8-canvas-string-packet-boundary.js");

            var passed = Convert.ToBoolean(runtime.Engine.Script.canvasStringBoundaryPassed);
            Console.WriteLine(
                $"V8 Canvas2D string packet boundary: {(passed ? "pass" : "fail")}; " +
                runtime.DescribeCanvasBatching());
            return passed ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"V8 Canvas2D string packet boundary: fail; " +
                $"{exception.GetType().Name}: {exception.Message}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}
