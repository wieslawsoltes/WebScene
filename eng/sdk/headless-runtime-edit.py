#!/usr/bin/env python3
# One-time engineering edit; resulting source changes are committed, never applied by an SDK consumer.
from pathlib import Path


def edit(name, old, new, expected=1):
    path=Path(name);text=path.read_text()
    if old not in text:
        if new in text:return
        raise RuntimeError('Source seam changed: '+name+' / '+old[:100])
    if expected is not None and text.count(old)!=expected:
        raise RuntimeError('Unexpected match count: '+name+' / '+str(text.count(old)))
    path.write_text(text.replace(old,new))

root='experiments/WebScene.NativeEngine.Probe/'
native=root+'native/'
graphics=native+'graphics/'
edit('src/WebScene.Sdk/CMakeLists.txt',
 'if(CMAKE_SYSTEM_NAME STREQUAL "Linux")\n  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebSceneLinuxSDK.cmake")',
 'option(WEBSCENE_SDK_RUNTIME "Include the optional JavaScript Runtime component on Linux" OFF)\nif(CMAKE_SYSTEM_NAME STREQUAL "Linux")\n  if(WEBSCENE_SDK_RUNTIME)\n    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebSceneLinuxRuntimeSDK.cmake")\n  else()\n    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebSceneLinuxSDK.cmake")\n  endif()')
edit('src/WebScene.Sdk/cmake/WebSceneLinuxConfig.cmake',
 'include(CMakeFindDependencyMacro)',
 'if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/WebSceneRuntimeProfile.cmake")\n  include("${CMAKE_CURRENT_LIST_DIR}/WebSceneLinuxRuntimeConfig.cmake")\n  return()\nendif()\ninclude(CMakeFindDependencyMacro)')
p=Path('src/WebScene.Sdk/cmake/WebSceneToolchain.cmake');text=p.read_text()
prefix='if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/WebSceneRuntimeProfile.cmake")\n  include("${CMAKE_CURRENT_LIST_DIR}/WebSceneLinuxRuntimeToolchain.cmake")\n  return()\nendif()\n'
if not text.startswith(prefix):p.write_text(prefix+text)
# Only the graphics dependency block changes; Windows ANGLE is retained.
p=Path(root+'CMakeLists.txt');text=p.read_text()
marker='option(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS "Link pinned native GPU dependencies for this platform" OFF)'
if 'option(WEBSCENE_NATIVE_ENGINE_HEADLESS ' not in text:
    text=text.replace(marker,'option(WEBSCENE_NATIVE_ENGINE_HEADLESS "Offscreen Linux Runtime canvas transport" OFF)\nif(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")\n    message(FATAL_ERROR "Runtime headless transport currently targets Linux")\n  endif()\n  target_compile_definitions(webscene_native_engine PRIVATE WEBSCENE_NATIVE_ENGINE_HEADLESS=1)\nendif()\n'+marker)
    start=text.index(marker);end=text.index('option(WEBSCENE_NATIVE_ENGINE_ENABLE_V8 ',start)
    block=text[start:end].replace('if(APPLE)\n        add_compile_definitions(WEBSCENE_GRAPHICS_ENABLE_ANGLE=0)', 'if(APPLE OR WEBSCENE_NATIVE_ENGINE_HEADLESS)\n        add_compile_definitions(WEBSCENE_GRAPHICS_ENABLE_ANGLE=0)')
    block=block.replace('if(NOT APPLE)', 'if(NOT APPLE AND NOT WEBSCENE_NATIVE_ENGINE_HEADLESS)')
    text=text[:start]+block+text[end:];p.write_text(text)
# These conditions guard platform-neutral GPUCanvasContext lifecycle code.
for p in [Path(native+'webscene_v8_runtime.cpp'), *Path(native).glob('webscene_v8_runtime*.inc')]:
    text=p.read_text().replace('(defined(__APPLE__) || defined(_WIN32))', '(defined(__APPLE__) || defined(_WIN32) || defined(WEBSCENE_NATIVE_ENGINE_HEADLESS))')
    text=text.replace('#if defined(__APPLE__) || defined(_WIN32)\n', '#if defined(__APPLE__) || defined(_WIN32) || defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n')
    p.write_text(text)
edit(native+'webscene_v8_runtime.cpp',
 '#if defined(_WIN32)\n            wgpu::BackendType::D3D12,',
 '#if defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n            wgpu::BackendType::Vulkan,\n#elif defined(_WIN32)\n            wgpu::BackendType::D3D12,')
edit(native+'webscene_native_engine.h',
 'WEBSCENE_WEBGPU_DXGI = 2 };',
 'WEBSCENE_WEBGPU_DXGI = 2, WEBSCENE_WEBGPU_OFFSCREEN = 3 };')
edit(native+'webscene_native_engine_worker.inc',
 '#if defined(_WIN32)\n            return decision==WEBSCENE_WEBGPU_DXGI',
 '#if defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n            return decision==WEBSCENE_WEBGPU_OFFSCREEN ? webscene::graphics::webgpu_canvas_interop::offscreen : webscene::graphics::webgpu_canvas_interop::none;\n#elif defined(_WIN32)\n            return decision==WEBSCENE_WEBGPU_DXGI')
edit(graphics+'webgpu_canvas_interop.h', 'enum class webgpu_canvas_interop { none,iosurface,dxgi };',
 'enum class webgpu_canvas_interop { none,iosurface,dxgi,offscreen };')
