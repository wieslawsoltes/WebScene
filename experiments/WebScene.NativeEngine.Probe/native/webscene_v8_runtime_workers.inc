    struct worker_state {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<clone_packet> incoming, outgoing;
        std::deque<std::string> errors;
        bool stopped{false};
        bool wake_requested{false};
        v8::Isolate* running_isolate{};
        std::function<void()> notify_parent;
        std::thread thread;
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Context> realm;
        void stop() {
            {
                std::lock_guard lock(mutex);
                stopped = true;
                if (running_isolate) running_isolate->TerminateExecution();
                incoming.clear(); outgoing.clear(); errors.clear();
            }
            ready.notify_all();
            if (thread.joinable()) thread.join();
        }
        ~worker_state() { stop(); }
    };
    std::vector<std::unique_ptr<worker_state>> dedicated_workers;
    worker_state* worker_owner{};
    bool force_dedicated_isolate{false};

    static v8::MaybeLocal<v8::Value> worker_transfer(
        const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (info.Length() < 2) return v8::Undefined(info.GetIsolate());
        if (info[1]->IsObject() && !info[1]->IsArray())
            return info[1].As<v8::Object>()->Get(info.GetIsolate()->GetCurrentContext(), js_string(info.GetIsolate(), "transfer"));
        return info[1];
    }
    static void worker_post_message_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* state = static_cast<worker_state*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        v8::Local<v8::Value> transfer;
        if (!worker_transfer(info).ToLocal(&transfer)) return;
        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet)) return;
        { std::lock_guard lock(state->mutex); if (state->stopped) return; state->incoming.push_back(std::move(packet)); }
        state->ready.notify_one();
    }
    static void worker_post_parent_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate());
        if (!self || !self->worker_owner) return;
        auto* state = self->worker_owner;
        v8::Local<v8::Value> transfer;
        if (!worker_transfer(info).ToLocal(&transfer)) return;
        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet)) return;
        { std::lock_guard lock(state->mutex); if (state->stopped) return; state->outgoing.push_back(std::move(packet)); }
        if (state->notify_parent) state->notify_parent();
    }
    static void worker_terminate(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* state = static_cast<worker_state*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        state->stop();
    }
    static void worker_close(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate());
        if (self && self->worker_owner) {
            std::lock_guard lock(self->worker_owner->mutex); self->worker_owner->stopped = true;
        }
    }
    bool dispatch_worker_message(v8::Local<v8::Context> realm,
        v8::Local<v8::Object> target, const clone_packet* packet, const std::string& error = {}) {
        v8::Context::Scope scope(realm);
        v8::TryCatch caught(isolate);
        auto event = v8::Object::New(isolate);
        event->Set(realm, js_string(isolate,"type"), js_string(isolate, packet ? "message" : "error")).Check();
        if (packet) {
            v8::Local<v8::Value> value;
            if (!deserialize_clone(isolate, realm, *packet).ToLocal(&value)) return false;
            event->Set(realm, js_string(isolate,"data"), value).Check();
        } else event->Set(realm, js_string(isolate,"message"), js_dom_string(isolate,error)).Check();
        v8::Local<v8::Value> dispatch;
        if (!target->Get(realm, js_string(isolate,"dispatchEvent")).ToLocal(&dispatch) || !dispatch->IsFunction()) return false;
        v8::Local<v8::Value> args[]{event};
        if (dispatch.As<v8::Function>()->Call(realm,target,1,args).IsEmpty()) {
            last_error = describe_reported_exception(caught,realm); return false;
        }
        perform_microtask_checkpoint();
        return true;
    }
    bool has_worker_messages() const {
        for (auto& state : dedicated_workers) {
            std::lock_guard lock(state->mutex);
            if (!state->outgoing.empty() || !state->errors.empty()) return true;
        }
        return false;
    }
    bool drain_worker_message() {
        for (auto& state : dedicated_workers) {
            std::optional<clone_packet> packet; std::string error;
            {
                std::lock_guard lock(state->mutex);
                if (!state->errors.empty()) { error = std::move(state->errors.front()); state->errors.pop_front(); }
                else if (!state->outgoing.empty()) { packet = std::move(state->outgoing.front()); state->outgoing.pop_front(); }
                else continue;
            }
            return dispatch_worker_message(state->realm.Get(isolate),state->wrapper.Get(isolate),
                packet ? &*packet : nullptr,error);
        }
        return true;
    }
    void stop_workers() { for (auto& worker : dedicated_workers) worker->stop(); }

    static void worker_construct_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate()); if (!self) return;
        auto* isolate = info.GetIsolate(); auto realm = isolate->GetCurrentContext();
        if (!info.IsConstructCall() || info.Length() < 1) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,"Worker requires new and a URL"))); return;
        }
        const auto url = resolve_resource_url(to_utf8(isolate,info[0]),self->current_base_address());
        // Until worker CORS/credentials are qualified, enforce a same-origin script.
        auto origin = [](const std::string& value) {
            auto scheme = value.find("://");
            return scheme == std::string::npos ? std::string{} : value.substr(0,value.find('/',scheme+3));
        };
        if (origin(url) != origin(self->current_base_address())) {
            throw_dom_exception(info,"Worker script must be same origin","SecurityError"); return;
        }
        bool module = false;
        std::string worker_name;
        if (info.Length()>1 && info[1]->IsObject()) {
            v8::Local<v8::Value> type;
            if (!info[1].As<v8::Object>()->Get(realm,js_string(isolate,"type")).ToLocal(&type)) return;
            if (!type->IsUndefined()) {
                auto text=to_utf8(isolate,type);
                if(text!="classic"&&text!="module"){
                    isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,"Invalid WorkerType")));return;
                }
                module=text=="module";
            }
            v8::Local<v8::Value> name;
            if(!info[1].As<v8::Object>()->Get(realm,js_string(isolate,"name")).ToLocal(&name))return;
            if(!name->IsUndefined())worker_name=to_utf8(isolate,name);
        }
        if (self->dedicated_workers.size() >= 1024) {
            isolate->ThrowException(v8::Exception::RangeError(js_string(isolate,"Worker capacity exhausted"))); return;
        }
        v8::Local<v8::Value> target_constructor;
        if (!realm->Global()->Get(realm,js_string(isolate,"EventTarget")).ToLocal(&target_constructor) || !target_constructor->IsFunction()) return;
        v8::Local<v8::Object> target;
        if (!target_constructor.As<v8::Function>()->NewInstance(realm,0,nullptr).ToLocal(&target)) return;
        v8::Local<v8::Value> prototype;
        if(info.NewTarget().As<v8::Object>()->Get(realm,js_string(isolate,"prototype")).ToLocal(&prototype)
            &&prototype->IsObject())target->SetPrototype(realm,prototype).FromMaybe(false);
        auto state = std::make_unique<worker_state>();
        auto* raw = state.get();
        raw->notify_parent = self->runtime_work_available;
        raw->wrapper.Reset(isolate,target);raw->realm.Reset(isolate,realm);
        auto data=v8::External::New(isolate,raw,v8::kExternalPointerTypeTagDefault);
        target->Set(realm,js_string(isolate,"postMessage"),v8::Function::New(realm,worker_post_message,data,1).ToLocalChecked()).Check();
        target->Set(realm,js_string(isolate,"terminate"),v8::Function::New(realm,worker_terminate,data).ToLocalChecked()).Check();
        auto loader=self->load_resource_callback;
        auto root=self->resource_root;
        self->dedicated_workers.push_back(std::move(state));
        raw->thread=std::thread([raw,url,module,worker_name=std::move(worker_name),loader=std::move(loader),root=std::move(root)] {
            auto report=[&](const std::string& text) {
                {std::lock_guard lock(raw->mutex);if(raw->stopped)return;raw->errors.push_back(text);}
                if(raw->notify_parent)raw->notify_parent();
            };
            try {
                native_document document;
                v8_dom_runtime runtime(document,[]{return v8_dom_runtime::viewport_metrics{1,1,1,0};},{},loader);
                auto* child=runtime.impl_.get();
                struct running_guard {
                    worker_state* state;implementation* child;
                    ~running_guard(){
                        std::lock_guard lock(state->mutex);state->running_isolate=nullptr;
                        if(child->isolate)child->isolate->CancelTerminateExecution();
                    }
                } guard{raw,child};
                child->force_dedicated_isolate=true;child->worker_owner=raw;
                child->runtime_work_available=[raw]{
                    {std::lock_guard lock(raw->mutex);raw->wake_requested=true;}
                    raw->ready.notify_one();
                };
                runtime.set_resource_root(root.string());
                if(!runtime.initialize())throw std::runtime_error(runtime.last_error());
                {
                    std::lock_guard lock(raw->mutex);
                    if(raw->stopped)return;
                    raw->running_isolate=child->isolate;
                }
                {
                    v8::Isolate::Scope entered(child->isolate);v8::HandleScope handles(child->isolate);
                    auto realm=child->context.Get(child->isolate);v8::Context::Scope scope(realm);
                    auto global=realm->Global();
                    child->document_base_address=url;
                    child->set_context_location(realm,global,url);
                    global->Set(realm,js_string(child->isolate,"postMessage"),v8::Function::New(realm,worker_post_parent,{},1).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"close"),v8::Function::New(realm,worker_close).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"self"),global).Check();
                    global->Set(realm,js_string(child->isolate,"name"),js_dom_string(child->isolate,worker_name)).Check();
                    global->Delete(realm,js_string(child->isolate,"document")).FromMaybe(false);
                    global->Delete(realm,js_string(child->isolate,"window")).FromMaybe(false);
                    std::string source,resolved,error;
                    if(!child->load_text_resource(url,{},WEBSCENE_RESOURCE_SCRIPT,source,resolved))report("Unable to load worker: "+url);
                    else if(!(module ? child->execute_module(realm,source,resolved,error)
                                      : child->execute_in_context(realm,source,resolved,error)))report(error);
                }
                while(true) {
                    std::optional<clone_packet> packet;
                    {
                        std::unique_lock lock(raw->mutex);
                        raw->ready.wait_for(lock,runtime.recommended_idle_wait(std::chrono::milliseconds(1000)),
                            [&]{return raw->stopped||raw->wake_requested||!raw->incoming.empty();});
                        raw->wake_requested=false;
                        if(raw->stopped)break;
                        if(!raw->incoming.empty()){packet=std::move(raw->incoming.front());raw->incoming.pop_front();}
                    }
                    v8::Isolate::Scope entered(child->isolate);v8::HandleScope handles(child->isolate);
                    auto realm=child->context.Get(child->isolate);v8::Context::Scope scope(realm);
                    if(packet&&!child->dispatch_worker_message(realm,realm->Global(),&*packet))report(child->last_error);
                    child->drain_tasks();
                }
                {std::lock_guard lock(raw->mutex);raw->running_isolate=nullptr;}
                child->isolate->CancelTerminateExecution();
            } catch(const std::exception& e) {
                {std::lock_guard lock(raw->mutex);raw->running_isolate=nullptr;}
                report(e.what());
            }
        });
        info.GetReturnValue().Set(target);
    }
    static void worker_construct(const v8::FunctionCallbackInfo<v8::Value>& info) {
        try {worker_construct_impl(info);}
        catch(const std::exception& e){
            info.GetIsolate()->ThrowException(v8::Exception::Error(js_string(info.GetIsolate(),e.what())));
        }
    }
    static void worker_post_message(const v8::FunctionCallbackInfo<v8::Value>& info) {
        try {worker_post_message_impl(info);}
        catch(const std::exception& e){info.GetIsolate()->ThrowException(v8::Exception::RangeError(js_string(info.GetIsolate(),e.what())));}
    }
    static void worker_post_parent(const v8::FunctionCallbackInfo<v8::Value>& info) {
        try {worker_post_parent_impl(info);}
        catch(const std::exception& e){info.GetIsolate()->ThrowException(v8::Exception::RangeError(js_string(info.GetIsolate(),e.what())));}
    }
