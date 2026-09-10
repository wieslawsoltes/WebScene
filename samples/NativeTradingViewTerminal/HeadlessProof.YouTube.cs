using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

namespace NativeTradingViewTerminal;

internal static partial class HeadlessProof
{
    private static void CaptureYoutubeEvidence(NativeWebSceneView view, Window window,
        string output, int width, int height)
    {
        var verify = view.EvaluateTextAsync("""
            (() => {
              const frames=Array.from(document.querySelectorAll('iframe'));
              return frames.length > 0 && frames.every(f=> {
                const d=f.contentDocument, a=d.getElementById('webscene-watch'), i=d.querySelector('img');
                return a && i && i.complete && i.naturalWidth > 0 && a.href.startsWith('https://www.youtube.com/watch?v=');
              });
            })()
            """);
        PumpUntil(verify, TimeSpan.FromSeconds(10));
        if (verify.Result != "true") throw new InvalidOperationException("Live YouTube thumbnails did not render.");
        var surface = (NativeSceneSurface)view.Content!;
        view.EnablePerformanceMonitoring();
        var baseline = view.CapturePerformanceSnapshot();
        using var process = Process.GetCurrentProcess();
        var cpuBefore = process.TotalProcessorTime;
        var steps = new List<double>();
        var midpointScroll = 0.0;
        for (var step = 0; step < 160; step++)
        {
            var timer = Stopwatch.StartNew();
            // Alternate page margins and iframe contents; reverse before the
            // end so the sample remains usable for visual verification.
            surface.SubmitWheel(step % 2 == 0 ? 20 : width / 2, height / 3,
                step < 80 ? 12 : -12);
            PumpFrames(view, window, TimeSpan.FromMilliseconds(1));
            steps.Add(timer.Elapsed.TotalMilliseconds);
            if (step == 79)
            {
                var position = view.EvaluateTextAsync("document.scrollingElement.scrollTop");
                PumpUntil(position, TimeSpan.FromSeconds(10));
                midpointScroll = double.Parse(position.Result, System.Globalization.CultureInfo.InvariantCulture);
            }
        }
        var state = view.EvaluateTextAsync("document.scrollingElement.scrollTop");
        PumpUntil(state, TimeSpan.FromSeconds(10));
        var after = view.CapturePerformanceSnapshot();
        var delta = after.Since(baseline);
        File.WriteAllText(Path.Combine(output, "youtube-scroll.json"), JsonSerializer.Serialize(new {
            note = "Headless diagnostic loop includes a deliberate 10ms sleep per sample; these are not compositor frame times.",
            cpuMs = (process.TotalProcessorTime - cpuBefore).TotalMilliseconds,
            midpointScroll, finalScrollTop = state.Result, stepMs = steps, delta, baseline, after
        }, new JsonSerializerOptions { IncludeFields = true, WriteIndented = true }));
        if (delta.ResourceRequests != 0 || delta.ResourceMisses != 0)
            throw new InvalidOperationException("Warm scrolling requested additional iframe/image resources.");
        if (midpointScroll < 100)
            throw new InvalidOperationException("Wheel input did not move the outer page past the embed.");
        var reset = view.EvaluateTextAsync("document.scrollingElement.scrollTop=0; true");
        PumpUntil(reset, TimeSpan.FromSeconds(10));
        PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
    }
}
