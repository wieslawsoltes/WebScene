#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include <v8.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            webscene_native::native_document document;
            webscene_native::v8_dom_runtime runtime(document,[] {
                return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};
            });
            require(runtime.initialize(),"runtime initialization failed");
            require(runtime.execute("globalThis.gpuDone=0; globalThis.rafDone=0; new Promise(r=>globalThis.gpuResolve=r).then(()=>globalThis.gpuDone=1); requestAnimationFrame(()=>globalThis.rafDone=1);","graphics-test"),"promise setup failed");
            runtime.set_visible(false);
            auto wake=std::make_shared<engine_wake>();
            const auto owner_thread=std::this_thread::get_id();
            bool delivered=false;
            auto& graphics=runtime.initialize_graphics(wake,[&](completion_record record) {
                require((record.operation==1 || record.operation==2) && record.status==completion_status::success,"unexpected completion");
                require(std::this_thread::get_id()==owner_thread,"completion left runtime thread");
                auto* isolate=v8::Isolate::GetCurrent();
                require(isolate!=nullptr && isolate->InContext(),"completion has no V8 context");
                auto context=isolate->GetCurrentContext();
                auto key=v8::String::NewFromUtf8Literal(isolate,"gpuResolve");
                auto resolve=context->Global()->Get(context,key).ToLocalChecked().As<v8::Function>();
                require(!resolve->Call(context,context->Global(),0,nullptr).IsEmpty(),"promise resolution failed");
                delivered=true;
            });
            bool wrong_thread_rejected=false;
            std::thread wrong([&] {
                try { runtime.initialize_graphics(wake,[](auto) {}); }
                catch (const std::logic_error&) { wrong_thread_rejected=true; }
            });
            wrong.join();
            require(wrong_thread_rejected,"wrong-thread initialization accepted");
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
            require(runtime.execute("if(gpuDone!==1 || rafDone!==0) throw new Error('completion or RAF scheduling failed');","graphics-check"),"promise continuation failed without RAF");
            require(runtime.execute("globalThis.gpuDone=0; new Promise(r=>globalThis.gpuResolve=r).then(()=>globalThis.gpuDone=1);","graphics-second-setup"),"second promise setup failed");
            delivered=false;
            auto second=mailbox->reserve(2,{graphics.engine_identity(),new_owner_token(),0}).value();
            require(mailbox->publish(second,completion_status::success),"second completion rejected");
            require(runtime.execute("void 0","graphics-execute-drain"),"execute completion drain failed");
            require(delivered,"execute did not drain completion");
            require(runtime.execute("if(gpuDone!==1 || rafDone!==0) throw new Error('execute checkpoint failed');","graphics-second-check"),"execute promise checkpoint failed");
            require(mailbox->metrics().occupied==0,"completion storage not reclaimed");
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
