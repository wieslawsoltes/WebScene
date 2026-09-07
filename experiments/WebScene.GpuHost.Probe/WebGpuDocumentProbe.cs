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
                  const encoder=device.createCommandEncoder();
                  const pass=encoder.beginRenderPass({colorAttachments:[{
                    view:context.getCurrentTexture().createView(),
                    clearValue:{r:0.03,g:0.04,b:0.07,a:1},loadOp:'clear',storeOp:'store'
                  }]});
                  pass.setPipeline(pipeline);
                  pass.draw(3);
                  pass.end();
                  device.queue.submit([encoder.finish()]);
                  globalThis.webGpuDemoSubmitted=true;
                  requestAnimationFrame(()=>{globalThis.webGpuDemoRaf=true;});
                })().catch(e=>{globalThis.webGpuDemoError=String(e);console.error(e)});
                </script></body></html>
                """);
            var uri = new Uri(path).AbsoluteUri;
            var view = new NativeWebSceneView(true, url => url == uri || url == path);
            desktop.MainWindow = new Window
            {
                Width = 400, Height = 240, Title = "WebScene WebGPU document", Content = view
            };
            desktop.MainWindow.Opened += async (_, _) =>
            {
                try
                {
                    await view.LoadAsync(uri, Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
                        ?? throw new InvalidOperationException("Set WEBSCENE_TEST_NATIVE_LIBRARY"));
                    Console.WriteLine("WebGPU document loaded through NativeWebSceneView.");
                    await Task.Delay(1000);
                    Console.WriteLine(await view.EvaluateTextAsync("({submitted:globalThis.webGpuDemoSubmitted,error:globalThis.webGpuDemoError,gpu:!!navigator.gpu,raf:globalThis.webGpuDemoRaf})"));
                    Console.WriteLine(view.SceneDiagnostics);
                }
                catch (Exception error) { Console.Error.WriteLine(error); desktop.Shutdown(1); }
            };
            desktop.Exit += (_, _) => File.Delete(path);
        }
        base.OnFrameworkInitializationCompleted();
    }
}
