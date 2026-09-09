using System.IO.Compression;
using System.Security.Cryptography;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using WebScene.Backends.Avalonia.Native;

// Exercises the ordinary document/view/composition path, without a native fixture.
internal sealed class WebGpuDocumentProbeApp : Application
{
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var path = Path.Combine(Path.GetTempPath(), $"webscene-webgpu-{Guid.NewGuid():N}.html");
            File.WriteAllText(path, """
                <!doctype html><html><body style="margin:0;background:white">
                <canvas id="gpu" width="400" height="240"></canvas>
                <script>
                (async()=>{
                  const adapter=await navigator.gpu.requestAdapter();
                  if(!adapter)throw new Error('No GPU adapter');
                  const device=await adapter.requestDevice();
                  const context=document.getElementById('gpu').getContext('webgpu');
                  context.configure({device,format:navigator.gpu.getPreferredCanvasFormat()});
                  const module=device.createShaderModule({code:`
                    struct VertexOutput {
                      @builtin(position) position:vec4f,
                      @location(0) color:vec3f
                    }
                    @vertex fn vs(@builtin(vertex_index) index:u32)->VertexOutput {
                      let positions=array<vec2f,3>(vec2f(0,0.8),vec2f(-0.8,-0.8),vec2f(0.8,-0.8));
                      let colors=array<vec3f,3>(vec3f(1,0.2,0.1),vec3f(0.1,1,0.3),vec3f(0.2,0.3,1));
                      var output:VertexOutput;
                      output.position=vec4f(positions[index],0,1);
                      output.color=colors[index];
                      return output;
                    }
                    @fragment fn fs(input:VertexOutput)->@location(0) vec4f {
                      return vec4f(input.color,1);
                    }
                  `});
                  const pipeline=device.createRenderPipeline({
                    layout:'auto',vertex:{module,entryPoint:'vs'},
                    fragment:{module,entryPoint:'fs',targets:[{format:navigator.gpu.getPreferredCanvasFormat()}]}
                  });
                  globalThis.webGpuDemoFrames=0;
                  globalThis.webGpuDemoFrameTimes=[];
                  const draw=()=>{
                    try {
                      webGpuDemoFrameTimes.push(performance.now());
                      const encoder=device.createCommandEncoder();
                      const pass=encoder.beginRenderPass({colorAttachments:[{
                        view:context.getCurrentTexture().createView(),
                        clearValue:{r:webGpuDemoFrames%2 ? 0.15 : 0.03,g:0.04,b:0.07,a:1},
                        loadOp:'clear',storeOp:'store'
                      }]});
                      pass.setPipeline(pipeline);
                      pass.draw(3);
                      pass.end();
                      device.queue.submit([encoder.finish()]);
                      globalThis.webGpuDemoSubmitted=true;
                      ++globalThis.webGpuDemoFrames;
                      if(webGpuDemoFrames<__FRAME_LIMIT__)requestAnimationFrame(draw);
                    } catch(e) { globalThis.webGpuDemoError=String(e);console.error(e); }
                  };
                  const resize=()=>{
                    const canvas=document.getElementById('gpu');
                    canvas.width=Math.max(1,Math.floor(innerWidth));
                    canvas.height=Math.max(1,Math.floor(innerHeight));
                    requestAnimationFrame(draw);
                  };
                  addEventListener('resize',resize);
                  resize();
                })().catch(e=>{globalThis.webGpuDemoError=String(e);console.error(e)});
                </script></body></html>
                """.Replace("__FRAME_LIMIT__", Environment.GetCommandLineArgs().Contains("--stress-webgpu") ? "120" : "1"));
            var arguments = Environment.GetCommandLineArgs();
            var kestrelIndex = Array.IndexOf(arguments, "--kestrel");
            var kestrel = kestrelIndex >= 0;
            if (kestrel)
            {
                if (kestrelIndex + 1 >= arguments.Length) throw new ArgumentException("--kestrel requires the original archive path");
                using var archive = ZipFile.OpenRead(arguments[kestrelIndex + 1]);
                var entry = archive.GetEntry("Kestrel-CAD/Kestrel-CAD.html")
                    ?? throw new InvalidDataException("Original standalone Kestrel document is missing");
                using (var input = entry.Open())
                using (var output = File.Create(path)) input.CopyTo(output);
                Console.WriteLine("Kestrel original document SHA256: " + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant());
            }
            var uri = new Uri(path).AbsoluteUri;
            var view = new NativeWebSceneView(true, url => url == uri || url == path);
            if (arguments.Contains("--verify-webgpu")) view.EnablePerformanceMonitoring();
            desktop.MainWindow = new Window
            {
                Width = ReadDocumentDimension(arguments, "--document-width", kestrel ? 1280 : 400),
                Height = ReadDocumentDimension(arguments, "--document-height", kestrel ? 800 : 240),
                Title = kestrel ? arguments.Contains("--webgpu-vsync")
                    ? Environment.GetEnvironmentVariable("WEBSCENE_SINGLE_SCENE_PER_FRAME") == "1"
                        ? "Kestrel in WebScene — steady frame pacing"
                        : Environment.GetEnvironmentVariable("WEBSCENE_INCREMENTAL_CANVAS_GPU") == "1"
                        ? "Kestrel in WebScene — bounded canvas history"
                        : "Kestrel in WebScene — vsync + input pacing"
                    : "Kestrel in WebScene" : "WebScene WebGPU document", Content = view
            };
            desktop.MainWindow.Opened += async (_, _) =>
            {
                try
                {
                    await view.LoadAsync(uri, Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
                        ?? throw new InvalidOperationException("Set WEBSCENE_TEST_NATIVE_LIBRARY"));
                    Console.WriteLine("WebGPU document loaded through NativeWebSceneView.");
                    if (kestrel)
                    {
                        await Task.Delay(3000);
                        Console.WriteLine(await view.EvaluateTextAsync("({ready:document.documentElement.dataset.ready,backend:document.getElementById('engine-label')?.textContent,history:document.getElementById('command-history')?.textContent,errors:document.querySelectorAll('#command-history .history-error').length,gpu:!!navigator.gpu})"));
                        if (arguments.Contains("--inspect-kestrel-styles"))
                        {
                            Console.WriteLine("Kestrel style diagnostics: " + await view.EvaluateTextAsync("""
                                (()=>({theme:document.documentElement.getAttribute('data-theme'),nodes:
                                  Array.from(document.querySelectorAll('#explorer-list,#explorer-list *,#viewport,#scene,#layers-tab,#objects-tab')).slice(0,40).map(n=>{
                                    const s=getComputedStyle(n),r=n.getBoundingClientRect();
                                    return {id:n.id,tag:n.tagName,classes:n.className,background:s.backgroundColor,color:s.color,display:s.display,rect:[r.x,r.y,r.width,r.height]};
                                  })}))()
                                """));
                        }
                        if (arguments.Contains("--mesh-kestrel"))
                        {
                            var baseline = int.Parse(await view.EvaluateTextAsync("Number(document.getElementById('object-count').textContent)"));
                            await view.EvaluateTextAsync("(()=>{const c=document.getElementById('command-input');c.value='';c.focus();})()");
                            var surface = (NativeSceneSurface)view.Content!;
                            if (surface.SubmitText("BOX") == 0 || surface.SubmitKey(7, 13) == 0 || surface.SubmitKey(8, 13) == 0)
                                throw new InvalidOperationException("Kestrel box command was not accepted.");
                            await Task.Delay(750);
                            if (await view.EvaluateTextAsync("document.getElementById('modal').open===true") != "true")
                            {
                                Console.WriteLine("Kestrel modal failure: " + await view.EvaluateTextAsync("({open:document.getElementById('modal').open,history:document.getElementById('command-history').textContent})"));
                                throw new InvalidOperationException("Kestrel's original primitive dialog did not open.");
                            }
                            await view.EvaluateTextAsync("(()=>{const f=document.getElementById('modal-form');for(const [name,value] of Object.entries({x:0,y:0,z:0,width:2000,depth:1500,height:2500}))f.querySelector('[name='+name+']').value=String(value);f.dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));})()");
                            await Task.Delay(1000);
                            Console.WriteLine("Kestrel mesh: " + await view.EvaluateTextAsync("({objects:Number(document.getElementById('object-count').textContent),backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length,modalError:document.getElementById('modal-error').textContent,history:document.getElementById('command-history').textContent})"));
                            var count = int.Parse(await view.EvaluateTextAsync("Number(document.getElementById('object-count').textContent)"));
                            if (count != baseline + 1)
                                throw new InvalidOperationException($"Kestrel box creation expected {baseline + 1} objects, got {count}.");
                        }
                        if (arguments.Contains("--edit-kestrel"))
                        {
                            var baseline = int.Parse(await view.EvaluateTextAsync("Number(document.getElementById('object-count').textContent)"));
                            foreach (var step in new[] { ("LINE 0,0 1000,1000 ENTER", 1), ("UNDO", 0), ("REDO", 1), ("UNDO", 0) })
                            {
                                await view.EvaluateTextAsync("(()=>{const c=document.getElementById('command-input');c.value='';c.focus();})()");
                                var surface = (NativeSceneSurface)view.Content!;
                                if (surface.SubmitText(step.Item1) == 0 || surface.SubmitKey(7, 13) == 0 || surface.SubmitKey(8, 13) == 0)
                                    throw new InvalidOperationException("Kestrel native command input was not accepted.");
                                await Task.Delay(750);
                                Console.WriteLine("Kestrel edit: " + await view.EvaluateTextAsync("({objects:Number(document.getElementById('object-count').textContent),backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length,history:document.getElementById('command-history').textContent})"));
                                var count = int.Parse(await view.EvaluateTextAsync("Number(document.getElementById('object-count').textContent)"));
                                if (count != baseline + step.Item2)
                                    throw new InvalidOperationException($"Kestrel command {step.Item1} expected {baseline + step.Item2} objects, got {count}.");
                            }
                        }
                        if (arguments.Contains("--exercise-kestrel"))
                        {
                            foreach (var step in new[] { ("iso", "shaded-edges"), ("front", "shaded"), ("iso", "xray"), ("top", "wireframe"), ("iso", "shaded-edges") })
                            {
                                await view.EvaluateTextAsync($"(()=>{{const v=document.getElementById('view-select'),s=document.getElementById('style-select');v.value='{step.Item1}';v.dispatchEvent(new Event('change',{{bubbles:true}}));s.value='{step.Item2}';s.dispatchEvent(new Event('change',{{bubbles:true}}));}})()");
                                await Task.Delay(750);
                                Console.WriteLine("Kestrel view: " + await view.EvaluateTextAsync("({view:document.getElementById('view-select').value,style:document.getElementById('style-select').value,backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length,history:document.getElementById('command-history').textContent})"));
                            }
                        }
                        if (arguments.Contains("--zoom-kestrel"))
                        {
                            // Observe the unchanged application's own resize behavior. Drive wheel
                            // through the host input queue, not synthetic JS events or camera calls.
                            await view.EvaluateTextAsync("""
                                (()=>{
                                  const viewport=document.getElementById('viewport'),canvas=document.getElementById('scene');
                                  const probe=globalThis.kestrelZoomProbe={resizes:[],bitmapMutations:[],wheelEvents:0,handledWheelEvents:0};
                                  probe.wheel=e=>{++probe.wheelEvents;if(e.defaultPrevented)++probe.handledWheelEvents;};
                                  document.addEventListener('wheel',probe.wheel);
                                  probe.resize=new ResizeObserver(entries=>{for(const e of entries)probe.resizes.push([e.contentRect.width,e.contentRect.height]);});
                                  probe.mutations=new MutationObserver(entries=>{for(const e of entries)probe.bitmapMutations.push([e.attributeName,canvas.width,canvas.height]);});
                                  probe.resize.observe(viewport);
                                  probe.mutations.observe(canvas,{attributes:true,attributeFilter:['width','height']});
                                })()
                                """);
                            try
                            {
                                await Task.Delay(250); // Let the required initial resize notification settle.
                                using var center = System.Text.Json.JsonDocument.Parse(await view.EvaluateTextAsync("(()=>{const r=document.getElementById('viewport').getBoundingClientRect();return [r.x+r.width/2,r.y+r.height/2]})()"));
                                var x = center.RootElement[0].GetDouble();
                                var y = center.RootElement[1].GetDouble();
                                var surface = (NativeSceneSurface)view.Content!;
                                var performanceBaseline = view.CapturePerformanceSnapshot();
                                surface.SubmitPointerMove(x, y);
                                for (var step = 0; step < 40; ++step)
                                {
                                    if (surface.SubmitWheel(x, y, step < 20 ? -25 : 25) == 0)
                                        throw new InvalidOperationException("Kestrel zoom input was rejected.");
                                    await Task.Delay(30);
                                }
                                await Task.Delay(500);
                                var performanceAfter = view.CapturePerformanceSnapshot();
                                Console.WriteLine("Kestrel zoom performance: " + System.Text.Json.JsonSerializer.Serialize(new { baseline = performanceBaseline, after = performanceAfter, delta = performanceAfter.Since(performanceBaseline) }, new System.Text.Json.JsonSerializerOptions { IncludeFields = true }));
                                Console.WriteLine("Kestrel zoom diagnostics: " + await view.EvaluateTextAsync("(()=>{const p=globalThis.kestrelZoomProbe,c=document.getElementById('scene');return {wheelEvents:p.wheelEvents,handledWheelEvents:p.handledWheelEvents,resizes:p.resizes,bitmapMutations:p.bitmapMutations,canvas:[c.width,c.height],backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length}})()"));
                            }
                            finally
                            {
                                await view.EvaluateTextAsync("(()=>{const p=globalThis.kestrelZoomProbe;p.resize.disconnect();p.mutations.disconnect();document.removeEventListener('wheel',p.wheel);delete globalThis.kestrelZoomProbe;})()");
                            }
                        }
                        if (arguments.Contains("--pan-kestrel"))
                        {
                            if (arguments.Contains("--trace-kestrel-methods"))
                                await view.EvaluateTextAsync("""
                                    (()=>{
                                      const p=globalThis.kestrelMethodProbe={samples:{},restore:[]};
                                      const wrap=(owner,name)=>{const original=owner[name];if(typeof original!=='function')return;
                                        p.restore.push(()=>owner[name]=original);
                                        owner[name]=function(...args){const start=performance.now();try{return original.apply(this,args);}
                                          finally{(p.samples[name]??=[]).push(performance.now()-start);}};};
                                      for(const name of ['pointerMove','eventPoint','snapPoint','ensureIndex','invalidate'])wrap(Kestrel.App.prototype,name);
                                      wrap(Element.prototype,'closest');wrap(Element.prototype,'getBoundingClientRect');
                                    })()
                                    """);
                            await view.EvaluateTextAsync("""
                                (()=>{
                                  const p=globalThis.kestrelPanProbe={events:[],captures:[],frames:[]};
                                  p.originalRaf=window.requestAnimationFrame;
                                  p.raf=callback=>p.originalRaf.call(window,function(timestamp){
                                    const start=performance.now();
                                    try{return callback.call(this,timestamp);}
                                    finally{p.frames.push({timestamp,start,duration:performance.now()-start});}
                                  });
                                  window.requestAnimationFrame=p.raf;
                                  p.resize=new ResizeObserver(es=>p.widths.push(es[0].contentRect.width));p.resize.observe(document.getElementById(globalThis.propertiesProbe?'properties':'explorer'));p.event=e=>p.events.push({type:e.type,x:e.clientX,y:e.clientY,button:e.button,buttons:e.buttons,time:performance.now(),panning:document.getElementById('viewport').classList.contains('panning')});
                                  p.capture=e=>p.captures.push(e.type);
                                  for(const name of ['pointerdown','pointermove','pointerup'])document.addEventListener(name,p.event);
                                  for(const name of ['gotpointercapture','lostpointercapture'])document.addEventListener(name,p.capture);
                                })()
                                """);
                            var surface = (NativeSceneSurface)view.Content!;
                            double x = 0, y = 0;
                            var pressed = false;
                            try
                            {
                                await Task.Delay(250);
                                using var center = System.Text.Json.JsonDocument.Parse(await view.EvaluateTextAsync("(()=>{const r=document.getElementById('viewport').getBoundingClientRect();return [r.x+r.width/2,r.y+r.height/2]})()"));
                                x = center.RootElement[0].GetDouble();
                                y = center.RootElement[1].GetDouble();
                                await view.EvaluateTextAsync("globalThis.kestrelPanProbe.events=[];globalThis.kestrelPanProbe.frames=[]");
                                var baseline = arguments.Contains("--pan-no-telemetry") ? null : view.CapturePerformanceSnapshot();
                                var traceStarted = System.Diagnostics.Stopwatch.GetTimestamp();
                                var started = System.Diagnostics.Stopwatch.StartNew();
                                var panCycles = ReadDocumentDimension(arguments, "--pan-cycles", arguments.Contains("--pan-long") ? 6 : 1);
                                var submittedMoves = new List<object>(80 * panCycles);
                                var panInputHz = arguments.Contains("--pan-input-120hz") ? 120 : 60;
                                var circularPan = arguments.Contains("--pan-circular");
                                using var inputPacer = arguments.Contains("--pan-high-resolution-input")
                                    ? new WindowsInputPacer() : null;
                                if (surface.SubmitPointerButton(2, x, y, 2, true) == 0)
                                    throw new InvalidOperationException("Kestrel pan press was rejected.");
                                pressed = true;
                                    for (var step = 1; step <= 80 * panCycles; ++step)
                                    {
                                        var offset = KestrelDragWorkloadValidator.PanOffset(step, circularPan);
                                        // Kind 1 routes a move with the right-button bit through the native queue.
                                        var submittedAt = System.Diagnostics.Stopwatch.GetTimestamp();
                                        var sequence = surface.SubmitPointerButton(1, x + offset.X, y + offset.Y, 2, true);
                                        if (sequence == 0)
                                            throw new InvalidOperationException("Kestrel pan move was rejected.");
                                        submittedMoves.Add(new { sequence, submittedAt, step, x = x + offset.X, y = y + offset.Y });
                                        // Include submission work in the 60Hz input budget,
                                        // as the continuous-resize workload already does.
                                        var deadline = traceStarted + step * System.Diagnostics.Stopwatch.Frequency / panInputHz;
                                        var remaining = deadline - System.Diagnostics.Stopwatch.GetTimestamp();
                                        if (inputPacer is not null)
                                            await inputPacer.WaitUntilAsync(deadline);
                                        else if (remaining > 0)
                                            await Task.Delay(TimeSpan.FromSeconds((double)remaining / System.Diagnostics.Stopwatch.Frequency));
                                        else
                                            await Task.Yield();
                                    }
                                    if (surface.SubmitPointerButton(3, x, y, 2, false) == 0)
                                        throw new InvalidOperationException("Kestrel pan release was rejected.");
                                    pressed = false;
                                    await Task.Delay(500);
                                    if (baseline is not null)
                                    {
                                        var after = view.CapturePerformanceSnapshot();
                                        if (arguments.Contains("--verify-checkpoint-fence")
                                            && after.RendererMemory.CanvasCheckpointDeferredReadbacks < 2)
                                            throw new InvalidOperationException("Deferred checkpoint readback was not exercised.");
                                        if (arguments.Contains("--verify-checkpoint-transfer")
                                            && after.RendererMemory.CanvasCheckpointWorkerReadbacks < 2)
                                            throw new InvalidOperationException("Worker checkpoint transfer was not exercised.");
                                        if (arguments.Contains("--verify-canvas-history"))
                                        {
                                            var memory = after.RendererMemory;
                                            if (memory.CanvasCheckpointSubmissions < 2
                                                || memory.MaximumRetainedCanvasCommands >= 2 * NativeCanvasSceneRenderer.CanvasCheckpointInterval)
                                                throw new InvalidOperationException("Canvas checkpoint history did not stay bounded.");
                                            Console.WriteLine($"Canvas history bounded: checkpoints={memory.CanvasCheckpointSubmissions}, peakCommands={memory.MaximumRetainedCanvasCommands}, retainedCommands={memory.RetainedCommandCount}.");
                                        }
                                        Console.WriteLine("Kestrel pan performance: " + System.Text.Json.JsonSerializer.Serialize(new { elapsedMilliseconds = started.Elapsed.TotalMilliseconds, baseline, after, delta = after.Since(baseline) }, new System.Text.Json.JsonSerializerOptions { IncludeFields = true }));
                                    }
                                    if (arguments.Contains("--trace-kestrel-methods"))
                                        Console.WriteLine("Kestrel method samples: " + await view.EvaluateTextAsync("globalThis.kestrelMethodProbe.samples"));
                                    var panDiagnostics = await view.EvaluateTextAsync("(()=>{const p=globalThis.kestrelPanProbe;return {events:p.events,captures:p.captures,frames:p.frames,panning:document.getElementById('viewport').classList.contains('panning'),backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length}})()");
                                    Console.WriteLine("Kestrel pan diagnostics: " + panDiagnostics);
                                    Console.WriteLine("Kestrel pan composition timeline: " + System.Text.Json.JsonSerializer.Serialize(new
                                    {
                                        timestampFrequency = System.Diagnostics.Stopwatch.Frequency,
                                        panInputHz,
                                        panPath = circularPan ? "circle" : "out-and-back",
                                        highResolutionInput = inputPacer is not null,
                                        panCycles,
                                        traceStarted,
                                        submittedMoves,
                                        publications = surface.PublishedScenes.Where(sample => sample.Timestamp >= traceStarted),
                                        renderedScenes = surface.RenderedScenes.Where(sample => sample.Timestamp >= traceStarted),
                                        scheduling = surface.SchedulingSamples.Where(sample => sample.Timestamp >= traceStarted),
                                        // Recorded at the end of OnRender, before platform presentation.
                                        drawCallbackCompletions = surface.PresentationTimestamps.Where(timestamp => timestamp >= traceStarted),
                                        physicalPresentationVerified = false
                                    }));
                                    KestrelDragWorkloadValidator.Validate(panDiagnostics, x, y, panCycles: panCycles, circular: circularPan);
                                    Console.WriteLine("Kestrel pan workload validated (physical presentation remains unqualified).");
                            }
                            finally
                            {
                                if (pressed) surface.SubmitPointerButton(3, x, y, 2, false);
                                await view.EvaluateTextAsync("(()=>{const p=globalThis.kestrelPanProbe;if(window.requestAnimationFrame===p.raf)window.requestAnimationFrame=p.originalRaf;for(const n of ['pointerdown','pointermove','pointerup'])document.removeEventListener(n,p.event);for(const n of ['gotpointercapture','lostpointercapture'])document.removeEventListener(n,p.capture);delete globalThis.kestrelPanProbe;})()");
                                if (arguments.Contains("--trace-kestrel-methods"))
                                    await view.EvaluateTextAsync("(()=>{for(const restore of kestrelMethodProbe.restore)restore();delete globalThis.kestrelMethodProbe;})()");
                            }
                        }
                        if (arguments.Contains("--sidebar-kestrel") || arguments.Contains("--properties-kestrel"))
                        {
                            var surface = (NativeSceneSurface)view.Content!;
                            var properties = arguments.Contains("--properties-kestrel");
                            var repeated = properties || arguments.Contains("--sidebar-cycles");
                            await view.EvaluateTextAsync(properties ? "globalThis.propertiesProbe=true" : "globalThis.propertiesProbe=false");
                            using var setup = System.Text.Json.JsonDocument.Parse(await view.EvaluateTextAsync("(()=>{const r=document.querySelector('.'+(globalThis.propertiesProbe?'right':'left')+'-resizer').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,width:document.getElementById(globalThis.propertiesProbe?'properties':'explorer').offsetWidth,viewport:[innerWidth,innerHeight],dpr:devicePixelRatio,canvas:[document.getElementById('scene').width,document.getElementById('scene').height]}})()"));
                            var x = setup.RootElement.GetProperty("x").GetDouble();
                            var y = setup.RootElement.GetProperty("y").GetDouble();
                            var originalWidth = setup.RootElement.GetProperty("width").GetDouble();
                            if (originalWidth <= 0) throw new InvalidOperationException("The requested sidebar is hidden at the current viewport width; discard this resize workload.");
                            await view.EvaluateTextAsync("(()=>{const p=globalThis.kestrelSidebarProbe={events:[],widths:[]};p.resize=new ResizeObserver(es=>p.widths.push(es[0].contentRect.width));p.resize.observe(document.getElementById(globalThis.propertiesProbe?'properties':'explorer'));p.event=e=>p.events.push({type:e.type,x:e.clientX,y:e.clientY,button:e.button,buttons:e.buttons});for(const n of ['pointerdown','pointermove','pointerup'])document.addEventListener(n,p.event);})()");
                            var submittedMoves = new List<object>(60);
                            var baseline = view.CapturePerformanceSnapshot();
                            var traceStarted = System.Diagnostics.Stopwatch.GetTimestamp();
                            using var pacer = OperatingSystem.IsWindows() ? new WindowsInputPacer() : null;
                            var pressed = false;
                            try
                            {
                                if (surface.SubmitPointerButton(2, x, y, 0, true) == 0)
                                    throw new InvalidOperationException("Sidebar press rejected.");
                                pressed = true;
                                for (var step = 1; step <= (repeated ? 600 : 60); ++step)
                                {
                                    var offset = repeated ? (properties ? -120.0 : 120.0) * (1 - Math.Abs((step % 120) - 60) / 60.0) : step * 2.0;
                                    var submittedAt = System.Diagnostics.Stopwatch.GetTimestamp();
                                    var sequence = surface.SubmitPointerButton(1, x + offset, y, 0, true);
                                    if (sequence == 0)
                                        throw new InvalidOperationException("Sidebar move rejected.");
                                    submittedMoves.Add(new { sequence, submittedAt, step, x = x + offset, y });
                                    var deadline = traceStarted + step * System.Diagnostics.Stopwatch.Frequency / 60;
                                    if (pacer is not null) await pacer.WaitUntilAsync(deadline);
                                    else { var remaining = deadline - System.Diagnostics.Stopwatch.GetTimestamp(); if (remaining > 0) await Task.Delay(TimeSpan.FromSeconds((double)remaining / System.Diagnostics.Stopwatch.Frequency)); }
                                }
                                if (surface.SubmitPointerButton(3, x + (repeated ? 0 : 120), y, 0, false) == 0)
                                    throw new InvalidOperationException("Sidebar release rejected.");
                                pressed = false;
                                await Task.Delay(500);
                                var width = double.Parse(await view.EvaluateTextAsync("document.getElementById(globalThis.propertiesProbe?'properties':'explorer').offsetWidth"), System.Globalization.CultureInfo.InvariantCulture);
                                var after = view.CapturePerformanceSnapshot();
                                var diagnostics = await view.EvaluateTextAsync("(()=>{return {events:globalThis.kestrelSidebarProbe.events,widths:globalThis.kestrelSidebarProbe.widths,panning:document.getElementById('viewport').classList.contains('panning'),errors:document.querySelectorAll('#command-history .history-error').length}})()");
                                Console.WriteLine("Kestrel sidebar diagnostics: " + diagnostics);
                                Console.WriteLine("Kestrel sidebar timeline: " + System.Text.Json.JsonSerializer.Serialize(new {
                                    traceStarted, timestampFrequency = System.Diagnostics.Stopwatch.Frequency,
                                    properties, originalWidth, width, initialGeometry = setup.RootElement, baseline, after, delta = after.Since(baseline), submittedMoves,
                                    publications = surface.PublishedScenes.Where(sample => sample.Timestamp >= traceStarted),
                                    renderedScenes = surface.RenderedScenes.Where(sample => sample.Timestamp >= traceStarted),
                                    scheduling = surface.SchedulingSamples.Where(sample => sample.Timestamp >= traceStarted),
                                    physicalPresentationVerified = false
                                }, new System.Text.Json.JsonSerializerOptions { IncludeFields = true }));
                                // Preserve failure diagnostics before rejecting an interrupted gesture.
                                if (Math.Abs(width - (repeated ? originalWidth : Math.Clamp(originalWidth + 120, 170, 390))) > 1)
                                    throw new InvalidOperationException($"Sidebar drag failed: width {originalWidth} became {width}.");
                                if (!repeated) KestrelDragWorkloadValidator.Validate(diagnostics, x, y, sidebar: true);
                                else { using var result = System.Text.Json.JsonDocument.Parse(diagnostics); if (result.RootElement.GetProperty("errors").GetInt32() != 0 || result.RootElement.GetProperty("panning").GetBoolean()) throw new InvalidOperationException("Properties workload reported an application error or panning."); if (result.RootElement.GetProperty("widths").EnumerateArray().Max(e => e.GetDouble()) - result.RootElement.GetProperty("widths").EnumerateArray().Min(e => e.GetDouble()) < 50) throw new InvalidOperationException("Properties divider did not resize."); }
                                Console.WriteLine("Kestrel sidebar workload validated (physical presentation remains unqualified).");
                            }
                            finally
                            {
                                if (pressed) surface.SubmitPointerButton(3, x + (repeated ? 0 : 120), y, 0, false);
                                await view.EvaluateTextAsync("(()=>{const p=globalThis.kestrelSidebarProbe;p.resize.disconnect();for(const n of ['pointerdown','pointermove','pointerup'])document.removeEventListener(n,p.event);delete globalThis.kestrelSidebarProbe;})()");
                            }
                        }
                        if (arguments.Contains("--continuous-resize-kestrel"))
                        {
                            var surface = (NativeSceneSurface)view.Content!;
                            var baseline = view.CapturePerformanceSnapshot();
                            var initialWidth = desktop.MainWindow.Width;
                            var initialHeight = desktop.MainWindow.Height;
                            var traceStarted = System.Diagnostics.Stopwatch.GetTimestamp();
                            var submittedSizes = new List<object>(80);
                            var nativeWindowResizes = new List<object>();
                            var surfaceSizeChanges = new List<object>();
                            EventHandler<WindowResizedEventArgs> onNativeResize = (_, e) => {
                                if (nativeWindowResizes.Count < 4096) nativeWindowResizes.Add(new {
                                    timestamp = System.Diagnostics.Stopwatch.GetTimestamp(),
                                    width = e.ClientSize.Width, height = e.ClientSize.Height, reason = e.Reason.ToString() });
                            };
                            EventHandler<SizeChangedEventArgs> onSurfaceResize = (_, e) => {
                                if (surfaceSizeChanges.Count < 4096) surfaceSizeChanges.Add(new {
                                    timestamp = System.Diagnostics.Stopwatch.GetTimestamp(),
                                    width = e.NewSize.Width, height = e.NewSize.Height });
                            };
                            desktop.MainWindow.Resized += onNativeResize;
                            surface.SizeChanged += onSurfaceResize;
                            long inputEnded;
                            try
                            {
                            for (var step = 1; step <= 80; ++step)
                            {
                                var offset = step <= 40 ? step : 80 - step;
                                var width = initialWidth + offset * 3;
                                var height = initialHeight + offset * 2;
                                var requestedAt = System.Diagnostics.Stopwatch.GetTimestamp();
                                desktop.MainWindow.Width = width;
                                desktop.MainWindow.Height = height;
                                var timestamp = System.Diagnostics.Stopwatch.GetTimestamp();
                                submittedSizes.Add(new { requestedAt, timestamp, width, height });
                                // Include synchronous resize work in the 60Hz budget.
                                // Adding a fresh 16ms sleep after it halves input cadence
                                // when the native setter already takes one display slot.
                                var deadline = traceStarted + step * System.Diagnostics.Stopwatch.Frequency / 60;
                                var remaining = deadline - System.Diagnostics.Stopwatch.GetTimestamp();
                                if (remaining > 0)
                                    await Task.Delay(TimeSpan.FromSeconds((double)remaining / System.Diagnostics.Stopwatch.Frequency));
                                else
                                    await Task.Yield(); // Keep late runs responsive; never busy-wait.

                            }
                            inputEnded = System.Diagnostics.Stopwatch.GetTimestamp();
                            await Task.Delay(750);
                            }
                            finally
                            {
                                desktop.MainWindow.Resized -= onNativeResize;
                                surface.SizeChanged -= onSurfaceResize;
                            }
                            var diagnostics = await view.EvaluateTextAsync("(()=>{const c=document.getElementById('scene'),r=c.getBoundingClientRect();const ancestors=[];for(let n=c.parentElement;n;n=n.parentElement){const b=n.getBoundingClientRect(),s=getComputedStyle(n);ancestors.push({id:n.id,tag:n.tagName,rect:[b.x,b.y,b.width,b.height],height:s.height,minHeight:s.minHeight,display:s.display,flex:s.flex,gridTemplateRows:s.gridTemplateRows});}return {window:[innerWidth,innerHeight],canvas:[c.width,c.height],css:[r.width,r.height],ancestors,dpr:devicePixelRatio,backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length}})()");
                            var published = surface.PublishedScenes.Where(sample => sample.Timestamp >= traceStarted).ToArray();
                            var drawn = surface.RenderedScenes.Where(sample => sample.Timestamp >= traceStarted).ToArray();
                            Console.WriteLine("Kestrel continuous window resize: " + System.Text.Json.JsonSerializer.Serialize(new {
                                traceStarted, inputEnded, timestampFrequency = System.Diagnostics.Stopwatch.Frequency,
                                submittedSizes, nativeWindowResizes, surfaceSizeChanges,
                                nativeSubmissions = surface.SubmittedResizes.Where(sample => sample.Timestamp >= traceStarted),
                                diagnostics, baseline, after = view.CapturePerformanceSnapshot(),
                                publications = published, renderedScenes = drawn,
                                scheduling = surface.SchedulingSamples.Where(sample => sample.Timestamp >= traceStarted),
                                physicalPresentationVerified = false, nativeUserDragVerified = false
                            }, new System.Text.Json.JsonSerializerOptions { IncludeFields = true }));
                            ValidateResizeGeometry(diagnostics);
                            using var geometry = System.Text.Json.JsonDocument.Parse(diagnostics);
                            var finalWindow = geometry.RootElement.GetProperty("window");
                            if (Math.Abs(finalWindow[0].GetDouble() - initialWidth) > 1 ||
                                Math.Abs(finalWindow[1].GetDouble() - initialHeight) > 1)
                                throw new InvalidOperationException("Continuous resize did not restore the initial viewport.");
                            if (published.Where(sample => sample.Timestamp <= inputEnded)
                                .Select(sample => (sample.ViewportWidth, sample.ViewportHeight)).Distinct().Count() < 3)
                                throw new InvalidOperationException("Continuous resize did not publish intermediate viewport sizes.");
                            if (drawn.Count(sample => sample.Timestamp <= inputEnded) < 3)
                                throw new InvalidOperationException("Continuous resize did not draw intermediate scenes.");
                            Console.WriteLine("Kestrel continuous window resize workload validated (physical presentation and native user drag remain unqualified).");
                        }
                        if (arguments.Contains("--resize-kestrel"))
                        {
                            if (arguments.Contains("--capture-resize-kestrel"))
                                await Task.Delay(5000); // Allow a window-scoped recorder to attach.
                            foreach (var size in new[] { (980, 680), (1440, 900), (1100, 740), (1280, 800) })
                            {
                                desktop.MainWindow.Width = size.Item1;
                                desktop.MainWindow.Height = size.Item2;
                                await Task.Delay(750);
                                var resizeDiagnostics = await view.EvaluateTextAsync("(()=>{const c=document.getElementById('scene'),r=c.getBoundingClientRect();const ancestors=[];for(let n=c.parentElement;n;n=n.parentElement){const b=n.getBoundingClientRect(),s=getComputedStyle(n);ancestors.push({id:n.id,tag:n.tagName,rect:[b.x,b.y,b.width,b.height],height:s.height,minHeight:s.minHeight,display:s.display,flex:s.flex,gridTemplateRows:s.gridTemplateRows});}return {window:[innerWidth,innerHeight],canvas:[c.width,c.height],css:[r.width,r.height],ancestors,dpr:devicePixelRatio,backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length}})()");
                                Console.WriteLine("Kestrel resize: " + resizeDiagnostics);
                                ValidateResizeGeometry(resizeDiagnostics);
                            }
                        }
                        if (arguments.Contains("--verify-kestrel"))
                        {
                            var webGpuReady = await view.EvaluateTextAsync("document.documentElement.dataset.ready==='true'&&document.getElementById('engine-label').textContent.startsWith('WebGPU')&&document.querySelectorAll('#command-history .history-error').length===0");
                            Console.WriteLine(webGpuReady == "true" ? "Kestrel WebGPU startup check passed (interaction qualification remains)." : "FAIL: Kestrel WebGPU startup or initial rendering reported an error.");
                            await view.DisposeAsync();
                            desktop.Shutdown(webGpuReady == "true" ? 0 : 1);
                        }
                        return;
                    }
                    if (Environment.GetCommandLineArgs().Contains("--resize-webgpu"))
                    {
                        foreach (var size in new[] { (640, 360), (280, 180), (520, 320), (400, 240) })
                        {
                            desktop.MainWindow.Width = size.Item1;
                            desktop.MainWindow.Height = size.Item2;
                            await Task.Delay(500);
                        }
                    }
                    if (Environment.GetCommandLineArgs().Contains("--stress-webgpu"))
                    {
                        var deadline = DateTime.UtcNow.AddSeconds(10);
                        while (await view.EvaluateTextAsync("webGpuDemoFrames>=120||!!globalThis.webGpuDemoError") != "true"
                            && DateTime.UtcNow < deadline)
                            await Task.Delay(100);
                    }
                    else await Task.Delay(1000);
                    Console.WriteLine(await view.EvaluateTextAsync("({submitted:globalThis.webGpuDemoSubmitted,error:globalThis.webGpuDemoError,gpu:!!navigator.gpu,frames:globalThis.webGpuDemoFrames,width:document.getElementById('gpu').width,height:document.getElementById('gpu').height})"));
                    Console.WriteLine(view.SceneDiagnostics);
                    if (arguments.Contains("--verify-webgpu"))
                    {
                        if (await view.EvaluateTextAsync("webGpuDemoFrames>=120&&!globalThis.webGpuDemoError") != "true")
                            throw new InvalidOperationException("WebGPU stress workload did not complete 120 frames.");
                        Console.WriteLine("WebGPU frame times: " + await view.EvaluateTextAsync("webGpuDemoFrameTimes"));
                        var surface = (NativeSceneSurface)view.Content!;
                        Console.WriteLine("WebGPU draw times: " + System.Text.Json.JsonSerializer.Serialize(new
                        {
                            frequency = System.Diagnostics.Stopwatch.Frequency,
                            timestamps = surface.PresentationTimestamps,
                            physicalPresentationVerified = false
                        }));
                        await view.DisposeAsync();
                        desktop.Shutdown(0);
                    }
                }
                catch (Exception error) { Console.Error.WriteLine(error); desktop.Shutdown(1); }
            };
            desktop.Exit += (_, _) => File.Delete(path);
        }
        base.OnFrameworkInitializationCompleted();
    }

    private static void ValidateResizeGeometry(string diagnostics)
    {
        using var parsed = System.Text.Json.JsonDocument.Parse(diagnostics);
        var root = parsed.RootElement;
        var css = root.GetProperty("css");
        var bitmap = root.GetProperty("canvas");
        var dpr = root.GetProperty("dpr").GetDouble();
        if (root.GetProperty("errors").GetInt32() != 0 || !double.IsFinite(dpr) || dpr <= 0)
            throw new InvalidOperationException("Kestrel resize reported an application error or invalid scale.");
        for (var axis = 0; axis < 2; ++axis)
        {
            var size = css[axis].GetDouble();
            if (!double.IsFinite(size) || size <= 0
                || Math.Abs(bitmap[axis].GetDouble() - size * dpr) > 1)
                throw new InvalidOperationException("Kestrel canvas bitmap does not match its resized CSS dimensions and DPR.");
        }
        var ancestors = root.GetProperty("ancestors").EnumerateArray().ToArray();
        var viewport = ancestors.Single(node => node.GetProperty("id").GetString() == "viewport").GetProperty("rect");
        var workbench = ancestors.Single(node => node.GetProperty("id").GetString() == "workbench").GetProperty("rect");
        if (Math.Abs(viewport[3].GetDouble() - workbench[3].GetDouble()) > 1)
            throw new InvalidOperationException("Kestrel viewport no longer tracks the resized workbench height.");
    }

    private static int ReadDocumentDimension(string[] arguments, string option, int fallback)
    {
        var index = Array.IndexOf(arguments, option);
        if (index < 0) return fallback;
        if (index + 1 >= arguments.Length || !int.TryParse(arguments[index + 1], out var value) || value <= 0)
            throw new ArgumentException($"{option} requires a positive integer.");
        return value;
    }

}
