using System.Security.Cryptography;
using System.Text.Json;
using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

namespace NativeTradingViewTerminal;

internal static partial class HeadlessProof
{
    private static int CaptureGoToEvidence(NativeWebSceneView view, Window window,
        string output, int width, int height, string nativeLibraryPath)
    {
        var open = view.EvaluateTextAsync("""
            (() => {
              const doc = Array.from(document.querySelectorAll('iframe'))
                .map(f => f.contentDocument).find(d => d?.querySelectorAll('canvas').length >= 8);
              const trigger = doc?.querySelector('[data-name="go-to-date"]');
              if (!trigger) throw new Error('Go To trigger is missing');
              // The compact toolbar hides this trigger behind Date Range.
              // Invoke its real application handler; no layout/style overrides.
              trigger.click();
              return true;
            })()
            """);
        PumpUntil(open, TimeSpan.FromSeconds(10));
        PumpFrames(view, window, TimeSpan.FromSeconds(3));
        var surface = (NativeSceneSurface)view.Content!;
        var snapshots = new List<JsonElement>();
        foreach (var tabIndex in new[] { 0, 1, 0 })
        {
            var click = view.EvaluateTextAsync($$"""
                (() => {
                  const doc = Array.from(document.querySelectorAll('iframe'))
                    .map(f => f.contentDocument).find(d => d?.querySelectorAll('canvas').length >= 8);
                  const dialog = Array.from(doc.querySelectorAll('[role="dialog"]'))
                    .find(d => d.textContent.includes('Go to'));
                  const tab = dialog?.querySelectorAll('[role="tab"]')[{{tabIndex}}];
                  if (!tab) throw new Error('Go To tab is missing');
                  const r = tab.getBoundingClientRect();
                  return {x:r.x+r.width/2,y:r.y+r.height/2};
                })()
                """);
            PumpUntil(click, TimeSpan.FromSeconds(10));
            using var point = JsonDocument.Parse(click.Result);
            var x = point.RootElement.GetProperty("x").GetDouble();
            var y = point.RootElement.GetProperty("y").GetDouble();
            surface.SubmitAvaloniaPointerMove(x, y);
            surface.SubmitPointerButton(2, x, y, 0, pressed: true);
            surface.SubmitPointerButton(3, x, y, 0, pressed: false);
            PumpFrames(view, window, TimeSpan.FromSeconds(1));
            var measurement = view.EvaluateTextAsync("""
                (() => {
                  const doc = Array.from(document.querySelectorAll('iframe'))
                    .map(f => f.contentDocument).find(d => d?.querySelectorAll('canvas').length >= 8);
                  const dialog = Array.from(doc.querySelectorAll('[role="dialog"]'))
                    .find(d => d.textContent.includes('Go to'));
                  const tabs = dialog.querySelector('[role="tablist"]');
                  const describe = n => {
                    const r=n.getBoundingClientRect();
                    return {x:r.x,y:r.y,width:r.width,height:r.height,
                      clientWidth:n.clientWidth,clientHeight:n.clientHeight,
                      scrollWidth:n.scrollWidth,scrollHeight:n.scrollHeight};
                  };
                  const content=dialog.querySelector('[class^="content-"]');
                  const scroller=tabs.parentElement;
                  return {viewport:[doc.defaultView.innerWidth,doc.defaultView.innerHeight],
                    selected:dialog.querySelector('[role="tab"][aria-selected="true"]')?.textContent,
                    dialog:describe(dialog),tabs:describe(tabs),scrollTabs:describe(scroller),
                    content:describe(content)};
                })()
                """);
            PumpUntil(measurement, TimeSpan.FromSeconds(10));
            using var data = JsonDocument.Parse(measurement.Result);
            snapshots.Add(data.RootElement.Clone());
            SaveNativeFrame(surface, Path.Combine(output,
                $"go-to-{snapshots.Count}-{(tabIndex == 0 ? "date" : "range")}.png"), width, height);
        }
        File.WriteAllText(Path.Combine(output, "go-to-evidence.json"), JsonSerializer.Serialize(new
        {
            nativeLibraryPath,
            nativeLibrarySha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(nativeLibraryPath))),
            snapshots
        }, new JsonSerializerOptions { WriteIndented = true }));
        for (var index = 0; index < snapshots.Count; ++index)
        {
            var expected = index == 1 ? "Custom range" : "Date";
            if (snapshots[index].GetProperty("selected").GetString()?.Trim() != expected)
                throw new InvalidOperationException($"Go To native pointer did not select {expected}.");
            var tabs = snapshots[index].GetProperty("scrollTabs");
            var content = snapshots[index].GetProperty("content");
            foreach (var box in new[] { tabs, content })
            {
                if (box.GetProperty("scrollWidth").GetDouble() > box.GetProperty("clientWidth").GetDouble() + 1
                    || (height >= 800 && box.GetProperty("scrollHeight").GetDouble()
                        > box.GetProperty("clientHeight").GetDouble() + 1))
                    throw new InvalidOperationException($"Go To {expected} has unexpected overflow: {box}");
            }
        }
        Console.WriteLine($"Go To Date → Custom range → Date verified at {width}x{height}: {output}");
        return 0;
    }
}
