#include "webscene_native_engine.h"
#include "webscene_gpu_provider.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "native_webgpu_triangle_tests: " << message << '\n';
    std::exit(1);
}

std::string last_error(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_last_error(engine, nullptr, 0U);
    std::string value(required == 0U ? 1U : required, '\0');
    webscene_engine_copy_last_error(engine, value.data(), value.size());
    return value.c_str();
}

std::string read_file(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail(std::string("unable to read Three.js bundle: ") + path);
    std::ostringstream value;
    value << stream.rdbuf();
    return value.str();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4) {
        fail("expected the Dawn provider path, optional Three.js bundle, and optional CTS subset");
    }
    if ((webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_WEBGPU_BINDINGS) == 0U) {
        fail("engine did not advertise its WebGPU binding slice");
    }
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.gpu_provider_path = argv[1];
    options.gpu_provider_path_length = std::strlen(argv[1]);
    options.required_capabilities = WEBSCENE_GPU_CAPABILITY_WEBGPU;
    auto* engine = webscene_engine_create_with_options(&options);
    if (engine == nullptr) fail("engine/provider negotiation failed");

    constexpr char source[] = R"JS(
      (async () => {
        globalThis.__webgpuStage = 'start';
        const canvas = document.createElement('canvas');
        canvas.width = 64;
        canvas.height = 48;
        canvas.style.width = '64px';
        canvas.style.height = '48px';
        document.body.appendChild(canvas);
        if (!navigator.gpu) throw new Error('navigator.gpu missing');
        let adapter = await navigator.gpu.requestAdapter();
        globalThis.__webgpuStage = 'adapter';
        if (!adapter) throw new Error('Metal adapter missing');
        if (!adapter.features.has('core-features-and-limits')) {
          throw new Error('Metal core feature set missing');
        }
        const discardedDevice = await adapter.requestDevice();
        const discardedLoss = discardedDevice.lost;
        discardedDevice.destroy();
        const lossInfo = await discardedLoss;
        if (lossInfo.reason !== 'destroyed') {
          throw new Error('explicit WebGPU device loss was not reported');
        }
        adapter = await navigator.gpu.requestAdapter();
        if (!adapter) throw new Error('Metal adapter did not recover after loss');
        const device = await adapter.requestDevice();
        globalThis.__webgpuStage = 'device';
        if (!device.features.has('core-features-and-limits') ||
            device.limits.maxTextureDimension2D < 64 ||
            GPUTextureUsage.RENDER_ATTACHMENT !== 16 ||
            GPUBufferUsage.VERTEX !== 32) {
          throw new Error('WebGPU globals or device capabilities missing');
        }
        const upload = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
          mappedAtCreation: true
        });
        new Float32Array(upload.getMappedRange()).set([0, 1, 2, 3]);
        upload.unmap();
        device.pushErrorScope('validation');
        const bindGroupLayout = device.createBindGroupLayout({ entries: [{
          binding: 0,
          visibility: GPUShaderStage.VERTEX,
          buffer: { type: 'uniform' }
        }] });
        const pipelineLayout = device.createPipelineLayout({
          bindGroupLayouts: [bindGroupLayout]
        });
        const uniform = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });
        const bindGroup = device.createBindGroup({
          layout: bindGroupLayout,
          entries: [{ binding: 0, resource: { buffer: uniform } }]
        });
        if (!pipelineLayout || !bindGroup || !device.createSampler()) {
          throw new Error('WebGPU resource binding APIs missing');
        }
        const depthTexture = device.createTexture({
          size: { width: 64, height: 48 },
          format: 'depth24plus',
          usage: GPUTextureUsage.RENDER_ATTACHMENT
        });
        if (depthTexture.width !== 64 || !depthTexture.createView()) {
          throw new Error('WebGPU texture API missing');
        }
        depthTexture.destroy();
        if (await device.popErrorScope() !== null) {
          throw new Error('WebGPU resource validation failed');
        }
        const context = canvas.getContext('webgpu');
        const format = navigator.gpu.getPreferredCanvasFormat();
        context.configure({ device, format, usage: 16, alphaMode: 'premultiplied' });
        globalThis.__webgpuStage = 'configured';
        const shader = device.createShaderModule({ code: `
          @vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
            var positions = array<vec2f, 3>(
              vec2f(0.0, 0.7), vec2f(-0.7, -0.7), vec2f(0.7, -0.7));
            return vec4f(positions[index], 0.0, 1.0);
          }
          @fragment fn fs() -> @location(0) vec4f {
            return vec4f(0.1, 0.6, 1.0, 1.0);
          }
        ` });
        const pipeline = device.createRenderPipeline({
          layout: 'auto',
          vertex: { module: shader, entryPoint: 'vs' },
          fragment: { module: shader, entryPoint: 'fs', targets: [{ format }] }
        });
        globalThis.__webgpuStage = 'pipeline';
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({ colorAttachments: [{
          view: context.getCurrentTexture().createView(),
          clearValue: { r: 0.02, g: 0.03, b: 0.05, a: 1 },
          loadOp: 'clear', storeOp: 'store'
        }] });
        pass.setPipeline(pipeline);
        pass.draw(3);
        pass.end();
        device.queue.submit([encoder.finish()]);
        globalThis.__webgpuStage = 'submitted';
        await device.queue.onSubmittedWorkDone();
        globalThis.__webgpuTriangleComplete = true;
      })();
    )JS";
    if (webscene_engine_execute_script(
            engine, source, sizeof(source) - 1U,
            "native-webgpu-triangle.js", 25U) == 0U) {
        fail("triangle script was rejected");
    }
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engine, &metrics);
        if (metrics.executed_scripts >= 1U || metrics.script_errors != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    {
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engine, &metrics);
        if (metrics.script_errors != 0U) {
            fail("triangle script failed at stage before completion check: "
                + last_error(engine));
        }
    }
    constexpr char completion_check[] = R"JS(
      if (!globalThis.__webgpuTriangleComplete) {
        throw new Error('triangle stage: ' + globalThis.__webgpuStage);
      }
    )JS";
    webscene_engine_execute_script(
        engine, completion_check, sizeof(completion_check) - 1U,
        "native-webgpu-triangle-check.js", 31U);
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engine, &metrics);
        if (metrics.executed_scripts + metrics.script_errors >= 2U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    webscene_engine_request_scene_checkpoint(engine);
    bool observed = false;
    for (auto attempt = 0; attempt < 500 && !observed; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->external_texture_count != 0U) {
                const auto& texture = scene->external_textures[0];
                observed = texture.pixel_width == 64U
                    && texture.pixel_height == 48U
                    && texture.shared_handle != 0U
                    && texture.texture_handle != 0U;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    if (!observed) {
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engine, &metrics);
        fail("no zero-copy triangle texture reached a scene: scripts="
            + std::to_string(metrics.executed_scripts) + ", errors="
            + std::to_string(metrics.script_errors) + ", lastError="
            + last_error(engine));
    }

    if (argc >= 3) {
        const auto three_source = read_file(argv[2]);
        webscene_engine_metrics before{};
        webscene_engine_get_metrics(engine, &before);
        if (webscene_engine_execute_script(
                engine, three_source.data(), three_source.size(),
                "three-webgpu-r184.js", 23U) == 0U) {
            fail("Three.js WebGPU bundle was rejected");
        }
        for (auto attempt = 0; attempt < 1500; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.executed_scripts > before.executed_scripts
                || metrics.script_errors > before.script_errors) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            fail("Three.js WebGPU bundle failed: " + last_error(engine));
        }
        constexpr char three_check[] = R"JS(
          if (!globalThis.__webSceneThreeWebGpuComplete) {
            throw new Error('Three.js WebGPU stage: ' +
              globalThis.__webSceneThreeWebGpuStage);
          }
        )JS";
        webscene_engine_execute_script(
            engine, three_check, sizeof(three_check) - 1U,
            "three-webgpu-r184-check.js", 29U);
        for (auto attempt = 0; attempt < 500; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.executed_scripts + metrics.script_errors
                >= after.executed_scripts + after.script_errors + 1U) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        webscene_engine_metrics checked{};
        webscene_engine_get_metrics(engine, &checked);
        if (checked.script_errors > after.script_errors) {
            fail("Three.js WebGPU completion failed: " + last_error(engine));
        }
        webscene_engine_request_scene_checkpoint(engine);
        bool observed_three = false;
        for (auto attempt = 0; attempt < 750 && !observed_three; ++attempt) {
            const auto* scene = webscene_engine_acquire_latest_scene(engine);
            if (scene != nullptr) {
                for (size_t index = 0U; index < scene->external_texture_count;
                    ++index) {
                    const auto& texture = scene->external_textures[index];
                    observed_three = observed_three
                        || (texture.pixel_width == 96U
                            && texture.pixel_height == 72U
                            && texture.shared_handle != 0U
                            && texture.texture_handle != 0U);
                }
                webscene_scene_acknowledge(scene);
                webscene_scene_release(scene);
            }
            if (!observed_three) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        }
        if (!observed_three) fail("Three.js produced no zero-copy 96x72 frame");
    }
    if (argc == 4) {
        const auto cts_source = read_file(argv[3]);
        webscene_engine_metrics before{};
        webscene_engine_get_metrics(engine, &before);
        if (webscene_engine_execute_script(
                engine, cts_source.data(), cts_source.size(),
                "webgpu-cts-operation-rendering-basic.js", 39U) == 0U) {
            fail("curated WebGPU CTS script was rejected");
        }
        for (auto attempt = 0; attempt < 1500; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.executed_scripts > before.executed_scripts
                || metrics.script_errors > before.script_errors) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            fail("curated WebGPU CTS failed: " + last_error(engine));
        }
        constexpr char cts_check[] = R"JS(
          if (!globalThis.__webSceneWebGpuCtsComplete) {
            throw new Error('WebGPU CTS stage: ' +
              globalThis.__webSceneWebGpuCtsStage);
          }
        )JS";
        webscene_engine_execute_script(
            engine, cts_check, sizeof(cts_check) - 1U,
            "webgpu-cts-operation-rendering-basic-check.js", 45U);
        for (auto attempt = 0; attempt < 500; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.executed_scripts + metrics.script_errors
                >= after.executed_scripts + after.script_errors + 1U) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        webscene_engine_metrics checked{};
        webscene_engine_get_metrics(engine, &checked);
        if (checked.script_errors > after.script_errors) {
            fail("curated WebGPU CTS completion failed: " + last_error(engine));
        }
    }
    webscene_engine_destroy(engine);
    return 0;
}