edit(graphics+'webgpu_prepared_device_descriptor.h',
 '        error=webgpu_device_request_error::none; return result;',
 '        if(interop==webgpu_canvas_interop::offscreen) {\n            const auto feature=wgpu::FeatureName::ImplicitDeviceSynchronization;\n            if(!adapter.HasFeature(feature)) return {};\n            features.push_back(feature);\n        }\n        error=webgpu_device_request_error::none; return result;')
edit(graphics+'dawn_event_service.h',
 '    void check_thread() const {',
 '    static wgpu::Instance create_instance() {\n#if defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n        constexpr auto feature=wgpu::InstanceFeatureName::TimedWaitAny;\n        wgpu::InstanceDescriptor descriptor{};\n        descriptor.requiredFeatureCount=1;descriptor.requiredFeatures=&feature;\n        return wgpu::CreateInstance(&descriptor);\n#else\n        return wgpu::CreateInstance();\n#endif\n    }\n    void check_thread() const {')
edit(graphics+'dawn_event_service.h', 'instance_(wgpu::CreateInstance())', 'instance_(create_instance())')
edit(graphics+'v8_webgpu_discovery.h', '#include <list>', '#include <list>\n#include <cstdlib>\n#include <string_view>')
edit(graphics+'v8_webgpu_discovery.h',
 '                auto handle=service_.adopt_adapter(std::move(adapter));',
 '                // Native host policy remains separate from JavaScript adapter descriptors.\n#if defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n                wgpu::AdapterInfo info{};\n                if(adapter.GetInfo(&info)!=wgpu::Status::Success) return v8::Null(isolate_);\n                const auto* software=std::getenv("WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER");\n                const bool force=software && std::string_view(software)=="1";\n                const bool hardware=info.adapterType==wgpu::AdapterType::DiscreteGPU || info.adapterType==wgpu::AdapterType::IntegratedGPU;\n                if((force && info.adapterType!=wgpu::AdapterType::CPU) || (!force && !hardware)) return v8::Null(isolate_);\n#endif\n                auto handle=service_.adopt_adapter(std::move(adapter));')
# Preserve configured texture usage/view formats when using the shared pool.
edit(graphics+'dawn_canvas_images.h',
 'struct slot { wgpu::Texture texture; image_metadata metadata{}; uint64_t bytes{}; };',
 'struct slot { wgpu::Texture texture; image_metadata metadata{}; uint64_t bytes{}; wgpu::TextureUsage usage{}; std::vector<wgpu::TextureFormat> view_formats; };')
edit(graphics+'dawn_canvas_images.h',
 '    std::optional<frame> acquire(image_metadata metadata) {',
 '    std::optional<frame> acquire(image_metadata metadata,const wgpu::TextureDescriptor* requested=nullptr) {')
edit(graphics+'dawn_canvas_images.h',
 '        const uint64_t pixels=uint64_t(metadata.width)*metadata.height;',
 '        const auto usage=requested ? requested->usage | wgpu::TextureUsage::CopySrc :\n            wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;\n        std::vector<wgpu::TextureFormat> view_formats;\n        if(requested && requested->viewFormatCount) {\n            if(!requested->viewFormats || requested->viewFormatCount>16) throw std::invalid_argument("Invalid canvas view formats");\n            view_formats.assign(requested->viewFormats,requested->viewFormats+requested->viewFormatCount);\n        }\n        const uint64_t pixels=uint64_t(metadata.width)*metadata.height;')
edit(graphics+'dawn_canvas_images.h',
 '            && slot.metadata.height==metadata.height && slot.metadata.format==metadata.format;',
 '            && slot.metadata.height==metadata.height && slot.metadata.format==metadata.format\n            && slot.usage==usage && slot.view_formats==view_formats;')
edit(graphics+'dawn_canvas_images.h',
 '            descriptor.usage=wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding\n                | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;',
 '            descriptor.usage=usage;\n            descriptor.viewFormatCount=view_formats.size();descriptor.viewFormats=view_formats.data();')
edit(graphics+'dawn_canvas_images.h',
 '        slot.metadata=metadata;',
 '        slot.metadata=metadata;slot.usage=usage;slot.view_formats=std::move(view_formats);')
edit(graphics+'dawn_canvas_images.h',
 '    size_t busy_images() const { return pool_.busy_images(); }',
 '    size_t busy_images() const { return pool_.busy_images(); }\n    image_lease_pool::occupancy inspect_occupancy() const {return pool_.inspect_occupancy();}')
edit(native+'webscene_v8_runtime_canvas.inc',
 '                auto provider=std::make_shared<platform_dawn_canvas_host>(64ULL*1024*1024,self->webgpu_wake);',
 '                auto provider=std::make_shared<platform_dawn_canvas_host>(64ULL*1024*1024,self->webgpu_wake);\n#if defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)\n                provider->bind_instance(self->graphics->dawn().instance());\n#endif')
edit(native+'webscene_v8_runtime_canvas.inc',
 'while(auto image=canvas.provider->take_ready()) {\n            const auto metadata=image->describe();',
 'while(auto image=webscene::graphics::take_platform_ready_gpu_image(canvas.provider)) {\n            const auto metadata=image->value.describe();')
edit(native+'webscene_v8_runtime_canvas.inc',
 'document.publish_gpu_canvas_image(*canvas.node,std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image)));',
 'document.publish_gpu_canvas_image(*canvas.node,std::move(image));')
print('Runtime source integration applied')
