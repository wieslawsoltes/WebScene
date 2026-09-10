void test_modules_and_clone() {
    webscene_native::native_document document;
    unsigned dependency_loads = 0;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t, const std::string& url, const auto&, const std::string&, int64_t, auto& response) {
            if (url.ends_with("/index.html")) response.content = R"HTML(
                <script type="module" src="./main.js"></script>
                <script>globalThis.executionOrder=['classic'];</script>)HTML";
            else if (url.ends_with("/main.js")) response.content = R"JS(
                import {count, increment} from './dep.js';
                import './cycle.js';
                globalThis.mainUrl=import.meta.url;
                increment();
                globalThis.moduleResult=count;
                globalThis.moduleCurrentScript=document.currentScript;
                executionOrder.push('module');
                import('./dep.js').then(m=>globalThis.dynamicResult=m.count);
            )JS";
            else if (url.ends_with("/dep.js")) {
                ++dependency_loads;
                response.content = "export let count=40;export function increment(){count+=2;}";
            } else if (url.ends_with("/cycle.js")) response.content = "import './main.js';export const cycle=true;";
            else return false;
            return true;
        });
    require(runtime.initialize(), "Module runtime initialization failed");
    require(runtime.load_url("https://modules.test/index.html"), "Module navigation failed");
    require(runtime.execute(R"JS(
        if(moduleResult!==42||dynamicResult!==42)throw Error('module live binding/dynamic import');
        if(mainUrl!=='https://modules.test/main.js'||moduleCurrentScript!==null)throw Error('module metadata');
        if(executionOrder.join(',')!=='classic,module')throw Error('module defer');
        const x={date:new Date(123),map:new Map([['key',42]]),set:new Set([1,2])};x.self=x;
        const copy=structuredClone(x);
        if(copy===x||copy.self!==copy||copy.date.getTime()!==123||copy.map.get('key')!==42||!copy.set.has(2))throw Error('structured clone');
        const bytes=new Uint8Array([1,2,3]);const moved=structuredClone({bytes},{transfer:[bytes.buffer]});
        if(bytes.byteLength!==0||moved.bytes[2]!==3)throw Error('buffer transfer');
        const b=new ArrayBuffer(8);let duplicate=false;
        try{structuredClone(b,{transfer:[b,b]})}catch(e){duplicate=e.name==='DataCloneError'}
        if(!duplicate||b.byteLength!==8)throw Error('duplicate transfer atomicity');
        let invalid=false;try{structuredClone(()=>{})}catch(e){invalid=e.name==='DataCloneError'}
        if(!invalid)throw Error('uncloneable function');
        import('unmapped').then(()=>globalThis.bareRejected=false,()=>globalThis.bareRejected=true);
    )JS","https://modules.test/test.js"), "Modules/clone regression failed");
    require(dependency_loads==1, "Module dependency was fetched more than once");
    require(runtime.execute("if(!bareRejected)throw Error('bare import did not reject');", "assert"), "Dynamic import rejection failed");
}
void test_dedicated_module_worker() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{64,64,1,0};}, {},
        [](uint32_t, const std::string& url, const auto&, const std::string&, int64_t, auto& response) {
            if(url.ends_with("/index.html"))response.content=R"HTML(<script>
                globalThis.workerMessages=[];
                globalThis.workerError='';
                globalThis.worker=new Worker('./worker.js',{type:'module'});
                if(!(worker instanceof Worker)||!(worker instanceof EventTarget))throw Error('Worker interface inheritance');
                worker.onmessage=e=>workerMessages.push(e.data);
                worker.onerror=e=>workerError=e.message;
                const array=new Uint8Array([4,8,12]);
                worker.postMessage({array},[array.buffer]);
                if(array.byteLength!==0)throw Error('parent buffer not transferred');
                globalThis.parentRemainedResponsive=true;
            </script>)HTML";
            else if(url.ends_with("/worker.js"))response.content=R"JS(
                import {factor} from './factor.js';
                self.onmessage=e=>{
                    const array=e.data.array;
                    array[1]*=factor;
                    postMessage({array,documentType:typeof document,url:import.meta.url},[array.buffer]);
                    const workerOwned=new Uint8Array([31,47]);
                    postMessage({detached:array.byteLength===0,workerOwned},[workerOwned.buffer]);
                };
            )JS";
            else if(url.ends_with("/busy.js"))response.content="while(true){}";
            else if(url.ends_with("/factor.js"))response.content="export const factor=3;";
            else return false;
            return true;
        });
    require(runtime.initialize()&&runtime.load_url("https://workers.test/index.html"),"Worker startup failed");
    for(unsigned i=0;i<100;++i){runtime.pump_task();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    if(!runtime.execute(R"JS(
        if(workerError)throw Error(workerError);
        if(!parentRemainedResponsive||workerMessages.length!==2)throw Error('missing ordered worker replies: '+workerMessages.length);
        if(workerMessages[0].array[1]!==24||workerMessages[0].documentType!=='undefined'
            ||workerMessages[0].url!=='https://workers.test/worker.js'||!workerMessages[1].detached)throw Error('worker transfer/isolation');
        worker.terminate();worker.terminate();
        // Originating isolate and allocator owner are gone; transferred memory remains valid.
        if(workerMessages[1].workerOwned[1]!==47)throw Error('worker-owned buffer lifetime');
        globalThis.busyWorker=new Worker('./busy.js');
    )JS","worker-assert"))throw std::runtime_error(runtime.last_error());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(runtime.load_url("https://workers.test/index.html"),"Worker navigation shutdown failed");
}
void test_secure_context_reporting() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{64,64,1,0};},{},
        [](uint32_t,const std::string&,const auto&,const std::string&,int64_t,auto& response){
            response.content="<!doctype html><body>origin test</body>";return true;
        });
    require(runtime.initialize(),"Secure context runtime initialization failed");
    for(auto [url,secure]:{
        std::pair{"https://example.test/index.html",true},
        std::pair{"http://example.test/index.html",false},
        std::pair{"http://localhost:4173/index.html",true},
        std::pair{"http://sub.localhost/index.html",true},
        std::pair{"http://127.0.0.1:4173/index.html",true},
        std::pair{"http://127.1.2.3/index.html",true},
        std::pair{"http://[::1]:4173/index.html",true},
        std::pair{"http://localhost.evil.test/index.html",false},
        std::pair{"http://127.evil.test/index.html",false},
        std::pair{"http://localhost@evil.test/index.html",false}
    }){
        require(runtime.load_url(url),"Origin navigation failed");
        require(runtime.execute(std::string("if(isSecureContext!==")+(secure?"true":"false")+")throw Error('secure context classification');","secure-context"),"Secure context classification mismatch");
        require(runtime.execute("if('gpu' in navigator)throw Error('origin reporting bypassed host GPU policy');","gpu-policy"),"Origin reporting granted GPU admission");
    }
}
