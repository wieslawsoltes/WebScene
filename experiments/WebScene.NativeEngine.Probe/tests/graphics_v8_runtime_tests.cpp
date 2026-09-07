#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include "graphics/v8_release_registry.h"
#include "graphics/v8_webgpu_adapter_request.h"
#include "graphics/v8_webgpu_buffers.h"
#include "graphics/v8_webgpu_mapped_ranges.h"
#include "graphics/image_lease_abi.h"
#include <v8.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
#include "graphics_v8_webgpu_options.h"
#include "graphics_v8_webgpu_buffer_descriptor.h"
int weak_releases=0;
void test_native_gpu_scene_leases();
void test_image_lease_abi() {
    struct provider final : image_provider_lifetime {};
    auto native=std::make_shared<provider>();
    std::weak_ptr<provider> alive=native;
    auto pool=std::make_unique<owned_image_pool>(native,3); native.reset();
    auto writer=pool->acquire();
    writer->set_metadata({1,2,3,4,5,6,640,480}); writer->begin();
    auto frame=writer->publish();
    auto* original=new webscene_gpu_image_lease_v3(std::move(*frame)); frame.reset();
    webscene_gpu_image_lease_v3* retained=nullptr;
    require(webscene_gpu_image_retain_v3(original,&retained)==WEBSCENE_SCENE_ACQUIRE_SUCCESS,"ABI retain failed");
    webscene_gpu_image_consumer_v3* consumer=nullptr;
    require(webscene_gpu_image_begin_consumer_v3(retained,&consumer)==WEBSCENE_SCENE_ACQUIRE_SUCCESS,"ABI consumer failed");
    webscene_gpu_image_lease_v3* saturated=nullptr;
    require(webscene_gpu_image_retain_v3(retained,&saturated)==WEBSCENE_SCENE_ACQUIRE_BACKPRESSURE && !saturated,"ABI backpressure failed");
    pool.reset(); writer->complete(); writer.reset();
    webscene_gpu_image_release_v3(original);
    webscene_gpu_image_info_v3 info{}; info.struct_size=sizeof(info); info.version=3;
    require(webscene_gpu_image_describe_v3(retained,&info) && info.width==640 && info.allocation_generation==3,"ABI metadata failed");
    info.version=99; require(!webscene_gpu_image_describe_v3(retained,&info),"ABI metadata version accepted");
    webscene_gpu_image_release_v3(retained);
    require(!alive.expired(),"ABI released pending GPU provider");
    std::thread completion([&] { webscene_gpu_image_complete_consumer_v3(consumer); }); completion.join();
    require(alive.expired(),"ABI completion leaked provider");
}
void test_scene_acquisition_v3() {
    std::unique_ptr<webscene_engine,decltype(&webscene_engine_destroy)> engine(webscene_engine_create(64),webscene_engine_destroy);
    require(engine!=nullptr,"scene ABI engine creation failed");
    webscene_scene_acquire_options_v3 options{sizeof(options),WEBSCENE_SCENE_VIEW_VERSION_3,0};
    const webscene_scene_view_v3* view=nullptr;
    auto invalid=options; invalid.scene_version=99;
    require(webscene_engine_acquire_next_scene_v3(engine.get(),&invalid,&view)==WEBSCENE_SCENE_ACQUIRE_UNSUPPORTED_VERSION && !view,"scene version rejection failed");
    invalid=options; invalid.struct_size=0;
    require(webscene_engine_acquire_next_scene_v3(engine.get(),&invalid,&view)==WEBSCENE_SCENE_ACQUIRE_INVALID_ARGUMENT && !view,"scene options size rejection failed");
    require(webscene_engine_acquire_next_scene_v3(nullptr,&options,&view)==WEBSCENE_SCENE_ACQUIRE_INVALID_ARGUMENT,"null scene engine accepted");
    auto status=webscene_engine_acquire_next_scene_v3(engine.get(),&options,&view);
    if (view) { webscene_scene_release_v3(view); view=nullptr; }
    constexpr char source[]="document.body.innerHTML='<div>scene lease ABI</div>';";
    require(webscene_engine_execute_script(engine.get(),source,sizeof(source)-1,"scene-v3",8),"scene ABI script failed");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        status=webscene_engine_acquire_next_scene_v3(engine.get(),&options,&view);
        if (status==WEBSCENE_SCENE_ACQUIRE_EMPTY) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (status==WEBSCENE_SCENE_ACQUIRE_EMPTY && std::chrono::steady_clock::now()<deadline);
    require(status==WEBSCENE_SCENE_ACQUIRE_SUCCESS && view,"versioned scene acquisition failed");
    require(view->struct_size==sizeof(*view) && view->scene_version==3 && view->required_capabilities==0,"invalid versioned scene layout");
    require(view->cpu_view && view->cpu_view->abi_version==2,"CPU scene compatibility view missing");
    const auto* legacy=webscene_engine_acquire_latest_scene(engine.get());
    require(legacy && legacy->abi_version==2,"legacy scene acquisition regressed");
    webscene_scene_release(legacy);
    auto incompatible=*view;
    incompatible.struct_size=sizeof(uint32_t);
    require(!webscene_scene_acknowledge_v3(&incompatible),"short scene view acknowledged");
    webscene_scene_release_v3(&incompatible);
    incompatible=*view; incompatible.scene_version=99;
    require(!webscene_scene_acknowledge_v3(&incompatible),"unknown scene view acknowledged");
    webscene_scene_release_v3(&incompatible);
    const webscene_scene_view_v3* latest=nullptr;
    require(webscene_engine_acquire_latest_scene_v3(engine.get(),&options,&latest)==WEBSCENE_SCENE_ACQUIRE_SUCCESS && latest,
        "latest versioned acquisition failed");
    webscene_scene_release_v3(latest);
    require(webscene_scene_acknowledge_v3(view),"versioned acknowledgement failed");
    const auto revision=view->cpu_view->header.revision;
    engine.reset();
    require(view->cpu_view->header.revision==revision,"retained scene did not survive engine disposal");
    webscene_scene_release_v3(view);
}
int main() {
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            webscene_native::native_document document;
            webscene_native::v8_dom_runtime runtime(document,[] {
                return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};
            },{},[](uint32_t,const std::string& url,const auto&,const std::string&,int64_t,auto& response) {
                if (url!="https://graphics.test/next") return false;
                response.content="<!doctype html><html><body>next</body></html>";
                return true;
            });
            require(runtime.initialize(),"runtime initialization failed");
            require(runtime.execute("globalThis.gpuDone=0; globalThis.rafDone=0; new Promise(r=>globalThis.gpuResolve=r).then(()=>globalThis.gpuDone=1); requestAnimationFrame(()=>globalThis.rafDone=1);","graphics-test"),"promise setup failed");
            require(runtime.execute("globalThis.canvasProbe=document.createElement('canvas'); canvasProbe.id='backing-probe'; document.body.appendChild(canvasProbe); globalThis.contextProbe=canvasProbe.getContext('2d');","backing-setup"),"canvas backing setup failed");
            auto* canvas_node=document.find_by_id("backing-probe");
            require(canvas_node!=nullptr,"canvas backing node missing");
            const auto& backing=canvas_node->canvas().backing;
            const auto backing_id=backing.identity(),allocation=backing.allocation_generation(),content=backing.content_serial();
            require(runtime.execute("contextProbe.fillRect(0,0,10,10);","backing-draw"),"canvas draw failed");
            require(backing.content_serial()>content && backing.allocation_generation()==allocation,"draw changed allocation generation");
            const auto after_draw=backing.content_serial();
            require(runtime.execute("canvasProbe.style.width='600px';","backing-css-size"),"canvas CSS resize failed");
            require(backing.content_serial()==after_draw && backing.allocation_generation()==allocation,"CSS resize changed bitmap backing");
            require(runtime.execute("canvasProbe.width=300;","backing-reset"),"canvas reset failed");
            require(backing.content_serial()>after_draw,"same-size bitmap reset did not change content");
            require(backing.identity()==backing_id && backing.allocation_generation()==allocation,"same-size reset changed allocation identity");
            require(runtime.execute("canvasProbe.width=640;","backing-resize"),"canvas bitmap resize failed");
            require(backing.width()==640 && backing.height()==150 && backing.allocation_generation()==allocation+1,"bitmap resize missed backing generation");
            require(backing.mode()==canvas_context_mode::two_d,"bitmap reset released context ownership");
            struct canvas_provider final : image_provider_lifetime {};
            owned_image_pool images(std::make_shared<canvas_provider>());
            auto image_writer=images.acquire();
            image_writer->set_metadata({backing.identity(),100,backing.allocation_generation(),backing.content_serial(),1,1,backing.width(),backing.height()});
            image_writer->begin(); auto image_frame=image_writer->publish();
            image_writer->complete();
            auto canvas_image=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image_frame)); image_frame.reset();
            document.layout(640,480);
            const auto before_publication=document.scene_generation();
            document.publish_gpu_canvas_image(*canvas_node,canvas_image);
            require(document.scene_generation()==before_publication+1 && !document.dirty(),
                "GPU publication did not request a scene independently of layout");
            document.publish_gpu_canvas_image(*canvas_node,canvas_image);
            require(document.scene_generation()==before_publication+1,"same image requested redundant scene");
            webscene_native::native_document foreign_document;
            bool foreign_image_rejected=false;
            try { foreign_document.publish_gpu_canvas_image(*canvas_node,canvas_image); }
            catch (const std::invalid_argument&) { foreign_image_rejected=true; }
            require(foreign_image_rejected,"foreign document accepted GPU canvas");

            require(runtime.execute("globalThis.paintBefore=document.createElement('div'); paintBefore.id='gpu-before'; paintBefore.style.cssText='width:20px;height:20px;background:red'; document.body.insertBefore(paintBefore,canvasProbe); globalThis.paintAfter=document.createElement('div'); paintAfter.id='gpu-after'; paintAfter.style.cssText='width:20px;height:20px;background:blue'; document.body.appendChild(paintAfter);","gpu-paint-order"),"GPU paint siblings failed");
            document.layout(640,480);
            std::vector<std::shared_ptr<const webscene_gpu_image_lease_v3>> captured;
            document.build_gpu_canvas_images(captured);
            require(captured.size()==1 && captured[0]==canvas_image,"canvas GPU image capture failed");
            std::vector<webscene_scene_command> paint;
            std::vector<webscene_scene_string> paint_strings;
            std::vector<char> paint_bytes;
            document.build_scene(paint,paint_strings,paint_bytes);
            const auto gpu_paint=std::find_if(paint.begin(),paint.end(),[](const auto& c) {
                return c.kind==WEBSCENE_SCENE_COMMAND_GPU_IMAGE;
            });
            require(gpu_paint!=paint.end() && gpu_paint->node_id==canvas_node->id
                && gpu_paint->x==canvas_node->layout.x && gpu_paint->width==canvas_node->layout.width,
                "GPU image paint placement missing");
            const auto before_id=document.find_by_id("gpu-before")->id;
            const auto after_id=document.find_by_id("gpu-after")->id;
            const auto before_paint=std::find_if(paint.begin(),paint.end(),[&](const auto& c) { return (c.kind==1 || c.kind==9) && c.node_id==before_id; });
            const auto after_paint=std::find_if(paint.begin(),paint.end(),[&](const auto& c) { return (c.kind==1 || c.kind==9) && c.node_id==after_id; });
            require(before_paint<gpu_paint && after_paint!=paint.end() && gpu_paint<after_paint,
                "GPU canvas did not interleave with sibling backgrounds");
            require(runtime.execute("globalThis.mixedCanvas=document.createElement('canvas'); mixedCanvas.id='mixed-2d'; mixedCanvas.width=10; mixedCanvas.height=10; mixedCanvas.getContext('2d').fillRect(0,0,10,10); document.body.insertBefore(mixedCanvas,paintAfter);", "mixed-canvas-setup"), "mixed Canvas2D setup failed");
            document.layout(640,480);
            document.build_scene(paint,paint_strings,paint_bytes,true);
            const auto mixed_id=document.find_by_id("mixed-2d")->id;
            const auto mixed_marker=std::find_if(paint.begin(),paint.end(),[&](const auto& c) {
                return c.kind==WEBSCENE_SCENE_COMMAND_CANVAS_LAYER && c.node_id==mixed_id;
            });
            const auto mixed_gpu=std::find_if(paint.begin(),paint.end(),[](const auto& c) {
                return c.kind==WEBSCENE_SCENE_COMMAND_GPU_IMAGE;
            });
            const auto mixed_after=std::find_if(paint.begin(),paint.end(),[&](const auto& c) {
                return (c.kind==1 || c.kind==9) && c.node_id==after_id;
            });
            require(mixed_marker!=paint.end() && mixed_after!=paint.end() && mixed_gpu<mixed_marker && mixed_marker<mixed_after,
                "Native mixed canvas placement order failed");
            document.build_scene(paint,paint_strings,paint_bytes);
            require(std::none_of(paint.begin(),paint.end(),[](const auto& c) {
                return c.kind==WEBSCENE_SCENE_COMMAND_CANVAS_LAYER;
            }), "Legacy DOM path unexpectedly emitted ordered canvas markers");
            require(runtime.execute("mixedCanvas.style.position='fixed';", "mixed-fixed-canvas"), "fixed canvas setup failed");
            document.layout(640,480); document.build_scene(paint,paint_strings,paint_bytes,true);
            require(std::count_if(paint.begin(),paint.end(),[&](const auto& c) {
                return c.kind==WEBSCENE_SCENE_COMMAND_CANVAS_LAYER && c.node_id==mixed_id;
            })==1, "Fixed canvas lost or duplicated its ordered marker");
            require(runtime.execute("mixedCanvas.remove();", "mixed-canvas-cleanup"), "mixed canvas cleanup failed");
            const auto before_effects=backing.content_serial();
            require(runtime.execute("canvasProbe.style.transform='scale(0.75) rotate(15deg)'; canvasProbe.style.opacity='0.5'; canvasProbe.style.overflow='hidden'; canvasProbe.style.borderRadius='8px';","gpu-paint-effects"),"GPU paint effects setup failed");
            document.layout(640,480); document.build_scene(paint,paint_strings,paint_bytes);
            int clips=0,scales=0,rotations=0,opacity_groups=0,gpu_draws=0;
            for (const auto& command:paint) {
                if (command.node_id!=canvas_node->id) continue;
                switch (command.kind) {
                    case 12: ++clips; if (command.radius_top_left!=8) throw std::runtime_error("GPU clip radius="+std::to_string(command.radius_top_left)+" bounds="+std::to_string(command.width)+"x"+std::to_string(command.height)); break;
                    case 13: --clips; break;
                    case 15: ++scales; require(command.width==0.75F,"GPU scale missing"); break;
                    case 16: --scales; break;
                    case 19: ++rotations; require(command.stroke_width==15,"GPU rotation missing"); break;
                    case 20: --rotations; break;
                    case 30: ++opacity_groups; require(command.rgba==128,"GPU group opacity missing"); break;
                    case 31: --opacity_groups; break;
                    case WEBSCENE_SCENE_COMMAND_GPU_IMAGE:
                        ++gpu_draws;
                        require(clips==1 && scales==1 && rotations==1 && opacity_groups==1,
                            "GPU sampling escaped its paint scopes"); break;
                }
                require(clips>=0 && scales>=0 && rotations>=0 && opacity_groups>=0,"GPU paint scope underflow");
            }
            require(gpu_draws==1 && clips==0 && scales==0 && rotations==0 && opacity_groups==0,
                "GPU paint scopes did not balance");
            require(backing.content_serial()==before_effects,"CSS paint effects changed GPU bitmap content");
            require(runtime.execute("canvasProbe.remove();","gpu-remove"),"canvas removal failed");
            std::vector<std::shared_ptr<const webscene_gpu_image_lease_v3>> detached;
            document.build_gpu_canvas_images(detached);
            require(detached.empty() && captured[0]->value.describe().width==640,"detachment lost retained image");
            require(runtime.execute("document.body.appendChild(canvasProbe);","gpu-reinsert"),"canvas reinsertion failed");
            document.layout(640,480); document.build_gpu_canvas_images(detached);
            require(detached.size()==1,"reinserted canvas lost image");
            require(runtime.execute("canvasProbe.width=800;","gpu-resize"),"GPU canvas reset failed");
            document.build_gpu_canvas_images(detached);
            require(detached.empty() && !canvas_node->canvas().gpu_image,"reset kept stale canvas image");
            document.build_scene(paint,paint_strings,paint_bytes);
            require(std::none_of(paint.begin(),paint.end(),[](const auto& c) {
                return c.kind==WEBSCENE_SCENE_COMMAND_GPU_IMAGE;
            }),"reset kept stale GPU paint operation");
            require(captured[0]->value.describe().width==640,"resize mutated retained frame");
            bool rejected=false;
            try { document.publish_gpu_canvas_image(*canvas_node,canvas_image); }
            catch (const std::invalid_argument&) { rejected=true; }
            require(rejected,"stale image generation accepted");
            image_writer.reset();
            captured.clear(); canvas_image.reset();
            require(images.busy_images()==0,"captured image leaked pool slot");
            auto verify_attribute_reset=[&](const char* script,uint32_t expected_width,uint32_t expected_height,bool changes_size) {
                const auto generation=backing.allocation_generation(),serial=backing.content_serial();
                auto before=images.acquire();
                before->set_metadata({backing.identity(),101,generation,serial,1,2,backing.width(),backing.height()});
                before->begin(); auto ticket=before->publish(); before->complete(); before.reset();
                auto old_image=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*ticket)); ticket.reset();
                document.publish_gpu_canvas_image(*canvas_node,old_image);
                require(runtime.execute(script,"canvas-attribute-reset"),"canvas attribute operation failed");
                require(backing.width()==expected_width && backing.height()==expected_height
                    && backing.content_serial()==serial+1
                    && backing.allocation_generation()==generation+(changes_size ? 1 : 0),
                    "canvas attribute reset missed bitmap version");
                require(!canvas_node->canvas().gpu_image && old_image->value.describe().content_serial==serial,
                    "attribute reset kept current image or mutated retained content");
            };
            verify_attribute_reset("canvasProbe.setAttribute('width','800');",800,150,false);
            verify_attribute_reset("canvasProbe.setAttribute('height','200');",800,200,true);
            verify_attribute_reset("canvasProbe.removeAttribute('width');",300,200,true);
            verify_attribute_reset("canvasProbe.setAttributeNS(null,'width','400');",400,200,true);
            verify_attribute_reset("canvasProbe.removeAttributeNS(null,'width');",300,200,true);
            verify_attribute_reset("globalThis.widthAttr=document.createAttribute('width'); widthAttr.value='500'; canvasProbe.setAttributeNode(widthAttr);",500,200,true);
            verify_attribute_reset("widthAttr.value='600';",600,200,true);
            verify_attribute_reset("canvasProbe.removeAttributeNode(widthAttr);",300,200,true);
            verify_attribute_reset("canvasProbe.toggleAttribute('width',true);",300,200,false);
            verify_attribute_reset("canvasProbe.toggleAttribute('width',false);",300,200,false);
            const auto removed_serial=backing.content_serial();
            require(runtime.execute("canvasProbe.removeAttribute('width'); canvasProbe.toggleAttribute('width',false); widthAttr.value='700';","detached-attribute"),"detached attribute mutation failed");
            require(backing.content_serial()==removed_serial,"absent or detached attribute reset canvas");
            runtime.set_visible(false);
            auto wake=std::make_shared<engine_wake>();
            const auto owner_thread=std::this_thread::get_id();
            bool delivered=false, adapter_delivered=false, adapter_cancelled=false;
            graphics_service* adapter_service=nullptr;
            std::unique_ptr<v8_webgpu_adapter_request> adapter_request, cancelled_adapter;
            std::array<std::unique_ptr<v8_webgpu_adapter_request>,2> failed_wrappers;
            size_t failed_wrapper_count=0;
            bool buffer_wrappers_tested=false;
            auto buffer_test_device=std::make_shared<wgpu::Device>();
            resource_handle<wgpu::Adapter> discovered_adapter;
            std::shared_ptr<release_channel> releases;
            std::unique_ptr<v8_release_registry> wrappers;
            std::unique_ptr<v8_webgpu_buffers> gc_buffers;
            resource_handle<dawn_device> gc_buffer_device;
            auto& graphics=runtime.initialize_graphics(wake,[&](completion_record record) {
                if (record.operation==104) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    auto context=isolate->GetCurrentContext();
                    require(record.status==completion_status::success && *buffer_test_device,"Buffer wrapper fixture device failed");
                    wgpu::Adapter adapter;
                    adapter_service->with_adapter(discovered_adapter,[&](const auto& native) { adapter=native; });
                    auto device_handle=adapter_service->adopt_device(std::move(adapter),std::move(*buffer_test_device));
                    resource_handle<wgpu::Buffer> buffer_handle;
                    adapter_service->with_device(device_handle,[&](auto& device) {
                        wgpu::BufferDescriptor descriptor{};
                        descriptor.size=64; descriptor.usage=wgpu::BufferUsage::CopyDst;
                        descriptor.mappedAtCreation=true;
                        buffer_handle=device.create_buffer(descriptor);
                    });
                    auto buffer_registry=std::make_unique<v8_webgpu_buffers>(isolate,context,1,context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>());
                    auto object=buffer_registry->wrap(context,*adapter_service,device_handle,buffer_handle).ToLocalChecked();
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"bufferProbe"),object).FromMaybe(false),"Buffer wrapper publication failed");
                    bool duplicate_rejected=false,realm_rejected=false;
                    try { buffer_registry->wrap(context,*adapter_service,device_handle,buffer_handle); }
                    catch (const std::invalid_argument&) { duplicate_rejected=true; }
                    try { buffer_registry->wrap(v8::Context::New(isolate),*adapter_service,device_handle,buffer_handle); }
                    catch (const std::logic_error&) { realm_rejected=true; }
                    require(duplicate_rejected && realm_rejected,"Buffer ownership or realm identity was duplicated");

                    auto run=[&](const char* source) {
                        v8::Local<v8::Script> script;
                        return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocal(&script)
                            && !script->Run(context).IsEmpty();
                    };
                    adapter_service->with_device(device_handle,[&](auto& device) {
                        device.with_buffer(buffer_handle,[&](const auto& buffer) {
                            auto* data=buffer.GetMappedRange(0,64);
                            require(data!=nullptr,"Mapped fixture memory unavailable");
                            v8_webgpu_mapped_ranges ranges(isolate,context,object,data,0,64);
                            auto first=ranges.create(context,0,16).ToLocalChecked();
                            auto second=ranges.create(context,16,16).ToLocalChecked();
                            require(first->Data()==data && second->Data()==static_cast<uint8_t*>(data)+16,"Mapped range copied native storage");
                            for (auto request:std::array<std::pair<uint64_t,uint64_t>,4>{{{8,8},{4,4},{32,6},{64,4}}}) {
                                bool rejected=false;
                                try { ranges.create(context,request.first,request.second); }
                                catch (const std::invalid_argument&) { rejected=true; }
                                require(rejected,"Invalid mapped range accepted");
                            }
                            require(!ranges.create(context,0,0).IsEmpty(),"Empty mapped range rejected");
                            require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"mappedProbe"),first).FromMaybe(false),"Mapped view publication failed");
                            require(run("globalThis.mappedWords=new Uint32Array(mappedProbe);mappedWords[0]=0x12345678;"),"Mapped JS write failed");
                            uint32_t word{}; std::memcpy(&word,data,sizeof(word));
                            require(word==0x12345678,"JavaScript write did not reach Dawn mapped memory");
                            {
                                v8::TryCatch caught(isolate);
                                require(first->Detach(v8::Undefined(isolate)).IsNothing() && caught.HasCaught()
                                    && !first->WasDetached(),"Mapped view allowed foreign detachment");
                            }
                            ranges.detach();
                            require(run("if(mappedProbe.byteLength!==0||mappedWords.length!==0)throw new Error('mapped views not detached');delete globalThis.mappedProbe;delete globalThis.mappedWords;"),"Mapped view detachment failed");
                            bool detached_rejected=false;
                            try { ranges.create(context,32,4); } catch (const std::invalid_argument&) { detached_rejected=true; }
                            require(detached_rejected,"Detached mapping accepted a new view");
                        });
                    });
                    require(run(R"JS(
                        globalThis.publicMapped=bufferProbe.getMappedRange(32,16);
                        globalThis.publicMappedWords=new Uint32Array(publicMapped);
                        publicMappedWords[0]=0x87654321;
                        if(bufferProbe.getMappedRange(48).byteLength!==16)throw new Error('default mapped size');
                        for(let args of [[32,8],[4,4],[64,4]]){
                            let rejected=false;try{bufferProbe.getMappedRange(...args)}catch(e){rejected=e instanceof DOMException&&e.name==='OperationError'}
                            if(!rejected)throw new Error('mapped range validation');
                        }
                        for(let args of [[-1],[Infinity],[1n],[0,9007199254740992]]){
                            let rejected=false;try{bufferProbe.getMappedRange(...args)}catch(e){rejected=e instanceof TypeError}
                            if(!rejected)throw new Error('mapped range WebIDL');
                        }
                    )JS"),"JavaScript getMappedRange failed");
                    adapter_service->with_device(device_handle,[&](auto& device) { device.with_buffer(buffer_handle,[&](const auto& buffer) {
                        uint32_t word{}; std::memcpy(&word,static_cast<const uint8_t*>(buffer.GetConstMappedRange(0,64))+32,sizeof(word));
                        require(word==0x87654321,"Public mapped range write missed native memory");
                    }); });
                    require(run("if(bufferProbe.size!==64||bufferProbe.usage!==8||bufferProbe.mapState!=='mapped')throw new Error('buffer metadata'); let p=Object.getPrototypeOf(bufferProbe); for(let f of [p.destroy,Object.getOwnPropertyDescriptor(p,'size').get,Object.getOwnPropertyDescriptor(p,'usage').get,Object.getOwnPropertyDescriptor(p,'mapState').get]){let ok=false;try{f.call({})}catch(e){ok=e instanceof TypeError}if(!ok)throw new Error('buffer brand')} bufferProbe.destroy();bufferProbe.destroy();if(bufferProbe.size!==64||bufferProbe.mapState!=='unmapped')throw new Error('destroy metadata/state');"),"Native buffer wrapper behavior failed");
                    require(run("if(publicMapped.byteLength!==0||publicMappedWords.length!==0)throw new Error('destroy did not detach');delete globalThis.publicMapped;delete globalThis.publicMappedWords;bufferProbe.unmap();"),"Destroy mapping detachment failed");
                    auto unmap_registry=std::make_unique<v8_webgpu_buffers>(isolate,context,1,context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>());
                    adapter_service->with_device(device_handle,[&](auto& device) {
                        wgpu::BufferDescriptor descriptor{}; descriptor.size=32; descriptor.usage=wgpu::BufferUsage::CopyDst; descriptor.mappedAtCreation=true;
                        auto handle=device.create_buffer(descriptor);
                        auto wrapped=unmap_registry->wrap(context,*adapter_service,device_handle,handle).ToLocalChecked();
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"unmapProbe"),wrapped).FromMaybe(false),"Unmap fixture publication failed");
                    });
                    require(run(R"JS(
                        {
                            let view=unmapProbe.getMappedRange(), bytes=new Uint8Array(view);
                            if(view.byteLength!==32)throw new Error('default full mapped range');
                            let conversionUnmapped=false;
                            try{unmapProbe.getMappedRange({valueOf(){unmapProbe.unmap();return 0}})}catch(e){conversionUnmapped=e instanceof DOMException&&e.name==='OperationError'}
                            if(!conversionUnmapped)throw new Error('mapping state not rechecked after conversion');
                            unmapProbe.unmap();
                            if(view.byteLength!==0||bytes.length!==0||unmapProbe.mapState!=='unmapped')throw new Error('unmap did not detach');
                            let rejected=false;try{unmapProbe.getMappedRange()}catch(e){rejected=e instanceof DOMException&&e.name==='OperationError'}
                            if(!rejected)throw new Error('unmapped range accepted');
                            delete globalThis.unmapProbe;
                        }
                    )JS"),"JavaScript unmap failed");
                    unmap_registry.reset();
                    require(run(R"JS(
                        if(bufferProbe.label!=='')throw new Error('label default');
                        bufferProbe.label='a\0\ud800';
                        if(bufferProbe.label!=='a\0\ufffd')throw new Error('label USVString');
                        let labelError={};
                        try {bufferProbe.label={toString(){throw labelError}}}catch(e){if(e!==labelError)throw e}
                        if(bufferProbe.label!=='a\0\ufffd')throw new Error('failed label mutation');
                        let symbolRejected=false;try{bufferProbe.label=Symbol()}catch(e){symbolRejected=e instanceof TypeError}
                        if(!symbolRejected)throw new Error('symbol label accepted');
                        bufferProbe.label={toString(){bufferProbe.destroy();return 'after destroy'}};
                        if(bufferProbe.label!=='after destroy')throw new Error('reentrant label');
                        let brandRejected=false,coerced=false;
                        try{Object.getOwnPropertyDescriptor(Object.getPrototypeOf(bufferProbe),'label').set.call({}, {toString(){coerced=true;return ''}})}catch(e){brandRejected=e instanceof TypeError}
                        if(!brandRejected||coerced)throw new Error('label brand ordering');
                    )JS"),"Buffer label behavior failed");
                    buffer_registry.reset();
                    require(run("let stale=false;try{bufferProbe.destroy()}catch(e){stale=e instanceof TypeError}if(!stale)throw new Error('stale buffer realm');delete globalThis.bufferProbe;"),"Buffer wrapper teardown left native access");
                    gc_buffer_device=device_handle;
                    gc_buffers=std::make_unique<v8_webgpu_buffers>(isolate,context,1,context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>());
                    adapter_service->with_device(device_handle,[&](auto& device) {
                        wgpu::BufferDescriptor descriptor{};
                        descriptor.size=32; descriptor.usage=wgpu::BufferUsage::CopyDst; descriptor.mappedAtCreation=true;
                        auto collectible=device.create_buffer(descriptor);
                        auto gc_object=gc_buffers->wrap(context,*adapter_service,device_handle,collectible).ToLocalChecked();
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"gcBufferProbe"),gc_object).FromMaybe(false),"Collectible buffer wrapper failed");
                        auto get_range=gc_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"getMappedRange")).ToLocalChecked().template As<v8::Function>();
                        auto held_range=get_range->Call(context,gc_object,0,nullptr).ToLocalChecked();
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"gcMappedProbe"),held_range).FromMaybe(false),"Retained mapped view publication failed");
                        auto overflow=device.create_buffer(descriptor);
                        require(gc_buffers->wrap(context,*adapter_service,device_handle,overflow).IsEmpty(),"Buffer wrapper registry was not bounded");
                        device.release_buffer(overflow); // Failed wrap did not take ownership.
                    });
                    buffer_wrappers_tested=true;
                    return;
                }
                if (record.operation==102 || record.operation==103) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    auto context=isolate->GetCurrentContext();
                    auto& request=failed_wrappers[record.operation-102];
                    require(record.status==completion_status::success,"Wrapper failure test needs a hardware adapter");
                    require(request->complete(isolate,context,record,[&](wgpu::Adapter) -> v8::MaybeLocal<v8::Value> {
                        if (record.operation==102) throw std::length_error("resource table full");
                        auto sentinel=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"wrapperSentinel")).ToLocalChecked();
                        isolate->ThrowException(sentinel);
                        return {};
                    }),"Wrapper failure did not reject its promise");
                    require(!request->pending(),"Failed wrapper left request pending");
                    require(!request->complete(isolate,context,record,[](wgpu::Adapter) -> v8::Local<v8::Value> {
                        throw std::runtime_error("duplicate completion wrapped twice");
                    }),"Duplicate completion was accepted");
                    ++failed_wrapper_count;
                    return;
                }
                if (record.operation==101) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    require(record.status==completion_status::cancelled,"Adapter cancellation lost");
                    require(cancelled_adapter->complete(isolate,isolate->GetCurrentContext(),record,
                        [](wgpu::Adapter) -> v8::Local<v8::Value> { throw std::runtime_error("Cancelled adapter was wrapped"); }),
                        "Cancelled adapter promise did not resolve");
                    adapter_cancelled=true;
                    return;
                }
                if (record.operation==100) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    auto context=isolate->GetCurrentContext();
                    require(adapter_request->complete(isolate,context,record,[&](wgpu::Adapter adapter) -> v8::Local<v8::Value> {
                        auto mailbox=adapter_service->dawn().completions();
                        auto ticket=mailbox->reserve(104,{adapter_service->engine_identity(),new_owner_token(),0}).value();
                        wgpu::DeviceDescriptor descriptor{};
                        adapter.RequestDevice(&descriptor,wgpu::CallbackMode::AllowSpontaneous,
                            [mailbox,ticket,buffer_test_device](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView) {
                                *buffer_test_device=std::move(device);
                                mailbox->publish(ticket,status==wgpu::RequestDeviceStatus::Success ? completion_status::success : completion_status::failed);
                            });
                        discovered_adapter=adapter_service->adopt_adapter(std::move(adapter));
                        // Diagnostic wrapper only; the standards GPUAdapter registry is separate work.
                        return v8::Object::New(isolate);
                    }), "Adapter promise completion failed");
                    require(!adapter_request->pending(), "Adapter promise remained pending");
                    adapter_delivered=true;
                    return;
                }
                require(((record.operation==1 || record.operation==2) && record.status==completion_status::success)
                    || (record.operation==3 && record.status==completion_status::cancelled),"unexpected completion");
                if (record.operation==3) {
                    bool rejected=false;
                    try { runtime.initialize_graphics(wake,[](auto) {}); }
                    catch (const std::logic_error&) { rejected=true; }
                    require(rejected,"navigation allowed reentrant graphics initialization");
                    rejected=false;
                    try { runtime.load_url("https://graphics.test/next"); }
                    catch (const std::logic_error&) { rejected=true; }
                    require(rejected,"cancellation delivery allowed reentrant navigation");
                }
                require(std::this_thread::get_id()==owner_thread,"completion left runtime thread");
                auto* isolate=v8::Isolate::GetCurrent();
                require(isolate!=nullptr && isolate->InContext(),"completion has no V8 context");
                auto context=isolate->GetCurrentContext();
                if (record.operation==1) {
                    test_v8_webgpu_adapter_options(isolate,context);
                    test_v8_webgpu_buffer_descriptor(isolate,context);
                    v8::Local<v8::Promise> promise;
                    webgpu_adapter_options options;
                    adapter_request=v8_webgpu_adapter_request::start(isolate,context,options,
                        adapter_service->dawn().instance(),adapter_service->dawn().completions(),
                        {adapter_service->engine_identity(),new_owner_token(),0},100,wgpu::BackendType::Undefined,promise);
                    require(adapter_request && adapter_request->pending(), "Adapter request did not become pending");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"adapterProbePromise"),promise).FromMaybe(false),
                        "Adapter promise publication failed");
                    const resource_owner cancelled_owner{adapter_service->engine_identity(),new_owner_token(),0};
                    v8::Local<v8::Promise> cancelled_promise;
                    cancelled_adapter=v8_webgpu_adapter_request::start(isolate,context,options,
                        adapter_service->dawn().instance(),adapter_service->dawn().completions(),
                        cancelled_owner,101,wgpu::BackendType::Undefined,cancelled_promise);
                    require(cancelled_adapter && cancelled_adapter->pending(),"Cancelled request was not admitted");
                    adapter_service->dawn().completions()->cancel_owner(cancelled_owner);
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"cancelledAdapterPromise"),cancelled_promise).FromMaybe(false),
                        "Cancelled adapter promise publication failed");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"wrapperSentinel"),v8::Object::New(isolate)).FromMaybe(false),"Sentinel publication failed");
                    for (size_t i=0;i<failed_wrappers.size();++i) {
                        v8::Local<v8::Promise> failed_promise;
                        failed_wrappers[i]=v8_webgpu_adapter_request::start(isolate,context,options,
                            adapter_service->dawn().instance(),adapter_service->dawn().completions(),
                            {adapter_service->engine_identity(),new_owner_token(),0},102+i,wgpu::BackendType::Undefined,failed_promise);
                        require(failed_wrappers[i] && failed_wrappers[i]->pending(),"Failure test request was not admitted");
                        require(context->Global()->Set(context,v8::String::NewFromUtf8(isolate,i==0 ? "nativeWrapperFailure" : "jsWrapperFailure").ToLocalChecked(),failed_promise).FromMaybe(false),
                            "Failure test promise publication failed");
                    }
                    // Install rejection observers before returning to the event pump;
                    // spontaneous discovery may finish in this same delivery batch.
                    auto observers=v8::String::NewFromUtf8Literal(isolate,"globalThis.wrapperFailures=0; nativeWrapperFailure.catch(e=>{if(!(e instanceof Error))throw e;wrapperFailures++}); jsWrapperFailure.catch(e=>{if(e!==wrapperSentinel)throw e;wrapperFailures++}); globalThis.adapterPromiseDone=false; globalThis.cancelledAdapterDone=false; adapterProbePromise.then(a=>{if(!a)throw new Error('adapter absent');adapterPromiseDone=true}); cancelledAdapterPromise.then(a=>{if(a!==null)throw new Error('cancelled adapter present');cancelledAdapterDone=true});");
                    v8::Local<v8::Script> observer_script;
                    require(v8::Script::Compile(context,observers).ToLocal(&observer_script)
                        && !observer_script->Run(context).IsEmpty(),"Adapter observer failed");
                    bool rejected=false;
                    try { runtime.load_url("https://graphics.test/next"); }
                    catch (const std::logic_error&) { rejected=true; }
                    require(rejected,"completion delivery allowed destructive navigation");
                    rejected=false;
                    try { runtime.shutdown_graphics(); }
                    catch (const std::logic_error&) { rejected=true; }
                    require(rejected,"completion delivery allowed destructive shutdown");
                    wrappers=std::make_unique<v8_release_registry>(isolate,releases,1);
                    graphics_command release{[](graphics_service&,std::span<const std::byte>,const graphics_command::arguments&) noexcept { ++weak_releases; }};
                    require(wrappers->attach(v8::Object::New(isolate),release),"weak wrapper registration failed");
                    require(!wrappers->attach(v8::Object::New(isolate),release),"weak wrapper capacity was not bounded");
                }
                if (record.operation==2) {
                    adapter_service->with_device(gc_buffer_device,[&](auto& device) {
                        require(device.live_buffers()==0,"Collected buffer native handle survived engine drain");
                        wgpu::BufferDescriptor descriptor{};
                        descriptor.size=16; descriptor.usage=wgpu::BufferUsage::CopyDst; descriptor.mappedAtCreation=true;
                        auto replacement=device.create_buffer(descriptor);
                        auto replacement_object=gc_buffers->wrap(context,*adapter_service,gc_buffer_device,replacement).ToLocalChecked();
                        auto method=replacement_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"getMappedRange")).ToLocalChecked().template As<v8::Function>();
                        auto view=method->Call(context,replacement_object,0,nullptr).ToLocalChecked().template As<v8::ArrayBuffer>();
                        require(view->ByteLength()==16,"Replacement buffer mapping failed");
                        auto stale_device=gc_buffer_device; ++stale_device.generation;
                        require(gc_buffers->detach_device(*adapter_service,stale_device)==0 && view->ByteLength()==16,"Stale device detached a live mapping");
                        require(gc_buffers->detach_device(*adapter_service,gc_buffer_device)==1 && view->ByteLength()==0,"Device-wide detachment failed");
                        require(gc_buffers->detach_device(*adapter_service,gc_buffer_device)==0,"Device detachment was not idempotent");

                    });
                    adapter_service->destroy_device(gc_buffer_device);
                    gc_buffers.reset(); // Delayed wrapper releases tolerate retired native devices.

                    graphics_command release{[](graphics_service&,std::span<const std::byte>,const graphics_command::arguments&) noexcept { ++weak_releases; }};
                    auto reachable=v8::Object::New(isolate);
                    require(wrappers->attach(reachable,release),"weak wrapper slot was not reusable");
                    wrappers.reset();
                    require(weak_releases==1,"registry disposal executed graphics inline");
                }

                auto key=v8::String::NewFromUtf8Literal(isolate,"gpuResolve");
                auto resolve=context->Global()->Get(context,key).ToLocalChecked().As<v8::Function>();
                v8::Local<v8::Value> outcome=v8::Integer::New(isolate,record.status==completion_status::cancelled ? 2 : 1);
                require(!resolve->Call(context,context->Global(),1,&outcome).IsEmpty(),"promise resolution failed");
                delivered=true;
            });
            bool wrong_thread_rejected=false;
            std::thread wrong([&] {
                try { runtime.initialize_graphics(wake,[](auto) {}); }
                catch (const std::logic_error&) { wrong_thread_rejected=true; }
            });
            wrong.join();
            require(wrong_thread_rejected,"wrong-thread initialization accepted");
            adapter_service=&graphics;
            releases=graphics.release_endpoint(4);
            auto mailbox=graphics.dawn().completions();
            auto ticket=mailbox->reserve(1,{graphics.engine_identity(),new_owner_token(),0}).value();
            std::thread native_callback([mailbox,ticket] { mailbox->publish(ticket,completion_status::success); });
            native_callback.join();
            require(!delivered,"native callback entered V8 directly");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (!delivered && std::chrono::steady_clock::now()<deadline) {
                if (runtime.has_pending_tasks()) require(runtime.pump_task(),"runtime task failed");
                else wake->wait_for(runtime.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
            }
            require(delivered,"hidden completion did not progress");

            while ((!adapter_delivered || !adapter_cancelled || !buffer_wrappers_tested || failed_wrapper_count!=2 || mailbox->metrics().native_pending!=0) && std::chrono::steady_clock::now()<deadline) {
                if (runtime.has_pending_tasks()) require(runtime.pump_task(),"Adapter task failed");
                else wake->wait_for(runtime.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
            }
            require(adapter_delivered && discovered_adapter.table && adapter_cancelled,"Actual Dawn adapter discovery/cancellation failed");
            require(runtime.execute("if(!adapterPromiseDone || !cancelledAdapterDone || wrapperFailures!==2 || rafDone!==0)throw new Error('adapter promise did not progress while hidden');", "adapter-promise-check"),"Adapter promise checkpoint failed");
            adapter_service->with_adapter(discovered_adapter,[](const auto& adapter) {
                require(static_cast<bool>(adapter),"Discovered adapter lost its native reference");
            });
            adapter_service->destroy_adapter(discovered_adapter);
            adapter_request.reset(); cancelled_adapter.reset();
            for (auto& request:failed_wrappers) request.reset();
            require(runtime.execute("delete globalThis.gcBufferProbe;", "buffer-drop-reference"),"Buffer reference removal failed");
            size_t buffers_before_gc=0;
            adapter_service->with_device(gc_buffer_device,[&](auto& device) { buffers_before_gc=device.live_buffers(); });
            require(buffers_before_gc>=1,"Collectible buffer disappeared before GC");
            runtime.notify_low_memory();
            adapter_service->with_device(gc_buffer_device,[&](auto& device) {
                require(device.live_buffers()==buffers_before_gc,"GC performed native buffer release inline");
            });
            require(weak_releases==0,"GC callback executed a native graphics release");
            require(runtime.has_pending_tasks(),"GC did not enqueue release work");
            require(runtime.pump_task(),"GC release task failed");
            require(weak_releases==1 && releases->occupied()==1,"Reachable mapped view did not retain its wrapper registration");
            adapter_service->with_device(gc_buffer_device,[&](auto& device) {
                require(device.live_buffers()==1,"Mapped view lost its native buffer after wrapper references were dropped");
            });
            require(runtime.execute("if(gcMappedProbe.byteLength!==32)throw new Error('retained mapping detached');new Uint8Array(gcMappedProbe)[0]=91;delete globalThis.gcMappedProbe;", "mapped-owner-drop"),"Mapped view did not survive owner GC");
            runtime.notify_low_memory();
            adapter_service->with_device(gc_buffer_device,[&](auto& device) {
                require(device.live_buffers()==1,"Mapped-view GC released native storage inline");
            });
            require(runtime.has_pending_tasks() && runtime.pump_task(),"Mapped-view release did not wake the engine");
            adapter_service->with_device(gc_buffer_device,[&](auto& device) {
                require(device.live_buffers()==0 && releases->occupied()==0,"Mapped-view owner release did not drain");
            });
            require(runtime.execute("if(gpuDone!==1 || rafDone!==0) throw new Error('completion or RAF scheduling failed');","graphics-check"),"promise continuation failed without RAF");
            require(runtime.execute("globalThis.gpuDone=0; new Promise(r=>globalThis.gpuResolve=r).then(()=>globalThis.gpuDone=1);","graphics-second-setup"),"second promise setup failed");
            delivered=false;
            auto second=mailbox->reserve(2,{graphics.engine_identity(),new_owner_token(),0}).value();
            require(mailbox->publish(second,completion_status::success),"second completion rejected");
            require(runtime.execute("void 0","graphics-execute-drain"),"execute completion drain failed");
            require(delivered,"execute did not drain completion");
            require(runtime.execute("if(gpuDone!==1 || rafDone!==0) throw new Error('execute checkpoint failed');","graphics-second-check"),"execute promise checkpoint failed");
            require(weak_releases==2 && releases->occupied()==0,"registry disposal release did not drain");
            require(mailbox->metrics().occupied==0,"completion storage not reclaimed");
            require(!runtime.load_url("https://graphics.test/missing"),"missing navigation unexpectedly succeeded");
            const auto old_identity=graphics.engine_identity();
            auto endpoint=graphics.command_endpoint(1,0);
            require(runtime.execute("globalThis.gpuDone=0; new Promise(r=>globalThis.gpuResolve=r).then(status=>globalThis.gpuDone=status);","navigation-setup"),"navigation promise setup failed");
            auto pending=mailbox->reserve(3,{old_identity,new_owner_token(),0}).value();
            require(runtime.load_url("https://graphics.test/next"),"navigation failed");
            require(runtime.execute("if(gpuDone!==2) throw new Error('navigation cancellation missing');","navigation-check"),"navigation did not terminate pending promise");
            require(!mailbox->publish(pending,completion_status::success),"old document callback delivered after navigation");
            graphics_command no_op{[](graphics_service&,std::span<const std::byte>,const graphics_command::arguments&) noexcept {}};
            require(endpoint->enqueue(no_op)==enqueue_result::closed,"old document command endpoint remained open");
            bool disposed=false;
            require(runtime.execute("globalThis.disposeDone=0; new Promise(r=>globalThis.disposeResolve=r).then(()=>globalThis.disposeDone=1);","dispose-setup"),"disposal promise setup failed");
            auto& next_graphics=runtime.initialize_graphics(wake,[&](auto record) {
                require(record.operation==4 && record.status==completion_status::cancelled,"disposal cancellation missing");
                require(std::this_thread::get_id()==owner_thread,"disposal left engine thread");
                auto* isolate=v8::Isolate::GetCurrent();
                require(isolate && isolate->InContext(),"disposal has no context");
                auto context=isolate->GetCurrentContext();
                auto key=v8::String::NewFromUtf8Literal(isolate,"disposeResolve");
                auto resolve=context->Global()->Get(context,key).ToLocalChecked().As<v8::Function>();
                require(!resolve->Call(context,context->Global(),0,nullptr).IsEmpty(),"disposal resolution failed");
                disposed=true;
            });
            require(next_graphics.engine_identity()!=old_identity,"navigation reused graphics identity");
            auto final_mailbox=next_graphics.dawn().completions();
            auto final_ticket=final_mailbox->reserve(4,{next_graphics.engine_identity(),new_owner_token(),0}).value();
            runtime.shutdown_graphics();
            runtime.shutdown_graphics();
            require(disposed,"shutdown did not deliver cancellation");
            require(runtime.execute("if(disposeDone!==1) throw new Error('disposal checkpoint missing');","dispose-check"),"disposal checkpoint failed");
            require(!final_mailbox->publish(final_ticket,completion_status::success),"late disposal callback delivered");
            bool restart_rejected=false;
            try { runtime.initialize_graphics(wake,[](auto) {}); }
            catch (const std::logic_error&) { restart_rejected=true; }
            require(restart_rejected,"disposed graphics runtime restarted");
        } catch (...) { failure=std::current_exception(); }
    });
    worker.join();
    if (failure) {
        try { std::rethrow_exception(failure); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
        return 1;
    }
    test_native_gpu_scene_leases();
    test_image_lease_abi();
    test_scene_acquisition_v3();
    std::cout << "Hidden V8 graphics completion, context affinity and promise checkpoint passed without RAF\n";
}
