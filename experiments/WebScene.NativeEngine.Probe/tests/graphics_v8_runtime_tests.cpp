#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include "graphics/v8_release_registry.h"
#include <v8.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int weak_releases=0;
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
    std::cout << "Hidden V8 graphics completion, context affinity and promise checkpoint passed without RAF\n";
}
