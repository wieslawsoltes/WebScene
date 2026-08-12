// Adapted from WebGPU CTS api/operation/rendering/basic.spec.ts at the exact
// revision recorded in subset.json. Copyright 2019 WebGPU CTS Contributors;
// distributed under the 3-clause BSD license recorded by that upstream suite.
(async () => {
  globalThis.__webSceneWebGpuCtsStage = "adapter";
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error("CTS: no WebGPU adapter");
  const device = await adapter.requestDevice();

  async function readPixel(texture) {
    const destination = device.createBuffer({
      size: 256,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
    });
    const encoder = device.createCommandEncoder();
    encoder.copyTextureToBuffer(
      { texture, mipLevel: 0, origin: { x: 0, y: 0, z: 0 } },
      { buffer: destination, bytesPerRow: 256 },
      { width: 1, height: 1, depthOrArrayLayers: 1 });
    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
    await destination.mapAsync(GPUMapMode.READ);
    const pixel = [...new Uint8Array(destination.getMappedRange(0, 4))];
    destination.unmap();
    destination.destroy();
    return pixel;
  }

  function expectGreen(name, pixel) {
    if (pixel.length !== 4 || pixel[0] !== 0 || pixel[1] !== 255 ||
        pixel[2] !== 0 || pixel[3] !== 255) {
      throw new Error(`CTS ${name}: expected 0,255,0,255; got ${pixel}`);
    }
  }

  globalThis.__webSceneWebGpuCtsStage = "clear";
  const clearTarget = device.createTexture({
    format: "rgba8unorm",
    size: { width: 1, height: 1, depthOrArrayLayers: 1 },
    usage: GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT
  });
  let encoder = device.createCommandEncoder();
  let pass = encoder.beginRenderPass({ colorAttachments: [{
    view: clearTarget.createView(),
    clearValue: { r: 0, g: 1, b: 0, a: 1 },
    loadOp: "clear",
    storeOp: "store"
  }] });
  pass.end();
  device.queue.submit([encoder.finish()]);
  await device.queue.onSubmittedWorkDone();
  expectGreen("clear", await readPixel(clearTarget));

  globalThis.__webSceneWebGpuCtsStage = "fullscreen_quad";
  const drawTarget = device.createTexture({
    format: "rgba8unorm",
    size: { width: 1, height: 1, depthOrArrayLayers: 1 },
    usage: GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT
  });
  const pipeline = device.createRenderPipeline({
    layout: "auto",
    vertex: { module: device.createShaderModule({ code: `
      @vertex fn main(@builtin(vertex_index) i: u32)
          -> @builtin(position) vec4f {
        var p = array<vec2f, 3>(vec2f(-1, -3), vec2f(3, 1), vec2f(-1, 1));
        return vec4f(p[i], 0, 1);
      }` }) },
    fragment: { module: device.createShaderModule({ code: `
      @fragment fn main() -> @location(0) vec4f {
        return vec4f(0, 1, 0, 1);
      }` }), targets: [{ format: "rgba8unorm" }] },
    primitive: { topology: "triangle-list" }
  });
  encoder = device.createCommandEncoder();
  pass = encoder.beginRenderPass({ colorAttachments: [{
    view: drawTarget.createView(),
    clearValue: { r: 1, g: 0, b: 0, a: 1 },
    loadOp: "clear",
    storeOp: "store"
  }] });
  pass.setPipeline(pipeline);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);
  await device.queue.onSubmittedWorkDone();
  expectGreen("fullscreen_quad", await readPixel(drawTarget));
  globalThis.__webSceneWebGpuCtsComplete = true;
})();
