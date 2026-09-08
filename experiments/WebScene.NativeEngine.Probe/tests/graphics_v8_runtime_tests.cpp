#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include "graphics/v8_release_registry.h"
#include "graphics/v8_webgpu_adapter_request.h"
#include "graphics/v8_webgpu_device_request.h"
#include "graphics/webgpu_prepared_device_descriptor.h"
#include "graphics/v8_webgpu_buffers.h"
#include "graphics/v8_webgpu_devices.h"
#include "graphics/v8_webgpu_shaders.h"
#include "graphics/webgpu_compilation_info.h"
#include "graphics/v8_webgpu_render_pipelines.h"
#include "graphics/v8_webgpu_bind_group_layouts.h"
#include "graphics/v8_webgpu_pipeline_layouts.h"
#include "graphics/v8_webgpu_bind_groups.h"
#include "graphics/v8_webgpu_texture_views.h"
#include "graphics/v8_webgpu_adapters.h"
#include "graphics/v8_webgpu_discovery.h"
#include "graphics/v8_webgpu_canvas_context.h"
#include "graphics/webgpu_adapter_info.h"
#include "graphics/v8_webgpu_mapped_ranges.h"
#include "graphics/v8_webgpu_map_request.h"
#include "graphics/image_lease_abi.h"
#include <v8.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
#include "graphics_v8_webgpu_options.h"
#include "graphics_v8_webgpu_buffer_descriptor.h"
#include "graphics_v8_webgpu_device_descriptor.h"
#include "graphics_v8_webgpu_shader_descriptor.h"
#include "graphics_v8_webgpu_programmable_stage.h"
#include "graphics_v8_webgpu_render_state.h"
#include "graphics_v8_webgpu_texture_descriptor.h"
#include "graphics_v8_webgpu_render_pass_descriptor.h"
#include "graphics_v8_webgpu_canvas_configuration.h"
#include "graphics_v8_iosurface_canvas_host.h"
int weak_releases=0;
void test_native_gpu_scene_leases();
void test_device_loss_signal() {
    resource_owner owner{new_owner_token(),new_owner_token(),0};
    for(bool early:{false,true}) {
        auto mailbox=std::make_shared<completion_mailbox>(1,nullptr);
        auto signal=std::make_shared<device_loss_signal>(nullptr);
        auto ticket=mailbox->reserve(new_owner_token(),owner,false).value();
        std::string message="driver loss";
        auto publish=[&]{signal->publish(wgpu::DeviceLostReason::Unknown,wgpu::StringView(message.data(),message.size()));};
        if(early){std::thread callback(publish);callback.join();}
        signal->subscribe(mailbox,ticket);
        if(!early){std::thread callback(publish);callback.join();}
        message[0]='X';
        require(signal->lost.load()&&signal->result()->message=="driver loss","Loss callback did not own its snapshot");
        unsigned count=0;require(mailbox->drain_one([&](auto record){++count;require(record.status==completion_status::success,"Loss completion failed");}),"Loss notification missing");
        signal->publish(wgpu::DeviceLostReason::Destroyed,wgpu::StringView("duplicate"));
        require(count==1&&!mailbox->has_ready(),"Loss delivered twice");
    }
}
void test_compilation_info_snapshot() {
    std::string text="diagnostic";
    wgpu::CompilationMessage message{};
    message.message=wgpu::StringView(text.data(),text.size());
    message.type=wgpu::CompilationMessageType::Error;
    message.lineNum=2;message.linePos=3;message.offset=4;message.length=5;
    wgpu::CompilationInfo info{};info.messageCount=1;info.messages=&message;
    wgpu::DawnCompilationMessageUtf16 utf16{};
    utf16.linePos=2;utf16.offset=3;utf16.length=4;
    message.nextInChain=&utf16;
    auto snapshot=webgpu_compilation_info::copy(info);
    text[0]='X';
    require(snapshot.messages.size()==1&&snapshot.messages[0].message=="diagnostic"
        &&snapshot.messages[0].offset==4&&snapshot.messages[0].length==5
        &&snapshot.messages[0].has_utf16&&snapshot.messages[0].utf16_offset==3
        &&snapshot.messages[0].utf16_line_pos==2&&snapshot.messages[0].utf16_length==4,
        "Compilation diagnostics did not retain callback data");
    bool bounded=false;try{webgpu_compilation_info::copy(info,1,2);}catch(const std::length_error&){bounded=true;}
    require(bounded,"Compilation diagnostic byte budget ignored");
    bounded=false;try{webgpu_compilation_info::copy(info,0);}catch(const std::length_error&){bounded=true;}
    require(bounded,"Compilation diagnostic count budget ignored");
    message.message=wgpu::StringView("terminated");
    require(webgpu_compilation_info::copy(info).messages[0].message=="terminated","NUL-terminated diagnostic copy failed");
}
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
void test_inline_canvas_intrinsic_layout() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(),"Inline canvas runtime failed");
    require(runtime.execute(R"JS(
        document.body.innerHTML='<span><canvas id="intrinsic" width="256" height="128">fallback</canvas></span>';
    )JS","inline-canvas"),"Inline canvas setup failed");
    document.layout(640,480);
    auto* canvas=document.find_by_id("intrinsic");
    require(canvas&&canvas->layout.width==256&&canvas->layout.height==128,"Inline canvas lost intrinsic dimensions");
    require(runtime.execute("document.getElementById('intrinsic').removeAttribute('width');document.getElementById('intrinsic').removeAttribute('height');","default-canvas"),"Canvas dimension removal failed");
    document.layout(640,480);
    require(canvas->layout.width==300&&canvas->layout.height==150,"Inline canvas defaults lost");
}
void test_runtime_webgpu_document_policy() {
#if defined(__APPLE__)
    webscene_native::native_document document;
    std::vector<std::string> decisions;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{64,64,1,0};},
        {},[](uint32_t,const std::string& url,const auto&,const std::string&,int64_t,auto& response){
            if(url=="https://graphics.test/missing")return false;
            response.content="<!doctype html><html><body><script>globalThis.policySeenByScript=('gpu' in navigator);</script></body></html>";
            return true;
        });
    runtime.set_webgpu_policy(std::make_shared<engine_wake>(),[&](const std::string& url){
        decisions.push_back(url);
        return url=="https://graphics.test/allowed" ? webgpu_canvas_interop::iosurface : webgpu_canvas_interop::none;
    });
    require(runtime.initialize(),"Policy runtime initialization failed");
    require(decisions==std::vector<std::string>{"about:blank"},"Initial document policy missing");
    require(runtime.execute("if('gpu' in navigator)throw new Error('blank admission');","blank-policy"),"Blank policy denied incorrectly");
    require(runtime.load_url("https://graphics.test/allowed"),"Allowed policy navigation failed");
    require(runtime.execute("if(!policySeenByScript||!navigator.gpu)throw new Error('late admission');","allowed-policy"),"Policy ran after application script");
    require(!runtime.load_url("https://graphics.test/missing"),"Missing policy resource loaded");
    require(decisions.size()==2,"Failed navigation changed admission");
    require(runtime.execute("if(!navigator.gpu)throw new Error('failed navigation retired GPU');","failed-policy"),"Failed navigation changed GPU exposure");
    require(runtime.load_url("https://graphics.test/denied"),"Denied policy navigation failed");
    require(runtime.execute("if(policySeenByScript||('gpu' in navigator))throw new Error('stale admission');","denied-policy"),"Denied navigation retained GPU exposure");
    require(decisions==std::vector<std::string>{"about:blank","https://graphics.test/allowed","https://graphics.test/denied"},"Document policy URL sequence incorrect");
#endif
}
void test_runtime_webgpu_installation() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,[]{return webscene_native::v8_dom_runtime::viewport_metrics{64,64,1,0};},
        {},[](uint32_t,const std::string&,const auto&,const std::string&,int64_t,auto& response){response.content="<!doctype html><html><body>next</body></html>";return true;});
    require(runtime.initialize(),"WebGPU installation runtime failed");
    auto wake=std::make_shared<engine_wake>();
    require(!runtime.install_webgpu(wake,false,webgpu_canvas_interop::none),"Insecure WebGPU installation accepted");
    require(runtime.execute("if('gpu' in navigator)throw new Error('insecure GPU exposure');","denied-gpu"),"Denied GPU exposure failed");
#if defined(__APPLE__)
    constexpr auto runtime_interop=webgpu_canvas_interop::iosurface;
#else
    constexpr auto runtime_interop=webgpu_canvas_interop::none;
