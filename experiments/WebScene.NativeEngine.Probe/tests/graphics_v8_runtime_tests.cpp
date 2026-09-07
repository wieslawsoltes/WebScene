#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include "graphics/v8_release_registry.h"
#include "graphics/image_lease_abi.h"
#include <v8.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int weak_releases=0;
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
            canvas_node->mutable_canvas().publish_gpu_image(canvas_image);
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
            try { canvas_node->mutable_canvas().publish_gpu_image(canvas_image); }
            catch (const std::invalid_argument&) { rejected=true; }
            require(rejected,"stale image generation accepted");
            image_writer.reset();
            captured.clear(); canvas_image.reset();
            require(images.busy_images()==0,"captured image leaked pool slot");
            runtime.set_visible(false);
            auto wake=std::make_shared<engine_wake>();
            const auto owner_thread=std::this_thread::get_id();
            bool delivered=false;
            std::shared_ptr<release_channel> releases;
            std::unique_ptr<v8_release_registry> wrappers;
            auto& graphics=runtime.initialize_graphics(wake,[&](completion_record record) {
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
            releases=graphics.release_endpoint(1);
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
            runtime.notify_low_memory();
            require(weak_releases==0,"GC callback executed a native graphics release");
            require(runtime.has_pending_tasks(),"GC did not enqueue release work");
            require(runtime.pump_task(),"GC release task failed");
            require(weak_releases==1 && releases->occupied()==0,"weak wrapper release did not drain");
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
    test_image_lease_abi();
    test_scene_acquisition_v3();
    std::cout << "Hidden V8 graphics completion, context affinity and promise checkpoint passed without RAF\n";
}
