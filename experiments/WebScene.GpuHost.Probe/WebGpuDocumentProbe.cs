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
                  const draw=()=>{
                    try {
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
            desktop.MainWindow = new Window
            {
                Width = kestrel ? 1280 : 400, Height = kestrel ? 800 : 240, Title = kestrel ? "Kestrel in WebScene" : "WebScene WebGPU document", Content = view
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
                        if (arguments.Contains("--exercise-kestrel"))
                        {
                            foreach (var step in new[] { ("iso", "shaded-edges"), ("front", "shaded"), ("iso", "xray"), ("top", "wireframe"), ("iso", "shaded-edges") })
                            {
                                await view.EvaluateTextAsync($"(()=>{{const v=document.getElementById('view-select'),s=document.getElementById('style-select');v.value='{step.Item1}';v.dispatchEvent(new Event('change',{{bubbles:true}}));s.value='{step.Item2}';s.dispatchEvent(new Event('change',{{bubbles:true}}));}})()");
                                await Task.Delay(750);
                                Console.WriteLine("Kestrel view: " + await view.EvaluateTextAsync("({view:document.getElementById('view-select').value,style:document.getElementById('style-select').value,backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length,history:document.getElementById('command-history').textContent})"));
                            }
                        }
                        if (arguments.Contains("--resize-kestrel"))
                        {
                            foreach (var size in new[] { (980, 680), (1440, 900), (1100, 740), (1280, 800) })
                            {
                                desktop.MainWindow.Width = size.Item1;
                                desktop.MainWindow.Height = size.Item2;
                                await Task.Delay(750);
                                Console.WriteLine("Kestrel resize: " + await view.EvaluateTextAsync("(()=>{const c=document.getElementById('scene'),r=c.getBoundingClientRect();return {window:[innerWidth,innerHeight],canvas:[c.width,c.height],css:[r.width,r.height],dpr:devicePixelRatio,backend:document.getElementById('engine-label').textContent,errors:document.querySelectorAll('#command-history .history-error').length}})()"));
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
                }
                catch (Exception error) { Console.Error.WriteLine(error); desktop.Shutdown(1); }
            };
            desktop.Exit += (_, _) => File.Delete(path);
        }
        base.OnFrameworkInitializationCompleted();
    }
}
