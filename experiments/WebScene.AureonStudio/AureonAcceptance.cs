using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

internal static class AureonAcceptance
{
    private static async Task Wait(NativeWebSceneView view, string expression, string name)
    {
        var deadline = DateTime.UtcNow.AddSeconds(40);
        while (DateTime.UtcNow < deadline)
        {
            if (await view.EvaluateTextAsync(expression) == "true") return;
            await Task.Delay(100);
        }
        throw new InvalidOperationException("Timed out: " + name);
    }

    public static async Task Run(NativeWebSceneView view, Window window)
    {
        await Wait(view, "aureon.gpuReady&&!aureon.building&&aureon.builtRevision===aureon.revision&&aureon.compiled.triangles.length>0&&!aureon.renderer.busy&&aureon.renderer.lastDuration>0", "initial raster and BVH");
        var count = int.Parse(await view.EvaluateTextAsync("aureon.doc.objects.length"));
        await view.EvaluateTextAsync("aureon.addPrimitive('box');true");
        await Wait(view, $"aureon.doc.objects.length==={count + 1}&&!aureon.building&&aureon.builtRevision===aureon.revision", "box creation");
        await view.EvaluateTextAsync("aureon.run('undo');true");
        await Wait(view, $"aureon.doc.objects.length==={count}&&!aureon.building&&aureon.builtRevision===aureon.revision", "undo");
        await view.EvaluateTextAsync("aureon.run('redo');true");
        await Wait(view, $"aureon.doc.objects.length==={count + 1}&&!aureon.building&&aureon.builtRevision===aureon.revision", "redo");
        await view.EvaluateTextAsync("aureon.run('undo');true");
        await Wait(view, $"aureon.doc.objects.length==={count}&&!aureon.building&&aureon.builtRevision===aureon.revision", "restore scene");
        Console.WriteLine("Aureon original editor: add box, undo, redo and BVH rebuilds passed.");

        foreach (var (width, height) in new[] { (980, 680), (1400, 900), (1280, 820) })
        {
            window.Width = width; window.Height = height;
            await Wait(view, $"innerWidth==={width}&&innerHeight==={height}&&!aureon.renderer.busy&&aureon.renderer.lastDuration>0", "viewport resize");
            await Task.Delay(200);
        }
        Console.WriteLine("Aureon viewport resizing passed; physical presentation cadence is separate.");

        await view.EvaluateTextAsync("globalThis.__originalSamples=aureon.doc.settings.samples;aureon.doc.settings.samples=4;aureon.toggleRender(true);true");
        await Wait(view, "aureon.renderer.samples>=4&&!aureon.renderer.busy", "four progressive compute samples");
        await view.EvaluateTextAsync("""
            globalThis.__hdrCheck=null;
            aureon.renderer.readHDR().then(({data,width,height})=>{
                let finite=true,min=Infinity,max=-Infinity;
                for(let i=0;i<data.length;i++){finite&&=Number.isFinite(data[i]);if(i%4!==3){min=Math.min(min,data[i]);max=Math.max(max,data[i]);}}
                globalThis.__hdrCheck={finite,min,max,width,height};
            }).catch(e=>globalThis.__hdrCheck={error:String(e)});
            true
            """);
        await Wait(view, "!!globalThis.__hdrCheck", "HDR GPU readback");
        Console.WriteLine("Aureon original compute/HDR result: " + await view.EvaluateTextAsync("globalThis.__hdrCheck"));
        if (await view.EvaluateTextAsync("__hdrCheck.finite===true&&__hdrCheck.max>__hdrCheck.min&&__hdrCheck.width>0&&__hdrCheck.height>0") != "true")
            throw new InvalidOperationException("Original path tracer did not produce finite, non-uniform HDR pixels.");
        await view.EvaluateTextAsync("aureon.toggleRender(false);aureon.doc.settings.samples=__originalSamples;true");
        await Wait(view, "aureon.renderer.mode==='raster'&&!aureon.renderer.busy&&aureon.renderer.lastDuration>0", "return to modeling");
        if (await view.EvaluateTextAsync("aureon.renderer.errors.length===0&&!aureon.renderer.lost") != "true")
            throw new InvalidOperationException("GPU validation errors or device loss occurred.");
    }
}