#endif
    require(runtime.install_webgpu(wake,true,runtime_interop),"Secure WebGPU installation failed");
    require(runtime.execute(R"JS(
        if(navigator.gpu!==navigator.gpu)throw new Error('GPU identity');
        navigator.gpu.requestAdapter().then(adapter=>{
            if(!adapter)throw new Error('adapter unavailable');
            return adapter.requestDevice();
        }).then(device=>{
            globalThis.installedDevice=device;
            const ready=document.createElement('div');ready.id='gpu-installed-ready';document.body.appendChild(ready);
        });
    )JS","installed-gpu"),"Installed WebGPU request failed");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!document.find_by_id("gpu-installed-ready")&&std::chrono::steady_clock::now()<deadline){
        require(runtime.pump_task(),"Installed GPU completion failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(document.find_by_id("gpu-installed-ready")!=nullptr,"Installed GPU did not create a device");
    require(runtime.execute(R"JS(
        {
            if(!(installedDevice instanceof EventTarget))throw new Error('GPUDevice EventTarget inheritance');
            let calls=0;
            function listener(event){
                if(this!==installedDevice||event.target!==installedDevice||event.currentTarget!==installedDevice)throw new Error('GPUDevice event target');
                ++calls;
            }
            installedDevice.addEventListener('probe',listener);
            installedDevice.addEventListener('probe',listener);
            installedDevice.dispatchEvent(new Event('probe'));
            if(calls!==1)throw new Error('GPUDevice duplicate listener');
            installedDevice.removeEventListener('probe',listener);
            installedDevice.dispatchEvent(new Event('probe'));
            if(calls!==1)throw new Error('GPUDevice listener removal');
            installedDevice.addEventListener('probe',listener,{once:true});
            installedDevice.dispatchEvent(new Event('probe'));installedDevice.dispatchEvent(new Event('probe'));
            if(calls!==2)throw new Error('GPUDevice once listener');
            let windowCalls=0;const windowListener=()=>++windowCalls;
            window.addEventListener('probe',windowListener);
            installedDevice.dispatchEvent(new Event('probe'));
            window.removeEventListener('probe',windowListener);
            if(windowCalls)throw new Error('GPUDevice dispatched to window');
        }
    )JS","device-events"),"GPUDevice EventTarget integration failed");
    require(runtime.execute(R"JS(
        (async()=>{
            if(!(installedDevice.lost instanceof Promise)||installedDevice.lost!==installedDevice.lost)throw new Error('lost SameObject promise');
            const adapter=await navigator.gpu.requestAdapter();const device=await adapter.requestDevice();
            const promise=device.lost;device.destroy();device.destroy();
            const info=await promise;
            if(!(info instanceof GPUDeviceLostInfo)||info.reason!=='destroyed'||typeof info.message!=='string')throw new Error('destroyed loss result');
            if(Object.prototype.toString.call(info)!=='[object GPUDeviceLostInfo]')throw new Error('lost info tag');
            if(device.lost!==promise||await device.lost!==info)throw new Error('lost identity changed');
            let rejected=false;try{new GPUDeviceLostInfo()}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('lost info constructible');
            const getter=Object.getOwnPropertyDescriptor(GPUDeviceLostInfo.prototype,'reason').get;
            rejected=false;try{getter.call({})}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('lost info receiver accepted');
            const node=document.createElement('div');node.id='loss-ready';document.body.appendChild(node);
        })();
    )JS","device-lost"),"GPUDevice lost request failed");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!document.find_by_id("loss-ready")&&std::chrono::steady_clock::now()<deadline) {
        require(runtime.pump_task(),"Device loss completion failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(document.find_by_id("loss-ready")!=nullptr,"Device destruction did not resolve lost promise");
    require(runtime.execute(R"JS(
        (async()=>{
            const queue=installedDevice.queue;
            if(queue.writeBuffer.length!==3)throw new Error('writeBuffer arity');
            const buffer=installedDevice.createBuffer({size:32,usage:9});
            const source=new Uint32Array([11,22,33,44]);
            queue.writeBuffer(buffer,0,source.subarray(1),1,1);
            source.fill(99);
            const bytes=new Uint8Array([1,2,3,4,5,6,7,8]);
            queue.writeBuffer(buffer,4,new DataView(bytes.buffer,2,6),1,4);
            queue.writeBuffer(buffer,8,bytes.buffer,4,4);
            const shared=new SharedArrayBuffer(8);new Uint32Array(shared).set([55,66]);
            queue.writeBuffer(buffer,12,new Uint32Array(shared),1,1);
            queue.writeBuffer(buffer,16,shared,0,4);
            queue.writeBuffer(buffer,32,new ArrayBuffer(0));
            await buffer.mapAsync(1);
            const result=new DataView(buffer.getMappedRange());
            if(result.getUint32(0,true)!==33||result.getUint32(4,true)!==0x07060504
                ||result.getUint32(8,true)!==0x08070605||result.getUint32(12,true)!==66||result.getUint32(16,true)!==55)
                throw new Error('writeBuffer uploaded wrong bytes');
            buffer.unmap();
            for(const args of [[buffer,0,bytes,9],[buffer,0,bytes,0,3],[buffer,0,bytes,4,8]]) {
                let rejected=false;try{queue.writeBuffer(...args)}catch(e){rejected=e instanceof DOMException&&e.name==='OperationError'}
                if(!rejected)throw new Error('invalid source range accepted');
            }
            for(const args of [[],[{},0,bytes],[buffer,-1,bytes],[buffer,0,{}],[buffer,0,bytes,-1]]) {
                let rejected=false;try{queue.writeBuffer(...args)}catch(e){rejected=e instanceof TypeError}
                if(!rejected)throw new Error('invalid writeBuffer conversion accepted');
            }
            const detached=new ArrayBuffer(8);detached.transfer();
            const resizable=new ArrayBuffer(8,{maxByteLength:16});
            for(const source of [detached,resizable]) {
                let rejected=false;try{queue.writeBuffer(buffer,0,source)}catch(e){rejected=e instanceof TypeError}
                if(!rejected)throw new Error('invalid backing store accepted');
            }
            const reentrant=new ArrayBuffer(8);let detachedRejected=false;
            try{queue.writeBuffer(buffer,0,reentrant,{valueOf(){reentrant.transfer();return 0}})}catch(e){detachedRejected=e instanceof TypeError}
            if(!detachedRejected)throw new Error('detachment during conversion accepted');
            const sentinel={};let propagated=false;
            try{queue.writeBuffer(buffer,{valueOf(){throw sentinel}},bytes)}catch(e){propagated=e===sentinel}
            if(!propagated)throw new Error('writeBuffer conversion exception lost');
            installedDevice.pushErrorScope('validation');
            queue.writeBuffer(buffer,2,new Uint32Array([1]));
            if(!(await installedDevice.popErrorScope() instanceof GPUValidationError))throw new Error('unaligned destination did not reach Dawn validation');
            const node=document.createElement('div');node.id='write-ready';document.body.appendChild(node);
        })();
    )JS","queue-write-buffer"),"writeBuffer request failed");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!document.find_by_id("write-ready")&&std::chrono::steady_clock::now()<deadline) {
        require(runtime.pump_task(),"writeBuffer completion failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(document.find_by_id("write-ready")!=nullptr,"writeBuffer readback did not pass");
    require(runtime.execute(R"JS(
        {
            const host=document.createElement('div');
            host.innerHTML='<form id="form-data-probe"><input name="x" value="10"><input name="x" value="20"><input name="omit" disabled value="bad"><input type="checkbox" name="checked" checked><input type="checkbox" name="unchecked"><fieldset disabled><legend><input name="legend" value="yes"></legend><input name="blocked" value="bad"></fieldset><select name="choice" multiple><option value="a" selected>A</option><option value="b" selected disabled>B</option><optgroup disabled><option value="c" selected>C</option></optgroup></select><textarea name="text">hello</textarea><button name="send" value="go" type="submit">Go</button></form><input name="external" value="outside" form="form-data-probe">';
            document.body.appendChild(host);
            const form=document.getElementById('form-data-probe');
            const data=new FormData(form);
            const expected=[['x','10'],['x','20'],['checked','on'],['legend','yes'],['choice','a'],['text','hello'],['external','outside']];
            if(JSON.stringify([...data])!==JSON.stringify(expected))throw new Error('form entries: '+JSON.stringify([...data]));
            form.querySelector('[name=x]').value='changed';
            if(data.get('x')!=='10')throw new Error('FormData was not a snapshot');
            const submitter=form.querySelector('button');
            if(new FormData(form,submitter).get('send')!=='go')throw new Error('submitter missing');
            for(const value of [null,{},host]) {
                let rejected=false;try{new FormData(value)}catch(e){rejected=e instanceof TypeError}
                if(!rejected)throw new Error('invalid form accepted');
            }
            let rejected=false;try{new FormData(form,form.querySelector('input'))}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('invalid submitter accepted');
            host.remove();
        }
    )JS","form-data-controls"),"FormData form control collection failed");
    const bool dialog_layout_ok=runtime.execute(R"JS(
        {
            const dialog=document.createElement('dialog');dialog.textContent='Dialog contents';document.body.appendChild(dialog);
            if(!(dialog instanceof HTMLDialogElement)||!(dialog instanceof HTMLElement)||Object.prototype.toString.call(dialog)!=='[object HTMLDialogElement]')throw new Error('dialog interface missing: '+[dialog instanceof HTMLDialogElement,dialog instanceof HTMLElement,Object.prototype.toString.call(dialog)]);
            const tag=Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype,Symbol.toStringTag);
            if(tag.value!=='HTMLDialogElement'||tag.writable||tag.enumerable||!tag.configurable)throw new Error('dialog tag descriptor');
            if(dialog.open!==false||dialog.returnValue!=='')throw new Error('dialog initial state');
            dialog.returnValue=42;
            if(dialog.returnValue!=='42'||dialog.hasAttribute('returnValue'))throw new Error('dialog returnValue incorrectly reflected');
            dialog.setAttribute('returnValue','authored');
            if(dialog.returnValue!=='42')throw new Error('attribute changed returnValue');
            let rejected=false;try{dialog.returnValue=Symbol()}catch(e){rejected=e instanceof TypeError}
            if(!rejected||dialog.returnValue!=='42')throw new Error('dialog DOMString conversion');
            const getter=Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype,'open').get;
            rejected=false;try{getter.call(document.createElement('div'))}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('dialog getter brand');
            dialog.returnValue='a\0\ud800z';
            if(dialog.returnValue!=='a\0\ud800z')throw new Error('dialog DOMString roundtrip');
            if(dialog.cloneNode().returnValue!=='')throw new Error('dialog clone copied internal state');
            if(getComputedStyle(dialog).display!=='none'||dialog.getBoundingClientRect().height!==0)throw new Error('closed dialog participates in layout');
            dialog.open=true;
            if(!dialog.hasAttribute('open'))throw new Error('open did not reflect');
            if(getComputedStyle(dialog).display==='none'||dialog.getBoundingClientRect().height<=0)throw new Error('open dialog stayed hidden');
            dialog.removeAttribute('open');
            if(dialog.open!==false)throw new Error('open getter ignored attribute removal');
            dialog.open='nonempty';
            if(!dialog.open)throw new Error('open truthy conversion');
            dialog.open=0;
            if(dialog.hasAttribute('open'))throw new Error('open false did not remove attribute');
            if(getComputedStyle(dialog).display!=='none'||dialog.getBoundingClientRect().height!==0)throw new Error('closing dialog retained its box');
            dialog.style.display='block';
            if(getComputedStyle(dialog).display!=='block')throw new Error('author display did not override dialog default');
            dialog.remove();
        }
    )JS","closed-dialog-layout");
    if (!dialog_layout_ok) throw std::runtime_error("Dialog default visibility failed: " + runtime.last_error());
    require(runtime.execute(R"JS(
        if(installedDevice.createBindGroupLayout.length!==1)throw new Error('binding layout arity');
        globalThis.bindingLayout=installedDevice.createBindGroupLayout({label:'camera',entries:new Set([{binding:0,visibility:1,buffer:{}}])});
        if(Object.prototype.toString.call(bindingLayout)!=='[object GPUBindGroupLayout]'||bindingLayout.label!=='camera')throw new Error('binding layout wrapper');
        for(const descriptor of [{},{entries:[{visibility:1}]},{entries:[{binding:0}]},{entries:[{binding:0,visibility:1,buffer:{type:'bad'}}]}]){
            let rejected=false;try{installedDevice.createBindGroupLayout(descriptor)}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('binding descriptor accepted');
        }
        bindingLayout.label='updated';if(bindingLayout.label!=='updated')throw new Error('binding layout label');
    )JS","binding-layout"),"Binding layout creation failed");
    require(runtime.execute(R"JS(
        {
        if(installedDevice.createBindGroup.length!==1)throw new Error('bind group arity');
        const buffer=installedDevice.createBuffer({size:64,usage:64});
        const order=[];
        const binding=new Proxy({buffer,offset:0,size:64},{get(target,key){order.push('buffer.'+key);return target[key]}});
        const entry=new Proxy({binding:0,resource:binding},{get(target,key){order.push('entry.'+key);return target[key]}});
        const descriptor=new Proxy({label:'camera group',entries:new Set([entry]),layout:bindingLayout},{get(target,key){order.push(key);return target[key]}});
        const group=installedDevice.createBindGroup(descriptor);
        if(order.join(',')!=='label,entries,entry.binding,entry.resource,buffer.buffer,buffer.offset,buffer.size,layout')throw new Error('binding conversion order: '+order);
        if(Object.prototype.toString.call(group)!=='[object GPUBindGroup]'||group.label!=='camera group')throw new Error('bind group wrapper');
        group.label='renamed';if(group.label!=='renamed')throw new Error('bind group label');
        installedDevice.createBindGroup({layout:bindingLayout,entries:[{binding:0,resource:buffer}]});
        for(const bad of [{},{entries:[]},{layout:bindingLayout,entries:[{binding:0}]},
            {layout:group,entries:[]},{layout:bindingLayout,entries:[{binding:0,resource:{buffer:{}}}]},
            {layout:bindingLayout,entries:[{binding:0,resource:{buffer,offset:-1}}]}]) {
            let rejected=false;try{installedDevice.createBindGroup(bad)}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('invalid bind group accepted');
        }
        const texture=installedDevice.createTexture({size:[2,2],format:'rgba8unorm',usage:4});
        const textureLayout=installedDevice.createBindGroupLayout({entries:[{binding:0,visibility:2,texture:{}}]});
        for(const resource of [texture,texture.createView()])
            installedDevice.createBindGroup({layout:textureLayout,entries:[{binding:0,resource}]});
        const sentinel={};let propagated=false;
        try{installedDevice.createBindGroup({get entries(){throw sentinel}})}catch(e){propagated=e===sentinel}
        if(!propagated)throw new Error('binding getter exception lost');
        let receiverRejected=false;try{installedDevice.createBindGroup.call({}, {})}catch(e){receiverRejected=e instanceof TypeError}
        if(!receiverRejected)throw new Error('binding receiver accepted');
        }
    )JS","binding-group"),"Bind group creation failed");
    require(runtime.execute(R"JS(
        {
            if(installedDevice.createPipelineLayout.length!==1)throw new Error('pipeline layout arity');
            const order=[];
            const descriptor=new Proxy({label:'explicit layout',bindGroupLayouts:new Set([bindingLayout]),immediateSize:0},
                {get(target,key){order.push(key);return target[key]}});
            const layout=installedDevice.createPipelineLayout(descriptor);
            if(order.join(',')!=='label,bindGroupLayouts,immediateSize')throw new Error('pipeline layout conversion order');
            if(Object.prototype.toString.call(layout)!=='[object GPUPipelineLayout]'||layout.label!=='explicit layout')throw new Error('pipeline layout wrapper');
            layout.label='updated';if(layout.label!=='updated')throw new Error('pipeline layout label');
            installedDevice.createPipelineLayout({bindGroupLayouts:[null,undefined]});
            for(const bad of [{},{bindGroupLayouts:[{}]},{bindGroupLayouts:[layout]},
                {bindGroupLayouts:[],immediateSize:-1},{bindGroupLayouts:[],immediateSize:Infinity}]) {
                let rejected=false;try{installedDevice.createPipelineLayout(bad)}catch(e){rejected=e instanceof TypeError}
                if(!rejected)throw new Error('invalid pipeline layout accepted');
            }
            const sentinel={};let propagated=false;
            try{installedDevice.createPipelineLayout({get bindGroupLayouts(){throw sentinel}})}catch(e){propagated=e===sentinel}
            if(!propagated)throw new Error('pipeline layout getter exception lost');
            const module=installedDevice.createShaderModule({code:'@vertex fn main()->@builtin(position) vec4f{return vec4f(0,0,0,1);}',compilationHints:[{entryPoint:'main',layout}]});
            const pipeline=installedDevice.createRenderPipeline({layout,vertex:{module},primitive:{topology:'point-list'}});
            if(Object.prototype.toString.call(pipeline)!=='[object GPURenderPipeline]')throw new Error('explicit pipeline creation');
        }
    )JS","pipeline-layout"),"Pipeline layout creation failed");
    require(runtime.execute(R"JS(
        const namespaces={GPUBufferUsage:{MAP_READ:1,MAP_WRITE:2,COPY_SRC:4,COPY_DST:8,INDEX:16,VERTEX:32,UNIFORM:64,STORAGE:128,INDIRECT:256,QUERY_RESOLVE:512},
          GPUTextureUsage:{COPY_SRC:1,COPY_DST:2,TEXTURE_BINDING:4,STORAGE_BINDING:8,RENDER_ATTACHMENT:16,TRANSIENT_ATTACHMENT:32},
          GPUMapMode:{READ:1,WRITE:2},GPUShaderStage:{VERTEX:1,FRAGMENT:2,COMPUTE:4},GPUColorWrite:{RED:1,GREEN:2,BLUE:4,ALPHA:8,ALL:15}};
        for(const [name,values] of Object.entries(namespaces)){
          const object=globalThis[name];if(Object.prototype.toString.call(object)!=='[object '+name+']')throw new Error('namespace brand');
          for(const [key,value] of Object.entries(values)){const d=Object.getOwnPropertyDescriptor(object,key);
            if(d.value!==value||d.writable||d.configurable||!d.enumerable)throw new Error('flag descriptor');}
        }
    )JS","gpu-constants"),"GPU flag namespaces failed");
    require(runtime.execute(R"JS(
        (async()=>{
            if(installedDevice.popErrorScope.length!==0)throw new Error('pop scope arity');
            for(const Type of [GPUValidationError,GPUOutOfMemoryError,GPUInternalError]) {
                const error=new Type('diagnostic');
                if(!(error instanceof GPUError)||!(error instanceof Type)||error.message!=='diagnostic')throw new Error('GPUError inheritance');
                if(Object.prototype.toString.call(error)!=='[object '+Type.name+']')throw new Error('GPUError tag');
                let rejected=false;try{Type('message')}catch(e){rejected=e instanceof TypeError}
                if(!rejected)throw new Error('error constructor callable');
            }
            let rejected=false;try{new GPUError()}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('base error constructible');
            const getter=Object.getOwnPropertyDescriptor(GPUError.prototype,'message').get;
            rejected=false;try{getter.call({})}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('GPUError getter accepted wrong receiver');
            const wrong=installedDevice.popErrorScope.call({});
            if(!(wrong instanceof Promise))throw new Error('pop receiver did not return promise');
            rejected=false;try{await wrong}catch(e){rejected=e instanceof TypeError}
            if(!rejected)throw new Error('pop receiver accepted');
            rejected=false;try{await installedDevice.popErrorScope()}catch(e){rejected=e instanceof DOMException&&e.name==='OperationError'}
            if(!rejected)throw new Error('empty scope accepted');
            installedDevice.pushErrorScope('validation');
            installedDevice.pushErrorScope('out-of-memory');
            installedDevice.createBuffer({size:16,usage:0});
            const clean=installedDevice.popErrorScope();
            const captured=installedDevice.popErrorScope();
            if(clean===captured)throw new Error('scope promises reused');
            if(await clean!==null)throw new Error('wrong scope captured validation');
            const error=await captured;
            if(!(error instanceof GPUValidationError)||!error.message.length)throw new Error('native validation missing');
            installedDevice.pushErrorScope('validation');
            if(await installedDevice.popErrorScope()!==null)throw new Error('clean scope failed');
            const node=document.createElement('div');node.id='scope-ready';document.body.appendChild(node);
        })();
    )JS","pop-error-scope"),"GPU popErrorScope request failed");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!document.find_by_id("scope-ready")&&std::chrono::steady_clock::now()<deadline) {
        require(runtime.pump_task(),"Error scope completion failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(document.find_by_id("scope-ready")!=nullptr,"Error scope promises did not settle correctly");
    require(runtime.execute(R"JS(
        if(installedDevice.pushErrorScope.length!==1)throw new Error('push scope arity');
        for(const filter of ['validation','out-of-memory','internal'])installedDevice.pushErrorScope(filter);
        for(const call of [
            ()=>installedDevice.pushErrorScope(),()=>installedDevice.pushErrorScope(undefined),
            ()=>installedDevice.pushErrorScope('Validation'),()=>installedDevice.pushErrorScope(null),
            ()=>installedDevice.pushErrorScope(Symbol()),()=>installedDevice.pushErrorScope.call({},'validation')
        ]){let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('scope filter accepted');}
        let conversions=0;
        installedDevice.pushErrorScope({toString(){++conversions;return 'validation';}});
        if(conversions!==1)throw new Error('scope conversion count');
        const sentinel={};let propagated=false;
        try{installedDevice.pushErrorScope({toString(){throw sentinel;}})}catch(e){propagated=e===sentinel}
        if(!propagated)throw new Error('scope conversion exception lost');
    )JS","push-error-scope"),"GPU pushErrorScope binding failed");

    require(runtime.execute(R"JS(
        (async()=>{
            const valid=installedDevice.createShaderModule({code:'@compute @workgroup_size(1) fn main() {}'});
            if(valid.getCompilationInfo.length!==0)throw new Error('compilation info arity');
            const first=valid.getCompilationInfo(),second=valid.getCompilationInfo();
            if(first===second)throw new Error('compilation promises reused');
            if((await first).messages.length||(await second).messages.length)throw new Error('valid shader diagnostics');
            let wrong=false;try{await valid.getCompilationInfo.call({})}catch(e){wrong=e instanceof TypeError}
            if(!wrong)throw new Error('compilation receiver accepted');
            const invalid=installedDevice.createShaderModule({code:'/* 😀 */ this is invalid WGSL'});
            const info=await invalid.getCompilationInfo();
            if(!Object.isFrozen(info.messages)||!info.messages.some(m=>m.type==='error'&&m.message.length&&m.offset>0))
                throw new Error('invalid shader diagnostics absent');
            const node=document.createElement('div');node.id='compilation-ready';document.body.appendChild(node);
        })();
    )JS","compilation-info"),"Compilation info request script failed");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!document.find_by_id("compilation-ready")&&std::chrono::steady_clock::now()<deadline) {
        require(runtime.pump_task(),"Compilation info completion failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(document.find_by_id("compilation-ready")!=nullptr,"Compilation info promises did not settle");
    require(runtime.execute("globalThis.pressureTexture=installedDevice.createTexture({size:[1,1],format:'rgba8unorm',usage:16});globalThis.livePressureView=pressureTexture.createView();","view-pressure-setup"),"View pressure setup failed");
    for(unsigned i=0;i<300;++i)
        require(runtime.execute("pressureTexture.createView();","view-pressure"),"Unreachable texture views exhausted release tickets");
    require(runtime.execute("livePressureView.label='still-live';if(livePressureView.label!=='still-live')throw new Error('live view collected');pressureTexture.destroy();delete globalThis.livePressureView;delete globalThis.pressureTexture;","view-pressure-cleanup"),"Pressure reclamation damaged a reachable view");

#if defined(__APPLE__)
    require(runtime.execute(R"JS(
        globalThis.domGPUCanvas=document.createElement('canvas');domGPUCanvas.width=4;domGPUCanvas.height=2;document.body.appendChild(domGPUCanvas);
        globalThis.domGPUContext=domGPUCanvas.getContext('webgpu');
        if(!domGPUContext||domGPUContext!==domGPUCanvas.getContext('webgpu')||domGPUContext.canvas!==domGPUCanvas||domGPUCanvas.getContext('2d')!==null)throw new Error('DOM GPU context ownership');
        const twoD=document.createElement('canvas');twoD.getContext('2d');if(twoD.getContext('webgpu')!==null)throw new Error('2D ownership lost');
        domGPUContext.configure({device:installedDevice,format:navigator.gpu.getPreferredCanvasFormat()});
        const initialTexture=domGPUContext.getCurrentTexture();if(initialTexture.width!==4||initialTexture.height!==2)throw new Error('DOM bitmap dimensions');
        const domEncoder=installedDevice.createCommandEncoder();const domPass=domEncoder.beginRenderPass({colorAttachments:[{view:initialTexture.createView(),loadOp:'clear',storeOp:'store',clearValue:[1,0,0,1]}]});
        domPass.end();installedDevice.queue.submit([domEncoder.finish()]);
        domGPUCanvas.width=8;
        const resizedTexture=domGPUContext.getCurrentTexture();if(resizedTexture===initialTexture||resizedTexture.width!==8)throw new Error('DOM GPU resize');
        domGPUContext.unconfigure();if(domGPUCanvas.getContext('2d')!==null)throw new Error('unconfigure released canvas mode');
    )JS","dom-gpu-canvas"),"DOM WebGPU canvas integration failed");
    require(runtime.execute(R"JS(
        domGPUCanvas.id='published-gpu-canvas';
        domGPUContext.configure({device:installedDevice,format:navigator.gpu.getPreferredCanvasFormat()});
        globalThis.publicationTexture=domGPUContext.getCurrentTexture();
        const publicationEncoder=installedDevice.createCommandEncoder();
        const publicationPass=publicationEncoder.beginRenderPass({colorAttachments:[{view:publicationTexture.createView(),loadOp:'clear',storeOp:'store',clearValue:[1,0,0,1]}]});
        publicationPass.end();installedDevice.queue.submit([publicationEncoder.finish()]);
    )JS","gpu-publication"),"GPU publication drawing failed");
    require((runtime.host_animation_frame_demand()&1U)!=0,"Canvas without RAF did not request a rendering opportunity");
    runtime.signal_animation_frame(100);
    require(runtime.has_pending_tasks()&&runtime.pump_task(),"Ordinary task pump did not service the GPU rendering opportunity");
    auto* published_node=document.find_by_id("published-gpu-canvas");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!published_node->canvas().gpu_image&&std::chrono::steady_clock::now()<deadline){
        require(runtime.pump_task(),"GPU publication completion failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto published=published_node->canvas().gpu_image;
    require(published!=nullptr,"Completed DOM GPU image did not publish");
    auto consumer=published->value.begin_consumer();require(consumer.has_value(),"Published DOM canvas consumer unavailable");
    auto surface=iosurface_canvas_images::resolve(*consumer).borrowed_handle();
    require(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,nullptr)==kIOReturnSuccess,"DOM canvas pixel lock failed");
    auto pixels=static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));auto stride=IOSurfaceGetBytesPerRow(surface);bool correct=pixels!=nullptr;
    if(pixels)for(size_t y=0;y<2;++y)for(size_t x=0;x<8;++x){auto pixel=pixels+y*stride+x*4;correct&=pixel[0]==0&&pixel[1]==0&&pixel[2]==255&&pixel[3]==255;}
    auto unlocked=IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,nullptr);consumer->complete();
    require(correct&&unlocked==kIOReturnSuccess,"Published DOM canvas pixel mismatch");
    require(runtime.host_animation_frame_demand()==0,"Published unchanged GPU canvas kept requesting frames");
    require(runtime.execute("if(domGPUContext.getCurrentTexture()===publicationTexture)throw new Error('frame texture not expired');","gpu-publication-expire"),"GPU publication expiration failed");
    require(published_node->canvas().backing.accepts_completed_content(published->value.describe().content_serial),
        "Acquiring the next frame invalidated completed canvas content");
    document.publish_gpu_canvas_image(*published_node,published);
    require(runtime.execute("domGPUContext.unconfigure();","gpu-unconfigure"),"GPU unconfigure failed");
    bool invalidated_image_rejected=false;
    try { document.publish_gpu_canvas_image(*published_node,published); }
    catch(const std::invalid_argument&) { invalidated_image_rejected=true; }
    require(invalidated_image_rejected,"Unconfigure accepted a previous completed image");
    require(!published_node->canvas().gpu_image,"Unconfigure retained the displayed GPU image");
    require(runtime.execute(R"JS(
        domGPUContext.configure({device:installedDevice,format:navigator.gpu.getPreferredCanvasFormat()});
        requestAnimationFrame(()=>{globalThis.rafCanvasTexture=domGPUContext.getCurrentTexture();});
        requestAnimationFrame(()=>{if(domGPUContext.getCurrentTexture()!==rafCanvasTexture)throw new Error('texture expired between RAF callbacks');});
    )JS","gpu-raf-group"),"GPU RAF setup failed");
    runtime.signal_animation_frame(116);
    require(runtime.pump_animation_frame_task()&&runtime.has_pending_animation_frame_task(),"First GPU RAF lost remaining rendering work");
    require(runtime.pump_animation_frame_task()&&!runtime.has_pending_animation_frame_task(),"GPU RAF group did not finish");
    require(runtime.execute("domGPUContext.unconfigure();","gpu-raf-cleanup"),"GPU RAF cleanup failed");
#endif
    require(runtime.load_url("https://graphics.test/webgpu-next"),"WebGPU navigation failed");
    require(runtime.execute("if('gpu' in navigator||'GPUBufferUsage' in globalThis||'GPUDeviceLostInfo' in globalThis||'GPUError' in globalThis||'GPUValidationError' in globalThis||'GPUOutOfMemoryError' in globalThis||'GPUInternalError' in globalThis)throw new Error('GPU policy survived navigation');","navigated-gpu"),"Navigation retained GPU exposure");
    require(runtime.install_webgpu(wake,true,webgpu_canvas_interop::none),"Navigated GPU reinstall failed");
    require(runtime.execute("globalThis.retiredGPU=navigator.gpu;","retain-gpu"),"GPU retention failed");
    runtime.shutdown_graphics();
    require(runtime.execute("let invalidated=false;try{retiredGPU.getPreferredCanvasFormat()}catch(e){invalidated=e instanceof TypeError}if(!invalidated)throw new Error('shutdown GPU callable');","shutdown-gpu"),"GPU shutdown failed to invalidate receiver");
    // Direct runtime destruction must enter the isolate before cancelling a
    // live realm; hosts are not required to call shutdown_graphics separately.
    webscene_native::native_document direct_document;
    webscene_native::v8_dom_runtime direct(direct_document,[]{return webscene_native::v8_dom_runtime::viewport_metrics{64,64,1,0};});
    require(direct.initialize()&&direct.install_webgpu(wake,true,webgpu_canvas_interop::none),"Direct disposal GPU setup failed");
    require(direct.execute("navigator.gpu.requestAdapter();","pending-disposal-gpu"),"Direct disposal request failed");
}
int main() {
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            test_device_loss_signal();
            test_compilation_info_snapshot();
            test_inline_canvas_intrinsic_layout();
            test_runtime_webgpu_document_policy();
            test_runtime_webgpu_installation();
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
            std::unique_ptr<v8_webgpu_device_request> device_request,failed_device_request,cancelled_device_request;
            bool cancelled_device_retired=false;
            bool device_failure_seen=false;
            auto buffer_test_device=std::make_shared<wgpu::Device>();
            resource_handle<wgpu::Adapter> discovered_adapter;
            std::shared_ptr<release_channel> releases;
            std::unique_ptr<v8_release_registry> wrappers;
            std::unique_ptr<v8_webgpu_buffers> gc_buffers,async_buffers;
            size_t binding_map_completions=0;
            std::unique_ptr<v8_webgpu_devices> device_registry;
            v8::Global<v8::Object> retired_device_probe;
            bool device_map_retired=false;
            bool buffer_validation_seen=false;
            resource_handle<dawn_device> gc_buffer_device;
            std::array<std::unique_ptr<v8_webgpu_map_request>,3> map_requests;
            size_t map_completions=0;
            std::array<uint64_t,14> test_operations;
            for(auto& operation:test_operations)operation=new_owner_token();
            auto& graphics=runtime.initialize_graphics(wake,[&](completion_record record) {
                if (device_registry && device_registry->complete(record)) {
                    auto* isolate=v8::Isolate::GetCurrent(); auto context=isolate->GetCurrentContext();
                    device_registry.reset();
                    {
                        v8::TryCatch caught(isolate);
                        auto object=retired_device_probe.Get(isolate);
                        auto method=object->Get(context,v8::String::NewFromUtf8Literal(isolate,"destroy")).ToLocalChecked().As<v8::Function>();
                        require(method->Call(context,object,0,nullptr).IsEmpty() && caught.HasCaught(),"Retired device wrapper retained native access");
                    }
                    retired_device_probe.Reset(); device_map_retired=true;
                    return;
                }
                if (record.operation==test_operations[13]) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    require(!cancelled_device_request->pending(),"Cancelled device request remained pending");
                    require(!cancelled_device_request->complete(isolate,isolate->GetCurrentContext(),record,[](wgpu::Device) -> v8::Local<v8::Value> {
                        throw std::runtime_error("Cancelled device wrapped after native callback");
                    }),"Cancelled device completion was consumed twice");
                    cancelled_device_request.reset(); cancelled_device_retired=true; return;
                }
                if (record.operation==test_operations[11]) {
                    auto* isolate=v8::Isolate::GetCurrent(); auto context=isolate->GetCurrentContext();
                    require(record.status==completion_status::failed,"Impossible device limit unexpectedly accepted");
                    require(failed_device_request->complete(isolate,context,record,[](wgpu::Device) -> v8::Local<v8::Value> {
                        throw std::runtime_error("Failed device was wrapped");
                    }),"Device failure promise did not settle");
                    auto promise=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"failedDevicePromise")).ToLocalChecked().As<v8::Promise>();
                    require(promise->State()==v8::Promise::kRejected,"Device failure did not reject");
                    auto name=promise->Result().As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"name")).ToLocalChecked();
                    require(name->StrictEquals(v8::String::NewFromUtf8Literal(isolate,"OperationError")),"Device failure has wrong exception type");
                    failed_device_request.reset();device_failure_seen=true;return;
                }
                if (record.operation==test_operations[10]) {
                    require(record.status==completion_status::success,"Invalid browser usage did not generate native validation");
                    buffer_validation_seen=true; return;
                }
                if (async_buffers && async_buffers->complete(record)) { ++binding_map_completions; return; }
                if (record.operation==test_operations[6] || record.operation==test_operations[7] || record.operation==test_operations[8]) {
                    auto& request=map_requests[std::find(test_operations.begin()+6,test_operations.begin()+9,record.operation)-(test_operations.begin()+6)];
                    require(request->complete(record,[&](const auto& buffer,auto) {
                        if (record.operation==test_operations[8]) throw std::bad_alloc();
                        require(record.operation==test_operations[6],"Canceled mapping attached native memory");
                        require(buffer.GetMapState()==wgpu::BufferMapState::Mapped && buffer.GetMappedRange(8,16),"Asynchronous subrange mapping failed");
                    }),"Map promise completion was not handled");
                    require(!request->pending(),"Map promise remained pending");
                    require(!request->complete(record,[](const auto&,auto) { throw std::runtime_error("Map attached twice"); }),"Duplicate map completion was accepted");
                    request.reset();
                    ++map_completions;
                    return;
                }
                if (record.operation==test_operations[4]) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    auto context=isolate->GetCurrentContext();
                    bool wrong_realm=false;
                    try { device_request->complete(isolate,v8::Context::New(isolate),record,[](wgpu::Device) -> v8::Local<v8::Value> {
                        throw std::runtime_error("Wrong realm wrapped device");
                    }); } catch (const std::logic_error&) { wrong_realm=true; }
                    require(wrong_realm && device_request->pending(),"Wrong realm consumed device completion");
                    require(device_request->complete(isolate,context,record,[&](wgpu::Device device) -> v8::Local<v8::Value> {
                        *buffer_test_device=std::move(device);
                        return v8::Object::New(isolate);
                    }),"Native device promise completion failed");
                    require(!device_request->pending(),"Native device promise remained pending");
                    require(!device_request->complete(isolate,context,record,[](wgpu::Device) -> v8::Local<v8::Value> {
                        throw std::runtime_error("Device was wrapped twice");
                    }),"Duplicate device completion accepted");
                    auto settled=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"deviceRequestPromise")).ToLocalChecked().As<v8::Promise>();
                    require(settled->State()==v8::Promise::kFulfilled && settled->Result()->IsObject(),"Device request did not fulfill with wrapper");
                    device_request.reset();
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
                    require(run("globalThis.mapExceptionSentinel={};globalThis.throwingMapException=class {constructor(){throw mapExceptionSentinel}};"),"Map exception fixture setup failed");
                    adapter_service->with_device(device_handle,[&](auto& device) {
                        for (size_t i=0;i<map_requests.size();++i) {
                            wgpu::BufferDescriptor descriptor{}; descriptor.size=64;
                            descriptor.usage=wgpu::BufferUsage::MapWrite|wgpu::BufferUsage::CopySrc;
                            auto buffer=device.native().CreateBuffer(&descriptor);
                            map_requests[i]=std::make_unique<v8_webgpu_map_request>(isolate,context,v8::Object::New(isolate),
                                context->Global()->Get(context,v8::String::NewFromUtf8(isolate,i==1 ? "throwingMapException" : "DOMException").ToLocalChecked()).ToLocalChecked().As<v8::Function>(),
                                std::move(buffer),device.owner(),test_operations[6+i]);
                            auto promise=map_requests[i]->start(adapter_service->dawn().completions(),wgpu::MapMode::Write,8,16).ToLocalChecked();
                            require(context->Global()->Set(context,v8::String::NewFromUtf8(isolate,i==0 ? "asyncMapProbe" : i==1 ? "cancelMapProbe" : "allocationMapProbe").ToLocalChecked(),promise).FromMaybe(false),"Map promise publication failed");
                        }
                    });
                    require(run("globalThis.asyncMapDone=false;globalThis.cancelMapDone=false;globalThis.allocationMapDone=false;allocationMapProbe.catch(e=>{if(!(e instanceof RangeError))throw e;allocationMapDone=true});asyncMapProbe.then(v=>{if(v!==undefined)throw new Error('map result');asyncMapDone=true});cancelMapProbe.catch(e=>{if(e!==mapExceptionSentinel)throw e;cancelMapDone=true});"),"Map promise observers failed");
                    require(map_requests[1]->cancel() && !map_requests[1]->pending() && !map_requests[1]->cancel(),"Map cancellation with throwing exception factory failed");
                    async_buffers=std::make_unique<v8_webgpu_buffers>(isolate,context,4,context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>());
                    adapter_service->with_device(device_handle,[&](auto& device) {
                        auto count=device.live_buffers();
                        {
                            v8::TryCatch caught(isolate);
                            auto invalid_size=v8::Script::Compile(context,v8::String::NewFromUtf8Literal(isolate,"({size:3,usage:8,mappedAtCreation:true})")).ToLocalChecked()->Run(context).ToLocalChecked();
                            require(async_buffers->create(context,*adapter_service,device_handle,invalid_size).IsEmpty()
                                && caught.HasCaught() && device.live_buffers()==count,"Misaligned mapped creation allocated a buffer");
                            require(caught.Exception().As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"name")).ToLocalChecked()->StrictEquals(v8::String::NewFromUtf8Literal(isolate,"RangeError")),"Mapped size error was not RangeError");
                        }
                        device.native().PushErrorScope(wgpu::ErrorFilter::Validation);
                        auto invalid_usage=v8::Script::Compile(context,v8::String::NewFromUtf8Literal(isolate,"({size:64,usage:1024,mappedAtCreation:true,label:'invalid usage'})")).ToLocalChecked()->Run(context).ToLocalChecked();
                        auto error_buffer=async_buffers->create(context,*adapter_service,device_handle,invalid_usage).ToLocalChecked();
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"errorBufferProbe"),error_buffer).FromMaybe(false),"Error buffer publication failed");
                        require(run("if(errorBufferProbe.usage!==1024||errorBufferProbe.size!==64||errorBufferProbe.label!=='invalid usage'||errorBufferProbe.getMappedRange().byteLength!==64)throw new Error('invalid buffer metadata/mapping');errorBufferProbe.unmap();delete globalThis.errorBufferProbe;"),"Browser error buffer behavior failed");
                        auto error_mailbox=adapter_service->dawn().completions();
                        auto error_ticket=error_mailbox->reserve(test_operations[10],device.owner()).value();
                        device.native().PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                            [error_mailbox,error_ticket](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView) {
                                error_mailbox->publish(error_ticket,status==wgpu::PopErrorScopeStatus::Success && type==wgpu::ErrorType::Validation
                                    ? completion_status::success : completion_status::failed);
                            });
                        for (const char* name:{"bindingBuffer","bindingCancelBuffer","bindingReadBuffer"}) {
                            const bool read=std::string_view(name)=="bindingReadBuffer";
                            v8::Local<v8::Object> object;
                            if (read) {
                                wgpu::BufferDescriptor descriptor{}; descriptor.size=64;
                                descriptor.usage=wgpu::BufferUsage::MapRead|wgpu::BufferUsage::CopyDst;
                                auto handle=device.create_buffer(descriptor);
                                device.with_buffer(handle,[&](const auto& buffer) {
                                    std::array<uint32_t,16> data; data.fill(0x11223344);
                                    device.native().GetQueue().WriteBuffer(buffer,0,data.data(),sizeof(data));
                                });
                                object=async_buffers->wrap(context,*adapter_service,device_handle,handle).ToLocalChecked();
                            } else {
                                auto input=v8::Script::Compile(context,v8::String::NewFromUtf8Literal(isolate,"({size:64,usage:6,label:'created from JS'})")).ToLocalChecked()->Run(context).ToLocalChecked();
                                object=async_buffers->create(context,*adapter_service,device_handle,input).ToLocalChecked();
                            }
                            require(context->Global()->Set(context,v8::String::NewFromUtf8(isolate,name).ToLocalChecked(),object).FromMaybe(false),"Async binding buffer publication failed");
                        }
                    });
                    {
                        auto saturated_registry=std::make_unique<v8_webgpu_buffers>(isolate,context,1,context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>());
                        auto input=v8::Script::Compile(context,v8::String::NewFromUtf8Literal(isolate,"({size:4,usage:8})")).ToLocalChecked()->Run(context).ToLocalChecked();
                        size_t before=0; adapter_service->with_device(device_handle,[&](auto& device) { before=device.live_buffers(); });
                        v8::TryCatch caught(isolate);
                        require(saturated_registry->create(context,*adapter_service,device_handle,input).IsEmpty() && caught.HasCaught(),"Release saturation silently returned an empty wrapper");
                        adapter_service->with_device(device_handle,[&](auto& device) { require(device.live_buffers()==before,"Wrapper registration failure leaked a native buffer"); });
                    }
                    require(run(R"JS(
                        globalThis.bindingMapDone=false;globalThis.bindingCancelDone=false;globalThis.bindingRemapDone=false;globalThis.bindingConversionDone=false;globalThis.bindingReadDone=false;
                        {
                            if(bindingBuffer.label!=='created from JS'||bindingBuffer.usage!==6)throw new Error('JS-created buffer metadata');if(bindingBuffer.mapAsync.length!==1)throw new Error('mapAsync length');
                            let conversion=bindingBuffer.mapAsync(1n);
                            if(!(conversion instanceof Promise))throw new Error('mapAsync conversion threw synchronously');
                            conversion.catch(e=>{if(!(e instanceof TypeError))throw e;bindingConversionDone=true});
                            let promise=bindingBuffer.mapAsync(2,8,16);
                            if(!(promise instanceof Promise)||bindingBuffer.mapState!=='pending')throw new Error('pending map state');
                            promise.then(()=>{
                                if(bindingBuffer.mapState!=='mapped')throw new Error('completed map state');
                                let view=bindingBuffer.getMappedRange(8,16);new Uint32Array(view)[0]=42;
                                bindingBuffer.unmap();
                                if(view.byteLength!==0||bindingBuffer.mapState!=='unmapped')throw new Error('async view detach');
                                bindingMapDone=true;delete globalThis.bindingBuffer;
                            });
                            bindingReadBuffer.mapAsync(1,8,16).then(()=>{
                                let words=new Uint32Array(bindingReadBuffer.getMappedRange(8,16));
                                if(words[0]!==0x11223344)throw new Error('read mapping data');
                                words[0]=0xffffffff;bindingReadBuffer.unmap();
                                return bindingReadBuffer.mapAsync(1,8,16);
                            }).then(()=>{
                                if(new Uint32Array(bindingReadBuffer.getMappedRange(8,16))[0]!==0x11223344)throw new Error('READ writes reached GPU buffer');
                                bindingReadBuffer.unmap();bindingReadDone=true;delete globalThis.bindingReadBuffer;
                            });
                            bindingCancelBuffer.mapAsync(2,0,16).catch(e=>{
                                if(!(e instanceof DOMException)||e.name!=='AbortError')throw e;bindingCancelDone=true;
                            });
                            bindingCancelBuffer.unmap();
                            bindingCancelBuffer.mapAsync(2,16,16).then(()=>{
                                if(bindingCancelBuffer.getMappedRange(16,16).byteLength!==16)throw new Error('remap range');
                                bindingCancelBuffer.unmap();bindingRemapDone=true;delete globalThis.bindingCancelBuffer;
                            });
                        }
                    )JS"),"JavaScript mapAsync dispatch failed");
                    graphics_service adapter_fixture(wake,2);
                    auto fresh_adapter=std::make_shared<wgpu::Adapter>();
                    auto fixture_mailbox=adapter_fixture.dawn().completions();
                    auto adapter_ticket=fixture_mailbox->reserve(new_owner_token(),{adapter_fixture.engine_identity(),new_owner_token(),0}).value();
                    auto adapter_options=make_dawn_adapter_options(webgpu_adapter_options{},wgpu::BackendType::Undefined).value();
                    adapter_fixture.dawn().instance().RequestAdapter(&adapter_options,wgpu::CallbackMode::AllowSpontaneous,
                        [fresh_adapter,fixture_mailbox,adapter_ticket](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView) {
                            *fresh_adapter=std::move(adapter);
                            fixture_mailbox->publish(adapter_ticket,status==wgpu::RequestAdapterStatus::Success?completion_status::success:completion_status::failed);
                        });
                    bool fresh_adapter_ready=false;
                    auto fresh_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while(!fresh_adapter_ready && std::chrono::steady_clock::now()<fresh_deadline) {
                        adapter_fixture.pump([&](auto completion) { require(completion.status==completion_status::success,"Fresh adapter request failed");fresh_adapter_ready=true; });
                        if(!fresh_adapter_ready)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(fresh_adapter_ready && *fresh_adapter,"Fresh adapter unavailable");
                    auto adapter_handle=adapter_fixture.adopt_adapter(std::move(*fresh_adapter));
                    auto exception_constructor=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>();
                    auto adapter_devices=std::make_unique<v8_webgpu_devices>(isolate,context,exception_constructor,2,4);
                    auto adapter_registry=std::make_unique<v8_webgpu_adapters>(isolate,context,*adapter_devices,exception_constructor,1);
                    auto adapter_object=adapter_registry->wrap(context,adapter_fixture,adapter_handle).ToLocalChecked();
                    bool duplicate_adapter=false,foreign_realm=false;
                    try { adapter_registry->wrap(context,adapter_fixture,adapter_handle); } catch (const std::invalid_argument&) { duplicate_adapter=true; }
                    try { adapter_registry->wrap(v8::Context::New(isolate),adapter_fixture,adapter_handle); } catch (const std::logic_error&) { foreign_realm=true; }
                    require(duplicate_adapter && foreign_realm,"Adapter identity or realm ownership duplicated");
                    auto adapter_features=adapter_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"features")).ToLocalChecked();
                    require(adapter_features->StrictEquals(adapter_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"features")).ToLocalChecked()),"Adapter feature identity changed");
                    auto feature_has=adapter_features.As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"has")).ToLocalChecked().As<v8::Function>();
                    adapter_fixture.with_adapter(adapter_handle,[&](const auto& native) {
                        verify_v8_limits(isolate,context,adapter_object,native);
                        auto info=read_webgpu_adapter_info(native);
                        require(webgpu_adapter_is_fallback(wgpu::BackendType::Vulkan,0x1ae0,0xc0de)
                            && !webgpu_adapter_is_fallback(wgpu::BackendType::Vulkan,0x1ae0,0)
                            && !webgpu_adapter_is_fallback(wgpu::BackendType::Metal,0x1ae0,0xc0de),"Fallback classification differs from pinned Dawn");
                        auto exposed_info=adapter_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"info")).ToLocalChecked().As<v8::Object>();
                        for(const auto& field:std::array<std::pair<const char*,const std::string*>,4>{{{"vendor",&info.vendor},{"architecture",&info.architecture},{"device",&info.device},{"description",&info.description}}}) {
                            auto value=exposed_info->Get(context,v8::String::NewFromUtf8(isolate,field.first).ToLocalChecked()).ToLocalChecked();
                            v8::String::Utf8Value text(isolate,value);
                            require(*text && std::string(*text,text.length())==*field.second,"Adapter info string differs from native snapshot");
                        }
                        wgpu::AdapterInfo native_info{};require(native.GetInfo(&native_info)==wgpu::Status::Success,"Adapter info query failed");
                        require(info.description==webgpu_info_string(native_info.description) && !info.is_fallback_adapter,"Adapter info snapshot incorrect");
                        require(info.subgroup_min_size==(native.HasFeature(wgpu::FeatureName::Subgroups)?native_info.subgroupMinSize:4)
                            && info.subgroup_max_size==(native.HasFeature(wgpu::FeatureName::Subgroups)?native_info.subgroupMaxSize:128),"Adapter subgroup information incorrect");
                        require(webgpu_info_identifier(wgpu::StringView("vendor-123"))=="vendor-123"
                            && webgpu_info_identifier(wgpu::StringView("Vendor Name")).empty()
                            && webgpu_info_identifier(wgpu::StringView("a--b")).empty(),"Adapter identifier normalization rules incorrect");
                        for (const auto& feature:webgpu_feature_names) {
                            v8::Local<v8::Value> name=v8::String::NewFromUtf8(isolate,feature.name.data(),v8::NewStringType::kNormal,static_cast<int>(feature.name.size())).ToLocalChecked();
                            require(feature_has->Call(context,adapter_features,1,&name).ToLocalChecked()->BooleanValue(isolate)==native.HasFeature(feature.native),"Adapter capability snapshot differs from Dawn");
                        }
                    });
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"adapterWrapperProbe"),adapter_object).FromMaybe(false),"Adapter wrapper publication failed");
                    require(run("globalThis.adapterDevicePromise=adapterWrapperProbe.requestDevice({label:'via adapter',defaultQueue:{label:'JS queue'}});"),"Adapter requestDevice call failed");
                    auto requested_device_promise=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"adapterDevicePromise")).ToLocalChecked().As<v8::Promise>();
                    auto request_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while (requested_device_promise->State()==v8::Promise::kPending && std::chrono::steady_clock::now()<request_deadline) {
                        adapter_fixture.pump([&](auto completion) { require(adapter_registry->complete(completion),"Adapter request completion not routed"); });
                        if (requested_device_promise->State()==v8::Promise::kPending) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(requested_device_promise->State()==v8::Promise::kFulfilled,"Adapter request did not produce a native device wrapper");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"adapterDeviceProbe"),requested_device_promise->Result()).FromMaybe(false),"Adapter device publication failed");
                    test_v8_webgpu_canvas_configuration(isolate,context);
                    size_t canvas_acquisitions=0,canvas_retirements=0;
                    webgpu_canvas_host canvas_host;
                    canvas_host.validate=[](const auto& config){if(config.color_space!="srgb")throw std::invalid_argument("Diagnostic canvas supports sRGB");};
                    canvas_host.acquire=[&](const auto& config,const auto& descriptor){++canvas_acquisitions;wgpu::Texture texture;descriptor.with_native([&](const auto& native){texture=config.device.CreateTexture(&native);});return texture;};
                    canvas_host.retire=[&](const auto& texture,bool){++canvas_retirements;texture.Destroy();};
                    auto canvas_context=std::make_unique<v8_webgpu_canvas_context>(isolate,context,v8::Object::New(isolate),context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),4,2,std::move(canvas_host));
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"canvasContextProbe"),canvas_context->object()).FromMaybe(false),"Canvas context publication failed");
                    require(run(R"JS(
                        if(canvasContextProbe.getConfiguration()!==null)throw new Error('initial canvas config');
                        let unconfigured=false;try{canvasContextProbe.getCurrentTexture()}catch(e){unconfigured=e instanceof DOMException&&e.name==='InvalidStateError'}if(!unconfigured)throw new Error('unconfigured canvas texture');
                        canvasContextProbe.configure({device:adapterDeviceProbe,format:'rgba8unorm',usage:17});
                        const snapshot=canvasContextProbe.getConfiguration();snapshot.viewFormats.push('invalid');snapshot.toneMapping.mode='invalid';
                        if(canvasContextProbe.getConfiguration().viewFormats.length!==0||canvasContextProbe.getConfiguration().toneMapping.mode!=='standard')throw new Error('canvas snapshot mutation');
                        if(canvasContextProbe.canvas!==canvasContextProbe.canvas)throw new Error('canvas identity');
                    )JS"),"Canvas configuration lifecycle failed");

                    {
                        auto device_object=requested_device_promise->Result().As<v8::Object>();auto device=v8_webgpu_devices::native_reference(device_object);
                        webgpu_texture_descriptor metadata;metadata.size={4,2,1};metadata.format=wgpu::TextureFormat::RGBA8Unorm;metadata.usage=16;metadata.label="imported canvas";
                        wgpu::Texture native;metadata.with_native([&](const auto& descriptor){native=device.CreateTexture(&descriptor);});
                        auto mismatch=metadata;mismatch.size.width=8;bool rejected=false;
                        try{v8_webgpu_devices::adopt_canvas_texture(context,device_object,device,native,mismatch);}catch(const std::invalid_argument&){rejected=true;}
                        require(rejected,"Canvas adoption accepted mismatched metadata");
                        auto wrapper=v8_webgpu_devices::adopt_canvas_texture(context,device_object,device,native,metadata).ToLocalChecked();
                        require(v8_webgpu_textures::native_reference(wrapper).Get()==native.Get(),"Canvas adoption replaced native texture");
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"importedCanvasTexture"),wrapper).FromMaybe(false),"Imported texture publication failed");
                        require(run("if(importedCanvasTexture.width!==4||importedCanvasTexture.label!=='imported canvas')throw new Error('imported metadata');const importedView=importedCanvasTexture.createView();if(Object.prototype.toString.call(importedView)!=='[object GPUTextureView]')throw new Error('imported view');delete globalThis.importedCanvasTexture;"),"Imported canvas texture JavaScript access failed");
                    }

                    require(run(R"JS(
                        if(adapterWrapperProbe.requestDevice.length!==0)throw new Error('requestDevice arity');
                        const ai=adapterWrapperProbe.info,di=adapterDeviceProbe.adapterInfo;
                        if(ai!==adapterWrapperProbe.info||di!==adapterDeviceProbe.adapterInfo||Object.prototype.toString.call(ai)!=='[object GPUAdapterInfo]')throw new Error('adapter info identity');
                        for(const key of ['vendor','architecture','device','description','subgroupMinSize','subgroupMaxSize','isFallbackAdapter'])if(ai[key]!==di[key])throw new Error('device adapter info mismatch');
                        const savedVendor=ai.vendor;let infoReadOnly=false;try{(()=>{'use strict';ai.vendor='changed'})()}catch(e){infoReadOnly=e instanceof TypeError}
                        if(!infoReadOnly||ai.vendor!==savedVendor||'backendType' in ai||'vendorID' in ai)throw new Error('adapter info mutation or private fields');
                        let infoBrand=false;try{Object.getOwnPropertyDescriptor(Object.getPrototypeOf(ai),'vendor').get.call({})}catch(e){infoBrand=e instanceof TypeError}if(!infoBrand)throw new Error('adapter info receiver');
                        globalThis.retainedAdapterInfo=ai;
                        if(adapterDeviceProbe.label!=='via adapter')throw new Error('requested device label');
                        if(adapterDeviceProbe.queue!==adapterDeviceProbe.queue||adapterDeviceProbe.queue.label!=='JS queue'||Object.prototype.toString.call(adapterDeviceProbe.queue)!=='[object GPUQueue]')throw new Error('device queue identity');
                        adapterDeviceProbe.queue.label='updated queue';if(adapterDeviceProbe.queue.label!=='updated queue')throw new Error('queue label');
                        {const encoder=adapterDeviceProbe.createCommandEncoder({label:'JS encoder'});
                         if(Object.prototype.toString.call(encoder)!=='[object GPUCommandEncoder]'||encoder.label!=='JS encoder'||encoder.finish.length!==0)throw new Error('encoder wrapper');
                         const sentinel={};let propagated=false;try{encoder.finish({get label(){throw sentinel}})}catch(e){propagated=e===sentinel}if(!propagated)throw new Error('finish exception');
                         const command=encoder.finish({label:'JS commands'});
                         if(Object.prototype.toString.call(command)!=='[object GPUCommandBuffer]'||command.label!=='JS commands')throw new Error('command buffer wrapper');
                         command.label='updated commands';if(command.label!=='updated commands')throw new Error('command label');
                         for(const call of [()=>adapterDeviceProbe.createCommandEncoder(1),()=>encoder.finish.call({}),()=>encoder.finish(1)]) {
                             let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid encoder call');
                         }
                         const empty=adapterDeviceProbe.createCommandEncoder().finish();if(empty.label!=='')throw new Error('encoder defaults');}

                        {const texture=canvasContextProbe.getCurrentTexture();texture.label='JS texture';
                         if(texture!==canvasContextProbe.getCurrentTexture())throw new Error('current texture identity');
                         if(texture.width!==4||texture.height!==2||texture.depthOrArrayLayers!==1||texture.mipLevelCount!==1||texture.sampleCount!==1||texture.dimension!=='2d'||texture.format!=='rgba8unorm'||texture.usage!==17)throw new Error('texture metadata');
                         if(Object.prototype.toString.call(texture)!=='[object GPUTexture]'||texture.label!=='JS texture')throw new Error('texture wrapper');
                         const view=texture.createView({label:'JS view'});
                         if(Object.prototype.toString.call(view)!=='[object GPUTextureView]'||view.label!=='JS view')throw new Error('view wrapper');
                         view.label='updated view';texture.label='updated texture';
                         let readonly=false;try{(()=>{'use strict';texture.width=8})()}catch(e){readonly=e instanceof TypeError}if(!readonly||texture.width!==4)throw new Error('texture readonly metadata');
                         for(const call of [()=>adapterDeviceProbe.createTexture(),()=>adapterDeviceProbe.createTexture({size:[],format:'rgba8unorm',usage:16}),()=>texture.createView.call({}),()=>texture.destroy.call({})]) {
                             let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid texture call');
                         }
                         const shader=adapterDeviceProbe.createShaderModule({code:'@vertex fn vs(@location(0) p:vec2f)->@builtin(position) vec4f {return vec4f(p,0,1);} @group(0) @binding(0) var<uniform> color:vec4f; @fragment fn fs()->@location(0) vec4f {return color;}'});
                         const vertices=adapterDeviceProbe.createBuffer({size:32,usage:32,mappedAtCreation:true});
                         new Float32Array(vertices.getMappedRange()).set([-1,-1,3,-1,-1,3],2);vertices.unmap();
                         const uniform=adapterDeviceProbe.createBuffer({size:512,usage:64,mappedAtCreation:true});
                         new Float32Array(uniform.getMappedRange()).set([1,0,0,1],64);uniform.unmap();
                         const bindingLayout=adapterDeviceProbe.createBindGroupLayout({entries:[{binding:0,visibility:2,buffer:{hasDynamicOffset:true}}]});
                         const layout=adapterDeviceProbe.createPipelineLayout({bindGroupLayouts:[bindingLayout]});
                         const group=adapterDeviceProbe.createBindGroup({layout:bindingLayout,entries:[{binding:0,resource:{buffer:uniform,size:16}}]});
                         const pipeline=adapterDeviceProbe.createRenderPipeline({layout,vertex:{module:shader,entryPoint:'vs',buffers:[{arrayStride:8,attributes:[{format:'float32x2',offset:0,shaderLocation:0}]}]},fragment:{module:shader,entryPoint:'fs',targets:[{format:'rgba8unorm'}]}});
                         const drawEncoder=adapterDeviceProbe.createCommandEncoder();
                         let shapeRejected=false;try{drawEncoder.beginRenderPass({colorAttachments:[{view,loadOp:'clear',storeOp:'store',clearValue:[0,0]}]})}catch(e){shapeRejected=e instanceof TypeError}if(!shapeRejected)throw new Error('clear color shape');
                         const pass=drawEncoder.beginRenderPass({label:'triangle pass',colorAttachments:[{view,loadOp:'clear',storeOp:'store',clearValue:[0,0,0,1]}]});
                         if(Object.prototype.toString.call(pass)!=='[object GPURenderPassEncoder]'||pass.label!=='triangle pass')throw new Error('render pass wrapper');
                         for(const call of [()=>pass.draw(),()=>pass.draw(-1),()=>pass.draw(1n),()=>pass.setPipeline({}),()=>pass.end.call({})]) {
                             let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid pass call');
                         }
                         if(pass.setBindGroup.length!==2)throw new Error('setBindGroup arity');
                         for(const call of [()=>pass.setBindGroup(),()=>pass.setBindGroup(-1,group),()=>pass.setBindGroup(0,{}),
                             ()=>pass.setBindGroup(0,group,[-1]),()=>pass.setBindGroup(0,group,new Uint32Array(1),0),
                             ()=>pass.setBindGroup(0,group,[],0,1)]) {
                             let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid bind group call');
                         }
                         for(const [start,count] of [[2,0],[0,2]]) {
                             let rejected=false;try{pass.setBindGroup(0,group,new Uint32Array(1),start,count)}catch(e){rejected=e instanceof RangeError}
                             if(!rejected)throw new Error('dynamic offset bounds accepted');
                         }
                         const bindingSentinel={};let bindingException=false;
                         try{pass.setBindGroup(0,group,{[Symbol.iterator](){throw bindingSentinel}})}catch(e){bindingException=e===bindingSentinel}
                         if(!bindingException)throw new Error('binding iterator exception lost');
                         if(pass.setVertexBuffer.length!==2)throw new Error('setVertexBuffer arity');
                         for(const call of [()=>pass.setVertexBuffer(),()=>pass.setVertexBuffer(-1,vertices),()=>pass.setVertexBuffer(0,{}),
                             ()=>pass.setVertexBuffer(0,vertices,-1),()=>pass.setVertexBuffer(0,vertices,0,1n)]) {
                             let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid vertex buffer call');
                         }
                         pass.setVertexBuffer(0,null);
                         pass.setVertexBuffer(0,vertices,8,24);
                         pass.setPipeline(pipeline);
                         pass.setBindGroup(0,null);
                         pass.setBindGroup(0,group,new Set([256]));pass.draw(3);
                         const dynamic=new Uint32Array([77,256,88]);
                         pass.setBindGroup(0,group,dynamic.subarray(1),0,1);pass.draw(3);
                         const sharedDynamic=new Uint32Array(new SharedArrayBuffer(8));sharedDynamic[1]=256;
                         pass.setBindGroup(0,group,sharedDynamic,1,1);pass.draw(3);pass.end();
                         const drawCommands=drawEncoder.finish({label:'triangle commands'});
                         if(drawCommands.label!=='triangle commands')throw new Error('recorded draw commands');
                         for(const call of [()=>adapterDeviceProbe.queue.submit(),()=>adapterDeviceProbe.queue.submit([{}]),()=>adapterDeviceProbe.queue.submit.call({},[])]) {
                             let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid queue call');
                         }
                         const queueSentinel={};let queueException=false;
                         try{adapterDeviceProbe.queue.submit({[Symbol.iterator](){throw queueSentinel}})}catch(e){queueException=e===queueSentinel}
                         if(!queueException)throw new Error('queue iterator exception');
                         adapterDeviceProbe.queue.submit(new Set([drawCommands]));
                         globalThis.triangleTextureProbe=texture;

                         // Keep the submitted texture alive for diagnostic pixel verification.
                         if(texture.width!==4||texture.label!=='updated texture'||view.label!=='updated view')throw new Error('destroyed texture metadata');}

                        {let b=adapterDeviceProbe.createBuffer({size:16,usage:8,mappedAtCreation:true});
                         let range=b.getMappedRange();new Uint32Array(range)[0]=123;b.unmap();
                         if(range.byteLength!==0 || b.size!==16)throw new Error('adapter device buffer');b.destroy();}
                        globalThis.consumedAdapterPromise=adapterWrapperProbe.requestDevice();
                        consumedAdapterPromise.catch(()=>{});
                        globalThis.badAdapterReceiverPromise=adapterWrapperProbe.requestDevice.call({});
                        badAdapterReceiverPromise.catch(()=>{});
                    )JS"),"Adapter device resource operations failed");
                    {
                        auto native_device=v8_webgpu_devices::native_reference(requested_device_promise->Result());
                        auto native_texture=v8_webgpu_textures::native_reference(context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"triangleTextureProbe")).ToLocalChecked());
                        wgpu::BufferDescriptor descriptor{};descriptor.size=512;descriptor.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead;
                        auto readback=native_device.CreateBuffer(&descriptor);auto copy=native_device.CreateCommandEncoder();
                        wgpu::TexelCopyTextureInfo source{};source.texture=native_texture;
                        wgpu::TexelCopyBufferInfo destination{};destination.buffer=readback;destination.layout.bytesPerRow=256;destination.layout.rowsPerImage=2;
                        wgpu::Extent3D extent{4,2,1};copy.CopyTextureToBuffer(&source,&destination,&extent);
                        auto commands=copy.Finish();native_device.GetQueue().Submit(1,&commands);
                        auto map_status=std::make_shared<std::atomic<int>>(0);
                        readback.MapAsync(wgpu::MapMode::Read,0,512,wgpu::CallbackMode::AllowSpontaneous,[map_status](wgpu::MapAsyncStatus status,wgpu::StringView){map_status->store(status==wgpu::MapAsyncStatus::Success?1:-1);});
                        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                        while(map_status->load()==0&&std::chrono::steady_clock::now()<deadline){adapter_fixture.pump([](auto){});std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                        require(map_status->load()==1,"Triangle diagnostic readback did not complete");
                        const auto* pixels=static_cast<const uint8_t*>(readback.GetConstMappedRange(0,512));require(pixels!=nullptr,"Triangle readback mapping missing");
                        for(size_t y=0;y<2;++y)for(size_t x=0;x<4;++x){const auto* pixel=pixels+y*256+x*4;require(pixel[0]==255&&pixel[1]==0&&pixel[2]==0&&pixel[3]==255,"JavaScript triangle pixel mismatch");}
                        readback.Unmap();readback.Destroy();
                        require(run("triangleTextureProbe.destroy();triangleTextureProbe.destroy();if(triangleTextureProbe.width!==4)throw new Error('destroyed texture metadata');delete globalThis.triangleTextureProbe;"),"Triangle texture cleanup failed");
                    }
                    require(canvas_acquisitions==1&&canvas_retirements==0,"Canvas acquired more than once per frame");
                    canvas_context->end_frame(true);
                    require(canvas_retirements==1,"Canvas frame did not retire");
                    canvas_context->resize(8,3);
                    require(run("const resized=canvasContextProbe.getCurrentTexture();if(resized.width!==8||resized.height!==3)throw new Error('canvas resize');canvasContextProbe.unconfigure();if(canvasContextProbe.getConfiguration()!==null)throw new Error('canvas unconfigure');"),"Canvas resize/unconfigure failed");
                    require(canvas_acquisitions==2&&canvas_retirements==2,"Canvas texture replacement lifetime failed");
                    canvas_context.reset();
                    require(run("let releasedCanvas=false;try{canvasContextProbe.getCurrentTexture()}catch(e){releasedCanvas=e instanceof TypeError}if(!releasedCanvas)throw new Error('released canvas receiver');delete globalThis.canvasContextProbe;"),"Released canvas wrapper remained callable");
