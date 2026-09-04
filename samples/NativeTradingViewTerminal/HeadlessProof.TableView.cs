using System.Text.Json;
using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

namespace NativeTradingViewTerminal;

internal static partial class HeadlessProof
{
    private static int CaptureTableViewEvidence(NativeWebSceneView view, Window window,
        string output, int width, int height)
    {
        var open = view.EvaluateTextAsync("""
            (() => {
              const f=Array.from(document.querySelectorAll('iframe'))
                .find(f => f.contentDocument?.querySelectorAll('canvas').length >= 8);
              f.contentWindow.chartWidget.executeActionById('openTableView');
              return true;
            })()
            """);
        PumpUntil(open, TimeSpan.FromSeconds(10));
        PumpFrames(view, window, TimeSpan.FromSeconds(2));
        var surface = (NativeSceneSurface)view.Content!;
        var snapshots = new List<JsonElement>();
        for (var step = 0; step < 4; step++)
        {
            if (step > 0)
            {
                surface.SubmitWheel(width / 3, height / 2, 10000);
                PumpFrames(view, window, TimeSpan.FromSeconds(1));
            }
            var state = view.EvaluateTextAsync("""
                (() => {
                  const doc=Array.from(document.querySelectorAll('iframe')).map(f=>f.contentDocument).find(d=>d?.querySelector('table'));
                  const table=doc.querySelector('table'), w=table.parentElement, wr=w.getBoundingClientRect();
                  const rect=n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}};
                  const rows=Array.from(table.querySelectorAll('tbody tr'));
                  const visible=rows.filter(n=>{const r=n.getBoundingClientRect();return n.textContent&&r.bottom>wr.top&&r.top<wr.bottom});
                  const arrow=doc.querySelector('[aria-label="Scroll to the top"]');
                  const date=table.querySelector('thead th'),label=date.querySelector('span');
                  const hit=doc.elementFromPoint(date.getBoundingClientRect().left+20,date.getBoundingClientRect().top+20);
                  return {top:w.scrollTop,extent:w.scrollHeight,viewport:w.clientHeight,table:rect(table),
                    date:rect(date),label:rect(label),dateText:date.textContent,dateHit:date.contains(hit),
                    rows:rows.length,visibleRows:visible.length,last:rows.at(-1)?.textContent,
                    arrow:arrow?rect(arrow):null,html:doc.body.innerHTML};
                })()
                """);
            PumpUntil(state, TimeSpan.FromSeconds(10));
            using var parsed = JsonDocument.Parse(state.Result);
            snapshots.Add(parsed.RootElement.Clone());
            SaveNativeFrame(surface, Path.Combine(output, $"table-{step}.png"), width, height);
        }
        File.WriteAllText(Path.Combine(output,"table-evidence.json"),
            JsonSerializer.Serialize(snapshots, new JsonSerializerOptions {WriteIndented=true}));
        var arrowBox = snapshots[^1].GetProperty("arrow");
        var backToTop = false;
        if (arrowBox.ValueKind == JsonValueKind.Object)
        {
            var x=arrowBox.GetProperty("x").GetSingle()+arrowBox.GetProperty("width").GetSingle()/2;
            var y=arrowBox.GetProperty("y").GetSingle()+arrowBox.GetProperty("height").GetSingle()/2;
            surface.SubmitAvaloniaPointerMove(x,y);
            PumpFrames(view,window,TimeSpan.FromMilliseconds(100));
            surface.SubmitPointerButton(2,x,y,0,pressed:true);
            PumpFrames(view,window,TimeSpan.FromMilliseconds(50));
            surface.SubmitPointerButton(3,x,y,0,pressed:false);
            PumpFrames(view,window,TimeSpan.FromSeconds(2));
            var top=view.EvaluateTextAsync("Array.from(document.querySelectorAll('iframe')).map(f=>f.contentDocument).find(d=>d?.querySelector('table')).querySelector('table').parentElement.scrollTop");
            PumpUntil(top,TimeSpan.FromSeconds(10));
            File.WriteAllText(Path.Combine(output,"back-to-top.json"),top.Result);
            backToTop=double.TryParse(top.Result,System.Globalization.CultureInfo.InvariantCulture,out var offset)&&offset==0;
            SaveNativeFrame(surface,Path.Combine(output,"table-back-to-top.png"),width,height);
        }
        Console.WriteLine($"Table View evidence: {output}");
        return backToTop && snapshots.Skip(1).All(s=>s.GetProperty("visibleRows").GetInt32()>0
            && s.GetProperty("top").GetDouble()>0 && s.GetProperty("dateHit").GetBoolean()) ? 0 : 1;
    }
}