#if defined(__APPLE__)
                    test_v8_iosurface_canvas_host(isolate,context,adapter_fixture,run);
                    adapter_fixture.drain_commands();
#endif
                    for(const char* name:{"consumedAdapterPromise","badAdapterReceiverPromise"}) {
                        auto rejected=context->Global()->Get(context,v8::String::NewFromUtf8(isolate,name).ToLocalChecked()).ToLocalChecked().As<v8::Promise>();
                        require(rejected->State()==v8::Promise::kRejected,"Invalid adapter request did not reject");
                        auto error_name=rejected->Result().As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"name")).ToLocalChecked();
                        const char* expected=std::string_view(name)=="consumedAdapterPromise"?"OperationError":"TypeError";
                        require(error_name->StrictEquals(v8::String::NewFromUtf8(isolate,expected).ToLocalChecked()),"Adapter rejection type incorrect");
                    }
                    // Dispose the registry before delivering successful native
                    // device completion. Use a fresh, unconsumed adapter.
                    auto cancel_adapter=std::make_shared<wgpu::Adapter>();
                    const auto cancel_adapter_operation=new_owner_token();
                    auto cancel_adapter_ticket=fixture_mailbox->reserve(cancel_adapter_operation,{adapter_fixture.engine_identity(),new_owner_token(),0}).value();
                    adapter_fixture.dawn().instance().RequestAdapter(&adapter_options,wgpu::CallbackMode::AllowSpontaneous,
                        [cancel_adapter,fixture_mailbox,cancel_adapter_ticket](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView) {
                            *cancel_adapter=std::move(adapter);
                            fixture_mailbox->publish(cancel_adapter_ticket,status==wgpu::RequestAdapterStatus::Success?completion_status::success:completion_status::failed);
                        });
                    bool cancel_adapter_ready=false;
                    auto cancel_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while(!cancel_adapter_ready && std::chrono::steady_clock::now()<cancel_deadline) {
                        adapter_fixture.pump([&](auto completion) { if(completion.operation!=cancel_adapter_operation)return;require(completion.status==completion_status::success,"Cancellation fixture adapter failed");cancel_adapter_ready=true; });
                        if(!cancel_adapter_ready)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(cancel_adapter_ready && *cancel_adapter,"Cancellation fixture adapter unavailable");
                    auto cancel_handle=adapter_fixture.adopt_adapter(std::move(*cancel_adapter));
                    auto cancel_registry=std::make_unique<v8_webgpu_adapters>(isolate,context,*adapter_devices,exception_constructor,1);
                    auto cancel_object=cancel_registry->wrap(context,adapter_fixture,cancel_handle).ToLocalChecked();
                    auto request_method=cancel_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"requestDevice")).ToLocalChecked().As<v8::Function>();
                    const auto lifetime_completions=fixture_mailbox->metrics().occupied;
                    auto cancel_promise=request_method->Call(context,cancel_object,0,nullptr).ToLocalChecked().As<v8::Promise>();
                    require(cancel_promise->State()==v8::Promise::kPending,"Teardown fixture did not admit native request");
                    cancel_promise->MarkAsHandled();
                    cancel_registry.reset();
                    require(cancel_promise->State()==v8::Promise::kRejected,"Registry teardown left pending device promise");
                    auto cancelled_reason=cancel_promise->Result();
                    size_t successful_retirements=0;
                    cancel_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while(fixture_mailbox->metrics().occupied>lifetime_completions && std::chrono::steady_clock::now()<cancel_deadline) {
                        adapter_fixture.pump([&](auto completion) {
                            require(completion.status==completion_status::success,"Native creation did not succeed in teardown race");++successful_retirements;
                        });
                        if(fixture_mailbox->metrics().occupied>lifetime_completions)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(successful_retirements==1 && fixture_mailbox->metrics().occupied==lifetime_completions
                        && adapter_fixture.live_devices()==1 && adapter_fixture.live_adapters()==1,
                        "Successful teardown race leaked completion or adopted an orphan device");
                    require(cancel_promise->State()==v8::Promise::kRejected && cancel_promise->Result()->StrictEquals(cancelled_reason),
                        "Late native success replaced cancellation rejection");
                    adapter_registry.reset();
                    { v8::TryCatch caught(isolate);
                        require(adapter_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"features")).IsEmpty() && caught.HasCaught(),"Retired adapter retained native access");
                    }
                    require(adapter_features.As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"size")).ToLocalChecked()->IsUint32(),"Retained adapter features lost after disposal");
                    adapter_devices.reset();
                    require(run("if(typeof retainedAdapterInfo.vendor!=='string')throw new Error('retained adapter info');delete globalThis.retainedAdapterInfo;"),"Adapter info did not survive teardown");
                    require(run("delete globalThis.adapterWrapperProbe;delete globalThis.adapterDeviceProbe;delete globalThis.adapterDevicePromise;delete globalThis.consumedAdapterPromise;delete globalThis.badAdapterReceiverPromise;"),"Adapter probe cleanup failed");
                    require(adapter_fixture.live_adapters()==1,"Adapter disposal bypassed deferred release");
                    adapter_fixture.pump([](completion_record) {});
                    require(adapter_fixture.live_adapters()==0 && adapter_fixture.live_devices()==0,"Adapter/device deferred release leaked native handles");
                    auto discovery_devices=std::make_unique<v8_webgpu_devices>(isolate,context,exception_constructor,2,4);
                    auto discovery_adapters=std::make_unique<v8_webgpu_adapters>(isolate,context,*discovery_devices,exception_constructor,2);
                    auto discovery=std::make_unique<v8_webgpu_discovery>(isolate,context,adapter_fixture,*discovery_adapters);
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"gpuDiscoveryProbe"),discovery->object()).FromMaybe(false),"Discovery object publication failed");
                    auto language_set=discovery->object()->Get(context,v8::String::NewFromUtf8Literal(isolate,"wgslLanguageFeatures")).ToLocalChecked().As<v8::Object>();
                    auto language_has=language_set->Get(context,v8::String::NewFromUtf8Literal(isolate,"has")).ToLocalChecked().As<v8::Function>();
                    size_t language_count=0;
                    for(const auto& feature:wgsl_language_feature_names) {
                        v8::Local<v8::Value> name=v8::String::NewFromUtf8(isolate,feature.name.data(),v8::NewStringType::kNormal,static_cast<int>(feature.name.size())).ToLocalChecked();
                        bool expected=adapter_fixture.dawn().instance().HasWGSLLanguageFeature(feature.native);
                        require(language_has->Call(context,language_set,1,&name).ToLocalChecked()->BooleanValue(isolate)==expected,"WGSL capability differs from native instance");
                        language_count+=expected;
                    }
                    require(language_set->Get(context,v8::String::NewFromUtf8Literal(isolate,"size")).ToLocalChecked()->Uint32Value(context).FromJust()==language_count,"WGSL snapshot count incorrect");
                    { v8::TryCatch caught(isolate);v8::Local<v8::Value> name=v8::String::NewFromUtf8Literal(isolate,"x");
                        require(feature_has->Call(context,language_set,1,&name).IsEmpty() && caught.HasCaught(),"WGSL set accepted GPU feature-set receiver brand");
                    }
                    require(run("{let f=gpuDiscoveryProbe.wgslLanguageFeatures;if(f!==gpuDiscoveryProbe.wgslLanguageFeatures||Object.prototype.toString.call(f)!=='[object WGSLLanguageFeatures]'||[...f].length!==f.size||f.has('chromium_testing_shipped')||f.has('chromium_print')||f.has('f16')||f.add)throw new Error('WGSL snapshot');}"),"WGSL snapshot semantics failed");
                    require(run("if(gpuDiscoveryProbe.getPreferredCanvasFormat()!=='bgra8unorm'||gpuDiscoveryProbe.getPreferredCanvasFormat.length!==0)throw new Error('preferred format');let badFormatReceiver=false;try{gpuDiscoveryProbe.getPreferredCanvasFormat.call({})}catch(e){badFormatReceiver=e instanceof TypeError}if(!badFormatReceiver)throw new Error('format receiver');"),"Preferred canvas format behavior failed");
                    bool invalid_format=false;
                    try { v8_webgpu_discovery invalid(isolate,context,adapter_fixture,*discovery_adapters,wgpu::BackendType::Undefined,wgpu::TextureFormat::RGBA16Float); }
                    catch(const std::invalid_argument&) { invalid_format=true; }
                    require(invalid_format,"Unsupported preferred format admitted");
                    { v8_webgpu_discovery rgba(isolate,context,adapter_fixture,*discovery_adapters,wgpu::BackendType::Undefined,wgpu::TextureFormat::RGBA8Unorm);
                        auto object=rgba.object();
                        auto method=object->Get(context,v8::String::NewFromUtf8Literal(isolate,"getPreferredCanvasFormat")).ToLocalChecked().As<v8::Function>();
                        require(method->Call(context,object,0,nullptr).ToLocalChecked()->StrictEquals(v8::String::NewFromUtf8Literal(isolate,"rgba8unorm")),"Host RGBA format selection ignored");
                    }
                    require(run("globalThis.discoveryPromise=gpuDiscoveryProbe.requestAdapter();"),"JavaScript adapter discovery failed");
                    auto discovery_promise=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"discoveryPromise")).ToLocalChecked().As<v8::Promise>();
                    auto discovery_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while(discovery_promise->State()==v8::Promise::kPending && std::chrono::steady_clock::now()<discovery_deadline) {
                        adapter_fixture.pump([&](auto completion) { require(discovery->complete(completion),"Discovery completion routing failed"); });
                        if(discovery_promise->State()==v8::Promise::kPending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(discovery_promise->State()==v8::Promise::kFulfilled && discovery_promise->Result()->IsObject(),"Discovery did not return a hardware adapter wrapper");
                    auto discovered_object=discovery_promise->Result().As<v8::Object>();
                    auto device_method=discovered_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"requestDevice")).ToLocalChecked().As<v8::Function>();
                    auto chain_device=device_method->Call(context,discovered_object,0,nullptr).ToLocalChecked().As<v8::Promise>();
                    discovery_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while(chain_device->State()==v8::Promise::kPending && std::chrono::steady_clock::now()<discovery_deadline) {
                        adapter_fixture.pump([&](auto completion) { require(discovery_adapters->complete(completion),"Discovered device completion routing failed"); });
                        if(chain_device->State()==v8::Promise::kPending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(chain_device->State()==v8::Promise::kFulfilled,"Discovered adapter could not create device");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"discoveryDevice"),chain_device->Result()).FromMaybe(false),"Discovered device publication failed");
                    require(run("{let buffer=discoveryDevice.createBuffer({size:16,usage:8,mappedAtCreation:true});let view=buffer.getMappedRange();new Uint32Array(view)[0]=99;buffer.unmap();if(view.byteLength!==0)throw new Error('discovery mapping');buffer.destroy();}"),"Discovered device could not execute buffer operations");
                    auto unavailable=run("globalThis.unavailableAdapter=gpuDiscoveryProbe.requestAdapter({featureLevel:'not-supported'});globalThis.invalidDiscovery=gpuDiscoveryProbe.requestAdapter({powerPreference:'invalid'});invalidDiscovery.catch(()=>{});");
                    require(unavailable,"Discovery failure paths threw synchronously");
                    auto unavailable_promise=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"unavailableAdapter")).ToLocalChecked().As<v8::Promise>();
                    require(unavailable_promise->State()==v8::Promise::kFulfilled && unavailable_promise->Result()->IsNull(),"Unavailable adapter did not resolve null");
                    auto invalid_discovery=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"invalidDiscovery")).ToLocalChecked().As<v8::Promise>();
                    require(invalid_discovery->State()==v8::Promise::kRejected,"Invalid discovery options did not reject");
                    discovery.reset(); discovery_adapters.reset(); discovery_devices.reset();
                    require(run("delete globalThis.gpuDiscoveryProbe;delete globalThis.discoveryPromise;delete globalThis.discoveryDevice;delete globalThis.unavailableAdapter;delete globalThis.invalidDiscovery;"),"Discovery cleanup failed");
                    adapter_fixture.pump([](completion_record) {});
                    require(adapter_fixture.live_adapters()==0 && adapter_fixture.live_devices()==0,"Discovery chain leaked native ownership");
                    buffer_wrappers_tested=true;
                    return;
                }
                if (record.operation==test_operations[2] || record.operation==test_operations[3]) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    auto context=isolate->GetCurrentContext();
                    auto& request=failed_wrappers[std::find(test_operations.begin()+2,test_operations.begin()+4,record.operation)-(test_operations.begin()+2)];
                    require(record.status==completion_status::success,"Wrapper failure test needs a hardware adapter");
                    require(request->complete(isolate,context,record,[&](wgpu::Adapter) -> v8::MaybeLocal<v8::Value> {
                        if (record.operation==test_operations[2]) throw std::length_error("resource table full");
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
                if (record.operation==test_operations[1]) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    require(record.status==completion_status::cancelled,"Adapter cancellation lost");
                    require(cancelled_adapter->complete(isolate,isolate->GetCurrentContext(),record,
                        [](wgpu::Adapter) -> v8::Local<v8::Value> { throw std::runtime_error("Cancelled adapter was wrapped"); }),
                        "Cancelled adapter promise did not resolve");
                    adapter_cancelled=true;
                    return;
                }
                if (record.operation==test_operations[0]) {
                    auto* isolate=v8::Isolate::GetCurrent();
                    auto context=isolate->GetCurrentContext();
                    require(adapter_request->complete(isolate,context,record,[&](wgpu::Adapter adapter) -> v8::Local<v8::Value> {
                        auto mailbox=adapter_service->dawn().completions();
                        webgpu_device_descriptor requested;
                        auto js_descriptor=v8::Script::Compile(context,v8::String::NewFromUtf8Literal(isolate,"({label:'requested device',defaultQueue:{label:'requested queue'},requiredLimits:{maxBufferSize:8192}})")).ToLocalChecked()->Run(context).ToLocalChecked();
                        require(read_webgpu_device_descriptor(isolate,context,js_descriptor,requested),"Device descriptor conversion failed");
                        webgpu_device_request_error preparation_error;
                        auto prepared=webgpu_prepared_device_descriptor::prepare(requested,adapter,false,preparation_error);
                        require(prepared && preparation_error==webgpu_device_request_error::none,"Actual adapter device preparation failed");
                        auto descriptor=prepared->native();
                        requested.label="mutated source"; requested.queue_label="mutated queue";
                        require(std::string_view(descriptor.label.data,descriptor.label.length)=="requested device"
                            && std::string_view(descriptor.defaultQueue.label.data,descriptor.defaultQueue.label.length)=="requested queue",
                            "Prepared device labels borrow mutable source storage");
                        require(!webgpu_prepared_device_descriptor::prepare(requested,adapter,true,preparation_error)
                            && preparation_error==webgpu_device_request_error::operation_error,"Consumed adapter accepted");
                        auto invalid_request=requested; invalid_request.required_features.push_back(wgpu::FeatureName::DawnInternalUsages);
                        require(!webgpu_prepared_device_descriptor::prepare(invalid_request,adapter,true,preparation_error)
                            && preparation_error==webgpu_device_request_error::unsupported_feature,"Private feature or feature-error precedence incorrect");
                        invalid_request=requested; invalid_request.required_limits.emplace_back(u"unknownLimit",1);
                        require(!webgpu_prepared_device_descriptor::prepare(invalid_request,adapter,false,preparation_error)
                            && preparation_error==webgpu_device_request_error::operation_error,"Unknown required limit accepted");
                        for (const auto& feature:webgpu_feature_names) {
                            auto feature_request=requested; feature_request.required_features={feature.native,feature.native};
                            auto feature_prepared=webgpu_prepared_device_descriptor::prepare(feature_request,adapter,false,preparation_error);
                            if (adapter.HasFeature(feature.native)) {
                                require(feature_prepared && feature_prepared->native().requiredFeatureCount==1
                                    && feature_prepared->native().requiredFeatures[0]==feature.native,"Supported feature set preparation failed");
                            } else require(!feature_prepared && preparation_error==webgpu_device_request_error::unsupported_feature,"Unsupported adapter feature accepted");
                        }
                        for (const auto& [source,expected]:std::vector<std::pair<const char*,const char*>>{
                            {"({requiredFeatures:['unknown-feature']})","TypeError"},
                            {"({requiredLimits:{unknown:1}})","OperationError"},
                            {"({get label(){throw globalThis.requestSentinel=new Error('sentinel')}})","Error"}}) {
                            auto input=v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();
                            const auto occupied=mailbox->metrics().occupied;
                            v8::Local<v8::Promise> rejected;
                            auto invalid=v8_webgpu_device_request::start_checked(isolate,context,input,[&] { return std::pair{adapter,false}; },mailbox,
                                {adapter_service->engine_identity(),new_owner_token(),0},new_owner_token(),
                                context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),rejected);
                            require(invalid && !invalid->pending() && rejected->State()==v8::Promise::kRejected
                                && mailbox->metrics().occupied==occupied,"Invalid descriptor reached native admission or failed to reject");
                            rejected->MarkAsHandled();
                            auto error_name=rejected->Result().As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"name")).ToLocalChecked();
                            require(error_name->StrictEquals(v8::String::NewFromUtf8(isolate,expected).ToLocalChecked()),"Checked device rejection type incorrect");
                            if (std::string_view(expected)=="Error") require(rejected->Result()->StrictEquals(context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"requestSentinel")).ToLocalChecked()),"Descriptor exception identity lost");
                        }
                        auto changing_input=v8::Script::Compile(context,v8::String::NewFromUtf8Literal(isolate,"({get label(){globalThis.adapterConsumedDuringConversion=true;return ''}})")).ToLocalChecked()->Run(context).ToLocalChecked();
                        v8::Local<v8::Promise> consumed_promise;
                        bool consumed_observed=false;
                        auto consumed_request=v8_webgpu_device_request::start_checked(isolate,context,changing_input,[&] {
                            bool consumed=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"adapterConsumedDuringConversion")).ToLocalChecked()->IsTrue();
                            consumed_observed=consumed;return std::pair{adapter,consumed};
                        },mailbox,{adapter_service->engine_identity(),new_owner_token(),0},new_owner_token(),
                            context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),consumed_promise);
                        require(consumed_observed && consumed_request && !consumed_request->pending() && consumed_promise->State()==v8::Promise::kRejected,"Consumed adapter did not reject");
                        consumed_promise->MarkAsHandled();
                        v8::Local<v8::Promise> promise;
                        device_request=v8_webgpu_device_request::start_checked(isolate,context,js_descriptor,[&] { return std::pair{adapter,false}; },mailbox,
                            {adapter_service->engine_identity(),new_owner_token(),0},test_operations[4],
                            context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),promise);
                        require(device_request && device_request->pending(),"Device promise did not start");
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"deviceRequestPromise"),promise).FromMaybe(false),"Device promise publication failed");
                        wgpu::Limits impossible_limits{}; impossible_limits.maxBufferSize=uint64_t{1}<<63;
                        wgpu::DeviceDescriptor impossible_descriptor{}; impossible_descriptor.requiredLimits=&impossible_limits;
                        v8::Local<v8::Promise> failure_promise;
                        failed_device_request=v8_webgpu_device_request::start(isolate,context,impossible_descriptor,adapter,mailbox,
                            {adapter_service->engine_identity(),new_owner_token(),0},test_operations[11],
                            context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),failure_promise);
                        require(failed_device_request && failed_device_request->pending(),"Failure device promise did not start");
                        failure_promise->MarkAsHandled();
                        require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"failedDevicePromise"),failure_promise).FromMaybe(false),"Failure promise publication failed");
                        v8::Local<v8::Promise> cancelled_promise;
                        cancelled_device_request=v8_webgpu_device_request::start(isolate,context,impossible_descriptor,adapter,mailbox,
                            {adapter_service->engine_identity(),new_owner_token(),0},test_operations[13],
                            context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),cancelled_promise);
                        require(cancelled_device_request && cancelled_device_request->pending(),"Cancellable device request did not start");
                        bool wrong_cancel_realm=false;
                        try { cancelled_device_request->cancel(v8::Context::New(isolate)); } catch (const std::logic_error&) { wrong_cancel_realm=true; }
                        require(wrong_cancel_realm && cancelled_device_request->pending(),"Foreign cancellation consumed request");
                        require(cancelled_device_request->cancel(context) && !cancelled_device_request->cancel(context),"Device cancellation was not idempotent");
                        require(cancelled_promise->State()==v8::Promise::kRejected,"Cancellation left an unresolved device promise");
                        cancelled_promise->MarkAsHandled();
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
                    test_v8_webgpu_device_descriptor(isolate,context);
                    test_v8_webgpu_shader_descriptor(isolate,context);
                test_v8_webgpu_render_state(isolate,context);
                test_v8_webgpu_texture_descriptor(isolate,context);
                {
                    auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
                    webgpu_bind_group_layout_descriptor descriptor;
                    require(read_webgpu_bind_group_layout_descriptor(isolate,context,evaluate("({entries:[{binding:0,visibility:1,buffer:{}},{binding:1,visibility:2,sampler:{}},{binding:2,visibility:2,texture:{}},{binding:3,visibility:4,storageTexture:{format:'rgba8unorm'}},{binding:4,visibility:2,externalTexture:{}}]})"),descriptor),"Binding variant conversion failed");
                    descriptor.with_native([&](const auto& native){
                        require(native.entryCount==5&&native.entries[0].buffer.type==wgpu::BufferBindingType::Uniform
                            &&native.entries[1].sampler.type==wgpu::SamplerBindingType::Filtering
                            &&native.entries[2].texture.sampleType==wgpu::TextureSampleType::Float
                            &&native.entries[3].storageTexture.access==wgpu::StorageTextureAccess::WriteOnly
                            &&native.entries[4].nextInChain&&native.entries[4].nextInChain->sType==wgpu::SType::ExternalTextureBindingLayout,
                            "Binding defaults or external chain incorrect");
                    });
                    require(read_webgpu_bind_group_layout_descriptor(isolate,context,evaluate("(()=>{globalThis.bindingOrder=[];return {entries:[new Proxy({binding:0,visibility:1,buffer:new Proxy({},{get(o,k){bindingOrder.push('buffer.'+k);return o[k]}})},{get(o,k){bindingOrder.push(k);return o[k]}})]}})()"),descriptor)
                        &&evaluate("bindingOrder.join(',')==='binding,buffer,buffer.hasDynamicOffset,buffer.minBindingSize,buffer.type,externalTexture,sampler,storageTexture,texture,visibility'")->IsTrue(),"Binding dictionary conversion order incorrect");
                }

                test_v8_webgpu_render_pass_descriptor(isolate,context);
                    v8::Local<v8::Promise> promise;
                    webgpu_adapter_options options;
                    adapter_request=v8_webgpu_adapter_request::start(isolate,context,options,
                        adapter_service->dawn().instance(),adapter_service->dawn().completions(),
                        {adapter_service->engine_identity(),new_owner_token(),0},test_operations[0],wgpu::BackendType::Undefined,promise);
                    require(adapter_request && adapter_request->pending(), "Adapter request did not become pending");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"adapterProbePromise"),promise).FromMaybe(false),
                        "Adapter promise publication failed");
                    const resource_owner cancelled_owner{adapter_service->engine_identity(),new_owner_token(),0};
                    v8::Local<v8::Promise> cancelled_promise;
                    cancelled_adapter=v8_webgpu_adapter_request::start(isolate,context,options,
                        adapter_service->dawn().instance(),adapter_service->dawn().completions(),
                        cancelled_owner,test_operations[1],wgpu::BackendType::Undefined,cancelled_promise);
                    require(cancelled_adapter && cancelled_adapter->pending(),"Cancelled request was not admitted");
                    adapter_service->dawn().completions()->cancel_owner(cancelled_owner);
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"cancelledAdapterPromise"),cancelled_promise).FromMaybe(false),
                        "Cancelled adapter promise publication failed");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"wrapperSentinel"),v8::Object::New(isolate)).FromMaybe(false),"Sentinel publication failed");
                    for (size_t i=0;i<failed_wrappers.size();++i) {
                        v8::Local<v8::Promise> failed_promise;
                        failed_wrappers[i]=v8_webgpu_adapter_request::start(isolate,context,options,
                            adapter_service->dawn().instance(),adapter_service->dawn().completions(),
                            {adapter_service->engine_identity(),new_owner_token(),0},test_operations[2+i],wgpu::BackendType::Undefined,failed_promise);
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
                    auto weak_probe=v8::Object::New(isolate);
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"weakReleaseProbe"),weak_probe).FromMaybe(false),"Weak probe root failed");
                    require(wrappers->attach(weak_probe,release),"weak wrapper registration failed");
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
                    device_registry=std::make_unique<v8_webgpu_devices>(isolate,context,context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>(),1,2);
                    auto device_object=device_registry->wrap(context,*adapter_service,gc_buffer_device).ToLocalChecked();
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) { verify_v8_limits(isolate,context,device_object,owned.native()); });
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"deviceProbe"),device_object).FromMaybe(false),"Device wrapper publication failed");
                    auto feature_object=device_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"features")).ToLocalChecked().As<v8::Object>();
                    auto feature_has=feature_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"has")).ToLocalChecked().As<v8::Function>();
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {
                        size_t enabled=0;
                        for (const auto& feature:webgpu_feature_names) {
                            v8::Local<v8::Value> argument=v8::String::NewFromUtf8(isolate,feature.name.data(),v8::NewStringType::kNormal,static_cast<int>(feature.name.size())).ToLocalChecked();
                            const bool expected=owned.native().HasFeature(feature.native);
                            require(feature_has->Call(context,feature_object,1,&argument).ToLocalChecked()->BooleanValue(isolate)==expected,"Device feature differs from native enabled set");
                            enabled+=expected;
                        }
                        require(feature_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"size")).ToLocalChecked()->Uint32Value(context).FromJust()==enabled,"Device feature count differs from native enabled set");
                    });
                    resource_handle<wgpu::ShaderModule> shader_handle;
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {
                        wgpu::ShaderSourceWGSL source{};source.code="@vertex fn vs()->@builtin(position) vec4f {return vec4f(0,0,0,1);}@fragment fn fs()->@location(0) vec4f {return vec4f(1,0,0,1);}";
                        wgpu::ShaderModuleDescriptor descriptor{};descriptor.nextInChain=&source;
                        shader_handle=owned.create_shader_module(descriptor);
                    });
                    auto shaders=std::make_unique<v8_webgpu_shaders>(isolate,context,1);
                    auto shader_object=shaders->wrap(context,*adapter_service,gc_buffer_device,shader_handle,device_object,"initial shader").ToLocalChecked();
                    bool duplicate_shader=false,foreign_shader_realm=false;
                    try{shaders->wrap(context,*adapter_service,gc_buffer_device,shader_handle,device_object);}catch(const std::invalid_argument&){duplicate_shader=true;}
                    try{shaders->wrap(v8::Context::New(isolate),*adapter_service,gc_buffer_device,shader_handle,device_object);}catch(const std::logic_error&){foreign_shader_realm=true;}
                    require(duplicate_shader && foreign_shader_realm,"Shader wrapper ownership duplicated");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"shaderProbe"),shader_object).FromMaybe(false),"Shader wrapper publication failed");
                    auto shader_script=v8::String::NewFromUtf8Literal(isolate,R"JS(
                        {if(Object.prototype.toString.call(shaderProbe)!=='[object GPUShaderModule]')throw new Error('shader tag');if(shaderProbe.label!=='initial shader')throw new Error('shader initial label');
                        shaderProbe.label='shader\0\ud800';if(shaderProbe.label!=='shader\0\ufffd')throw new Error('shader USV label');
                        let error={};try{shaderProbe.label={toString(){throw error}}}catch(e){if(e!==error)throw e}
                        if(shaderProbe.label!=='shader\0\ufffd')throw new Error('shader failed label committed');
                        let symbolError=false;try{shaderProbe.label=Symbol()}catch(e){symbolError=e instanceof TypeError}if(!symbolError)throw new Error('shader symbol label');
                        let brand=false;try{Object.getOwnPropertyDescriptor(Object.getPrototypeOf(shaderProbe),'label').get.call({})}catch(e){brand=e instanceof TypeError}if(!brand)throw new Error('shader receiver');}
                    )JS");
                    require(!v8::Script::Compile(context,shader_script).ToLocalChecked()->Run(context).IsEmpty(),"Shader wrapper label behavior failed");
                    test_v8_webgpu_programmable_stage(isolate,context);
                    {
                        resource_handle<wgpu::Texture> texture;resource_handle<wgpu::TextureView> view;
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned) {
                            wgpu::TextureDescriptor descriptor{};descriptor.size={4,4,1};descriptor.format=wgpu::TextureFormat::RGBA8Unorm;descriptor.usage=wgpu::TextureUsage::RenderAttachment;
                            texture=owned.create_texture(descriptor);view=owned.create_texture_view(texture,{});
                        });
                        auto views=std::make_unique<v8_webgpu_texture_views>(isolate,context,1);
                        auto object=views->wrap(context,*adapter_service,gc_buffer_device,view,device_object,"view").ToLocalChecked();
                        auto retained=v8_webgpu_texture_views::native_reference(object);
                        bool wrong=false;try{v8_webgpu_shaders::native_reference(object);}catch(const std::invalid_argument&){wrong=true;}
                        require(wrong&&retained,"Texture view interface conversion failed");
                        views.reset();bool retired=false;
                        try{v8_webgpu_texture_views::native_reference(object);}catch(const std::invalid_argument&){retired=true;}
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned) {require(retired&&owned.live_texture_views()==1,"Texture view released inline");});
                        adapter_service->drain_commands();
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned) {require(owned.live_texture_views()==0,"Texture view deferred release failed");owned.release_texture(texture);});
                    }
                    auto retained_shader=v8_webgpu_shaders::native_reference(shader_object);
                    resource_handle<wgpu::RenderPipeline> pipeline_handle;
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {
                        owned.with_shader_module(shader_handle,[&](const auto& native) {require(native.Get()==retained_shader.Get(),"Shader conversion changed native identity");});
                        wgpu::ColorTargetState target{};target.format=wgpu::TextureFormat::RGBA8Unorm;
                        wgpu::FragmentState fragment{};fragment.module=retained_shader;fragment.entryPoint="fs";fragment.targetCount=1;fragment.targets=&target;
                        wgpu::RenderPipelineDescriptor descriptor{};descriptor.vertex.module=retained_shader;descriptor.vertex.entryPoint="vs";descriptor.fragment=&fragment;
                        pipeline_handle=owned.create_render_pipeline(descriptor);
                    });
                    auto pipelines=std::make_unique<v8_webgpu_render_pipelines>(isolate,context,1);
                    auto pipeline_object=pipelines->wrap(context,*adapter_service,gc_buffer_device,pipeline_handle,device_object,"render pipeline").ToLocalChecked();
                    auto retained_pipeline=v8_webgpu_render_pipelines::native_reference(pipeline_object);
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {
                        owned.with_render_pipeline(pipeline_handle,[&](const auto& native) {require(native.Get()==retained_pipeline.Get(),"Pipeline conversion changed native identity");});
                    });
                    bool wrong_shader=false,wrong_pipeline=false,forged_pipeline=false;
                    try{v8_webgpu_shaders::native_reference(pipeline_object);}catch(const std::invalid_argument&){wrong_shader=true;}
                    try{v8_webgpu_render_pipelines::native_reference(shader_object);}catch(const std::invalid_argument&){wrong_pipeline=true;}
                    try{v8_webgpu_render_pipelines::native_reference(v8::Object::New(isolate));}catch(const std::invalid_argument&){forged_pipeline=true;}
                    require(wrong_shader && wrong_pipeline && forged_pipeline,"GPU resource native conversion accepted wrong brand");
                    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"pipelineProbe"),pipeline_object).FromMaybe(false),"Pipeline publication failed");
                    auto pipeline_script=v8::String::NewFromUtf8Literal(isolate,R"JS(
                        {if(Object.prototype.toString.call(pipelineProbe)!=='[object GPURenderPipeline]' || pipelineProbe.label!=='render pipeline')throw new Error('pipeline metadata');
                        pipelineProbe.label='pipeline\0\ud800';if(pipelineProbe.label!=='pipeline\0\ufffd')throw new Error('pipeline label');
                        let wrong=false;try{Object.getOwnPropertyDescriptor(Object.getPrototypeOf(pipelineProbe),'label').get.call(shaderProbe)}catch(e){wrong=e instanceof TypeError}if(!wrong)throw new Error('cross resource getter');}
                    )JS");
                    require(!v8::Script::Compile(context,pipeline_script).ToLocalChecked()->Run(context).IsEmpty(),"Pipeline wrapper behavior failed");
                    pipelines.reset();
                    bool retired_pipeline=false;
                    try{v8_webgpu_render_pipelines::native_reference(pipeline_object);}catch(const std::invalid_argument&){retired_pipeline=true;}
                    require(retired_pipeline && retained_pipeline,"Disposed pipeline conversion remained available");
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {require(owned.live_render_pipelines()==1,"Pipeline disposal released native resources inline");});
                    require(context->Global()->Delete(context,v8::String::NewFromUtf8Literal(isolate,"pipelineProbe")).FromMaybe(false),"Pipeline cleanup failed");
                    shaders.reset();
                    {v8::TryCatch caught(isolate);require(shader_object->Get(context,v8::String::NewFromUtf8Literal(isolate,"label")).IsEmpty()&&caught.HasCaught(),"Disposed shader retained native access");}
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {require(owned.live_shader_modules()==1,"Shader registry released native module inline");});
                    require(context->Global()->Delete(context,v8::String::NewFromUtf8Literal(isolate,"shaderProbe")).FromMaybe(false),"Shader probe cleanup failed");
                    {
                        auto push=v8::String::NewFromUtf8Literal(isolate,"deviceProbe.pushErrorScope('validation');");
                        require(!v8::Script::Compile(context,push).ToLocalChecked()->Run(context).IsEmpty(),"Native scope push script failed");
                        auto native=v8_webgpu_devices::native_reference(device_object);
                        native.InjectError(wgpu::ErrorType::Validation,"scope capture sentinel");
                        auto captured=std::make_shared<std::atomic<int>>(0);
                        native.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                            [captured](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView message){
                                const bool correct=status==wgpu::PopErrorScopeStatus::Success&&type==wgpu::ErrorType::Validation
                                    && std::string_view(message.data,message.length).find("scope capture sentinel")!=std::string_view::npos;
                                captured->store(correct?1:-1);
                            });
                        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                        while(captured->load()==0&&std::chrono::steady_clock::now()<until){
                            adapter_service->pump([](auto){});
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        require(captured->load()==1,"JavaScript scope did not capture Dawn validation error");
                        native.PushErrorScope(wgpu::ErrorFilter::Validation);
                        wgpu::ShaderSourceWGSL invalid_source{};invalid_source.code="/* 😀 */ this is invalid WGSL";
                        wgpu::ShaderModuleDescriptor invalid_descriptor{};invalid_descriptor.nextInChain=&invalid_source;
                        auto invalid_shader=native.CreateShaderModule(&invalid_descriptor);
                        native.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                            [](wgpu::PopErrorScopeStatus,wgpu::ErrorType,wgpu::StringView){});
                        auto diagnostics=std::make_shared<std::atomic<int>>(0);
                        invalid_shader.GetCompilationInfo(wgpu::CallbackMode::AllowSpontaneous,
                            [diagnostics](wgpu::CompilationInfoRequestStatus status,const wgpu::CompilationInfo* info){
                                try{
                                    if(status!=wgpu::CompilationInfoRequestStatus::Success||!info){diagnostics->store(-1);return;}
                                    auto snapshot=webgpu_compilation_info::copy(*info);
                                    bool error=false;
                                    for(const auto& message:snapshot.messages)
                                        error|=message.type==wgpu::CompilationMessageType::Error&&!message.message.empty()&&message.has_utf16&&message.offset>message.utf16_offset;
                                    diagnostics->store(error?1:-1);
                                }catch(...){diagnostics->store(-1);}
                            });
                        until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                        while(diagnostics->load()==0&&std::chrono::steady_clock::now()<until){
                            adapter_service->pump([](auto){});
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        require(diagnostics->load()==1,"Invalid WGSL did not produce owned Dawn diagnostics");

                    }
                    {
                        resource_handle<wgpu::BindGroupLayout> binding_handle;
                        resource_handle<wgpu::PipelineLayout> layout_handle;
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned){
                            wgpu::BindGroupLayoutEntry entry{};entry.binding=0;
                            entry.visibility=wgpu::ShaderStage::Vertex;entry.buffer.type=wgpu::BufferBindingType::Uniform;
                            wgpu::BindGroupLayoutDescriptor descriptor{};descriptor.entryCount=1;descriptor.entries=&entry;
                            binding_handle=owned.create_bind_group_layout(descriptor);
                            owned.with_bind_group_layout(binding_handle,[&](const auto& binding){
                                bool guarded=false;try{owned.release_bind_group_layout(binding_handle);}catch(const std::logic_error&){guarded=true;}
                                require(guarded,"Binding layout released inside native access scope");
                                wgpu::PipelineLayoutDescriptor pipeline{};pipeline.bindGroupLayoutCount=1;pipeline.bindGroupLayouts=&binding;
                                layout_handle=owned.create_pipeline_layout(pipeline);
                            });
                            owned.release_bind_group_layout(binding_handle);
                            require(owned.live_bind_group_layouts()==0&&owned.live_pipeline_layouts()==1,"Layout handle ownership mismatch");
                            owned.with_pipeline_layout(layout_handle,[&](const auto& layout){
                                require(static_cast<bool>(layout),"Pipeline layout lost native dependency");
                                bool guarded=false;try{owned.release_pipeline_layout(layout_handle);}catch(const std::logic_error&){guarded=true;}
                                require(guarded,"Pipeline layout released inside native access scope");
                                guarded=false;try{owned.close();}catch(const std::logic_error&){guarded=true;}
                                require(guarded,"Device closed inside pipeline layout access scope");
                            });
                        });
                        auto layouts=std::make_unique<v8_webgpu_pipeline_layouts>(isolate,context,1);
                        auto wrapper=layouts->wrap(context,*adapter_service,gc_buffer_device,layout_handle,device_object).ToLocalChecked();
                        auto native=v8_webgpu_pipeline_layouts::native_reference(wrapper);
                        bool wrong=false;try{v8_webgpu_bind_group_layouts::native_reference(wrapper);}catch(const std::invalid_argument&){wrong=true;}
                        require(wrong,"Layout wrappers accepted the wrong interface brand");
                        layouts.reset();
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned){require(owned.live_pipeline_layouts()==1,"Layout wrapper released native resource inline");});
                        adapter_service->drain_commands();
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned){require(owned.live_pipeline_layouts()==0,"Deferred pipeline layout release failed");});
                        require(static_cast<bool>(native),"Retained native layout reference lost");
                    }
                    {
                        resource_handle<wgpu::BindGroup> group_handle;
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned){
                            wgpu::BufferDescriptor buffer_descriptor{};
                            buffer_descriptor.size=64;buffer_descriptor.usage=wgpu::BufferUsage::Uniform;
                            auto buffer_handle=owned.create_buffer(buffer_descriptor);
                            wgpu::BindGroupLayoutEntry layout_entry{};layout_entry.binding=0;
                            layout_entry.visibility=wgpu::ShaderStage::Vertex;
                            layout_entry.buffer.type=wgpu::BufferBindingType::Uniform;
                            wgpu::BindGroupLayoutDescriptor layout_descriptor{};
                            layout_descriptor.entryCount=1;layout_descriptor.entries=&layout_entry;
                            auto layout_handle=owned.create_bind_group_layout(layout_descriptor);
                            owned.with_bind_group_layout(layout_handle,[&](const auto& layout){
                                owned.with_buffer(buffer_handle,[&](const auto& buffer){
                                    wgpu::BindGroupEntry entry{};entry.binding=0;entry.buffer=buffer;entry.size=64;
                                    wgpu::BindGroupDescriptor descriptor{};descriptor.layout=layout;
                                    descriptor.entryCount=1;descriptor.entries=&entry;
                                    group_handle=owned.create_bind_group(descriptor);
                                });
                            });
                            owned.release_buffer(buffer_handle);
                            owned.release_bind_group_layout(layout_handle);
                            owned.with_bind_group(group_handle,[&](const auto& group){
                                require(static_cast<bool>(group),"Bind group lost native dependencies");
                                bool guarded=false;try{owned.release_bind_group(group_handle);}catch(const std::logic_error&){guarded=true;}
                                require(guarded,"Bind group released during native access");
                                guarded=false;try{owned.close();}catch(const std::logic_error&){guarded=true;}
                                require(guarded,"Device closed during bind group access");
                            });
                        });
                        auto groups=std::make_unique<v8_webgpu_bind_groups>(isolate,context,1);
                        auto wrapper=groups->wrap(context,*adapter_service,gc_buffer_device,group_handle,device_object).ToLocalChecked();
                        auto native=v8_webgpu_bind_groups::native_reference(wrapper);
                        bool wrong=false;try{v8_webgpu_bind_group_layouts::native_reference(wrapper);}catch(const std::invalid_argument&){wrong=true;}
                        require(wrong,"Bind group accepted as a bind group layout");
                        groups.reset();
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned){require(owned.live_bind_groups()==1,"Bind group released inline");});
                        adapter_service->drain_commands();
                        adapter_service->with_device(gc_buffer_device,[&](auto& owned){require(owned.live_bind_groups()==0,"Deferred bind group release failed");});
                        require(static_cast<bool>(native),"Retained native bind group reference lost");
                    }
                    auto device_script=v8::String::NewFromUtf8Literal(isolate,R"JS(
                        {
                            if(deviceProbe.createShaderModule.length!==1)throw new Error('shader arity');
                            const module=deviceProbe.createShaderModule({label:'JS shader',code:'@compute @workgroup_size(1) fn main() {} @vertex fn vs()->@builtin(position) vec4f {return vec4f(0,0,0,1);} @fragment fn fs()->@location(0) vec4f {return vec4f(1,0,0,1);}',compilationHints:[{entryPoint:'main',layout:'auto'}]});
                            if(Object.prototype.toString.call(module)!=='[object GPUShaderModule]' || module.label!=='JS shader')throw new Error('shader creation');
                            if(deviceProbe.createRenderPipeline.length!==1)throw new Error('pipeline arity');
                            const pipeline=deviceProbe.createRenderPipeline({label:'JS pipeline',layout:'auto',vertex:{module,entryPoint:'vs'},fragment:{module,entryPoint:'fs',targets:[{format:'rgba8unorm'}]}});
                            if(Object.prototype.toString.call(pipeline)!=='[object GPURenderPipeline]'||pipeline.label!=='JS pipeline')throw new Error('pipeline creation');
                            pipeline.label='updated pipeline';if(pipeline.label!=='updated pipeline')throw new Error('pipeline label');
                            for(const call of [()=>deviceProbe.createRenderPipeline(),()=>deviceProbe.createRenderPipeline({}),()=>deviceProbe.createRenderPipeline.call({},{}),()=>deviceProbe.createRenderPipeline({layout:'auto',vertex:{module:{}}})]) {
                                let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('invalid pipeline call');
                            }
                            module.label='updated shader';if(module.label!=='updated shader')throw new Error('created shader label');
                            for(const call of [()=>deviceProbe.createShaderModule(),()=>deviceProbe.createShaderModule({}),()=>deviceProbe.createShaderModule.call({}, {code:''})]) {
                                let rejected=false;try{call()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('shader invalid call');
                            }
                            const shaderSentinel={};let propagated=false;
                            try{deviceProbe.createShaderModule({get code(){throw shaderSentinel}})}catch(e){propagated=e===shaderSentinel}
                            if(!propagated)throw new Error('shader getter exception');
                            const limits=deviceProbe.limits;globalThis.retainedLimits=limits;
                            const originalLimit=limits.maxBufferSize;
                            if(Object.prototype.toString.call(limits)!=='[object GPUSupportedLimits]')throw new Error('limits tag');
                            let readOnly=false;try{(()=>{'use strict';limits.maxBufferSize=1})()}catch(e){readOnly=e instanceof TypeError}
                            if(!readOnly||limits.maxBufferSize!==originalLimit)throw new Error('limits mutable');
                            const getter=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(limits),'maxBufferSize').get;
                            let wrongLimits=false;try{getter.call({})}catch(e){wrongLimits=e instanceof TypeError}if(!wrongLimits)throw new Error('limits receiver');
                            const features=deviceProbe.features;
                            globalThis.retainedFeatures=features;
                            if(features!==deviceProbe.features)throw new Error('features identity');
                            if(Object.prototype.toString.call(features)!=='[object GPUSupportedFeatures]')throw new Error('features brand');
                            const names=[...features];
                            if(names.length!==features.size || new Set(names).size!==names.length)throw new Error('features size');
                            if(features.keys!==features.values || features.values!==features[Symbol.iterator])throw new Error('features iterator identity');
                            if(features.has('dawn-internal-usages') || features.has('shared-texture-memory-iosurface'))throw new Error('private feature exposed');
                            for(const name of names)if(!features.has({toString(){return name}}))throw new Error('feature coercion');
                            for(const [a,b] of features.entries())if(a!==b || !features.has(a))throw new Error('feature entries');
                            let count=0;const thisArg={};features.forEach(function(a,b,set){if(this!==thisArg || a!==b || set!==features)throw new Error('forEach arguments');count++},thisArg);
                            if(count!==features.size || features.add || features.delete || features.clear)throw new Error('immutable features');
                            for(const operation of [()=>features.has(),()=>features.has(Symbol()),()=>features.has.call({},'x'),()=>features.forEach(null),()=>Set.prototype.add.call(features,'x')]) {
                                let rejected=false;try{operation()}catch(e){rejected=e instanceof TypeError}if(!rejected)throw new Error('features validation');
                            }
                            const featureFailure={};let featureThrown=false;
                            try{features.has({toString(){throw featureFailure}})}catch(e){featureThrown=e===featureFailure}
                            if(!featureThrown)throw new Error('feature coercion exception');
                            if(deviceProbe.label!=='')throw new Error('device label default');
                            deviceProbe.label='device\0\ud800';
                            if(deviceProbe.label!=='device\0\ufffd')throw new Error('device label conversion');
                            let labelFailure={};
                            try{deviceProbe.label={toString(){throw labelFailure}}}catch(e){if(e!==labelFailure)throw e}
                            if(deviceProbe.label!=='device\0\ufffd')throw new Error('failed device label changed value');
                            let symbolRejected=false;try{deviceProbe.label=Symbol()}catch(e){symbolRejected=e instanceof TypeError}
                            if(!symbolRejected)throw new Error('device Symbol label accepted');
                            if(deviceProbe.createBuffer.length!==1)throw new Error('createBuffer arity');
                            for(let operation of [()=>deviceProbe.createBuffer(),()=>deviceProbe.createBuffer.call({},{}),()=>deviceProbe.destroy.call({})]) {
                                let rejected=false;try{operation()}catch(e){rejected=e instanceof TypeError}
                                if(!rejected)throw new Error('device brand or required descriptor');
                            }
                            let buffer=deviceProbe.createBuffer({size:32,usage:8,mappedAtCreation:true,label:'via device'});
                            if(buffer.size!==32||buffer.usage!==8||buffer.label!=='via device')throw new Error('device-created buffer metadata');
                            let range=buffer.getMappedRange(),words=new Uint32Array(range);words[0]=123;
                            let pending=deviceProbe.createBuffer({size:16,usage:6});
                            globalThis.deviceMapCancelled=false;
                            pending.mapAsync(2).catch(e=>{if(!(e instanceof DOMException)||e.name!=='AbortError')throw e;deviceMapCancelled=true});
                            if(pending.mapState!=='pending')throw new Error('device map did not become pending');
                            deviceProbe.destroy();deviceProbe.destroy();
                            deviceProbe.label='destroyed device';
                            if(deviceProbe.label!=='destroyed device')throw new Error('destroyed device label');
                            if(pending.mapState!=='unmapped')throw new Error('device destroy did not cancel map');
                            if(range.byteLength!==0||words.length!==0||buffer.mapState!=='unmapped'||buffer.size!==32)throw new Error('device destroy mapping lifetime');
                            let rejected=false;try{buffer.getMappedRange()}catch(e){rejected=e instanceof DOMException&&e.name==='OperationError'}
                            if(!rejected)throw new Error('destroyed device mapping remained available');
                            delete globalThis.deviceProbe;
                        }
                    )JS");
                    size_t shaders_before_device_script=0,pipelines_before_device_script=0;
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned){
                        shaders_before_device_script=owned.live_shader_modules();
                        pipelines_before_device_script=owned.live_render_pipelines();
                    });
                    v8::Local<v8::Script> device_test;
                    require(v8::Script::Compile(context,device_script).ToLocal(&device_test) && !device_test->Run(context).IsEmpty(),"JavaScript device buffer creation/destruction failed");
                    adapter_service->with_device(gc_buffer_device,[&](auto& owned) {
                        require(owned.live_shader_modules()==shaders_before_device_script+1,"JavaScript shader creation did not adopt exactly one native module");
                        require(owned.live_render_pipelines()==pipelines_before_device_script+1,"JavaScript pipeline creation did not adopt exactly one native pipeline");
                    });
                    retired_device_probe.Reset(isolate,device_object);
                    async_buffers.reset();
                    adapter_service->destroy_device(gc_buffer_device);
                    gc_buffers.reset(); // Delayed wrapper releases tolerate retired native devices.
                    // Retire disposed shader/pipeline fixture tickets before the
                    // next registration in this deliberately eight-ticket queue.
                    adapter_service->drain_commands();

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
            releases=graphics.release_endpoint(8);
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

            while ((!adapter_delivered || !adapter_cancelled || !cancelled_device_retired || !device_failure_seen || !buffer_wrappers_tested || !buffer_validation_seen || binding_map_completions!=5 || map_completions!=3 || failed_wrapper_count!=2 || mailbox->metrics().native_pending!=0) && std::chrono::steady_clock::now()<deadline) {
                if (runtime.has_pending_tasks()) require(runtime.pump_task(),"Adapter task failed");
                else wake->wait_for(runtime.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
            }
            require(adapter_delivered && discovered_adapter.table && adapter_cancelled,"Actual Dawn adapter discovery/cancellation failed");
            require(buffer_validation_seen && binding_map_completions==5 && map_completions==3 && mailbox->metrics().native_pending==0,"Buffer verification timed out before native retirement");
            require(runtime.execute("if(!allocationMapDone || !bindingReadDone || !bindingMapDone || !bindingCancelDone || !bindingRemapDone || !bindingConversionDone || !asyncMapDone || !cancelMapDone || !adapterPromiseDone || !cancelledAdapterDone || wrapperFailures!==2 || rafDone!==0)throw new Error('adapter promise did not progress while hidden');", "adapter-promise-check"),"Adapter promise checkpoint failed");
            adapter_service->with_adapter(discovered_adapter,[](const auto& adapter) {
                require(static_cast<bool>(adapter),"Discovered adapter lost its native reference");
            });
            adapter_service->destroy_adapter(discovered_adapter);
            adapter_request.reset(); cancelled_adapter.reset();
            for (auto& request:failed_wrappers) request.reset();
            require(runtime.execute("delete globalThis.gcBufferProbe;delete globalThis.weakReleaseProbe;", "buffer-drop-reference"),"Buffer reference removal failed");
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
            const auto device_map_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (!device_map_retired && std::chrono::steady_clock::now()<device_map_deadline) {
                if (runtime.has_pending_tasks()) require(runtime.pump_task(),"Device map retirement task failed");
                else wake->wait_for(runtime.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
            }
            require(cancelled_device_retired,"Cancelled native device callback did not retire");
            require(device_failure_seen,"Device failure completion was not observed");
            require(device_map_retired,"Device map native completion did not retire");
            require(runtime.execute("if(!(retainedLimits.maxBufferSize>0))throw new Error('retained limits');delete globalThis.retainedLimits;if([...retainedFeatures].length!==retainedFeatures.size)throw new Error('retained features');delete globalThis.retainedFeatures;", "retained-features"),"Feature snapshot did not survive registry disposal");
            require(runtime.execute("if(!deviceMapCancelled)throw new Error('device cancellation promise not delivered');", "device-map-cancel-check"),"Device map rejection failed");
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
