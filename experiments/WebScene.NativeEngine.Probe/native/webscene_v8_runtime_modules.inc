    struct module_entry {
        std::string url;
        v8::Global<v8::Context> realm;
        v8::Global<v8::Module> module;
    };
    std::vector<module_entry> module_map;

    std::string module_url(v8::Local<v8::Module> module) {
        for (auto& entry : module_map)
            if (entry.module.Get(isolate) == module) return entry.url;
        return {};
    }

    static v8::MaybeLocal<v8::Module> resolve_module(
        v8::Local<v8::Context> realm, v8::Local<v8::String> specifier,
        v8::Local<v8::FixedArray> attributes, v8::Local<v8::Module> referrer) {
        auto* self = current(v8::Isolate::GetCurrent());
        if (!self) return {};
        if (attributes->Length()) {
            v8::Isolate::GetCurrent()->ThrowException(v8::Exception::TypeError(
                js_dom_string(v8::Isolate::GetCurrent(), "Import attributes are not supported")));
            return {};
        }
        return self->fetch_module(realm, to_utf8(self->isolate, specifier), self->module_url(referrer));
    }

    v8::MaybeLocal<v8::Module> compile_module(v8::Local<v8::Context> realm,
        std::string_view source, const std::string& url) {
        for (auto& entry : module_map)
            if (entry.url == url && entry.realm.Get(isolate) == realm)
                return entry.module.Get(isolate);
        v8::ScriptOrigin origin(js_dom_string(isolate, url), 0, 0, false, -1, {}, false, false, true);
        v8::ScriptCompiler::Source input(js_dom_string(isolate, std::string(source)), origin);
        v8::Local<v8::Module> result;
        if (!v8::ScriptCompiler::CompileModule(isolate, &input).ToLocal(&result)) return {};
        module_map.push_back({url, v8::Global<v8::Context>(isolate, realm),
            v8::Global<v8::Module>(isolate, result)});
        return result;
    }

    v8::MaybeLocal<v8::Module> fetch_module(v8::Local<v8::Context> realm,
        const std::string& specifier, const std::string& base) {
        if (!(specifier.starts_with("./") || specifier.starts_with("../")
            || specifier.starts_with("/") || specifier.find(':') != std::string::npos)) {
            isolate->ThrowException(v8::Exception::TypeError(js_dom_string(isolate,
                "Unable to resolve bare module specifier: " + specifier)));
            return {};
        }
        auto url = resolve_resource_url(specifier, base);
        for (auto& entry : module_map)
            if (entry.url == url && entry.realm.Get(isolate) == realm)
                return entry.module.Get(isolate);
        std::string source, resolved;
        if (!load_text_resource(url, {}, WEBSCENE_RESOURCE_SCRIPT, source, resolved)) {
            isolate->ThrowException(v8::Exception::TypeError(js_dom_string(isolate,
                "Unable to load module: " + url)));
            return {};
        }
        return compile_module(realm, source, resolved);
    }

    static void initialize_import_meta(v8::Local<v8::Context> realm,
        v8::Local<v8::Module> module, v8::Local<v8::Object> meta) {
        auto* self = current(v8::Isolate::GetCurrent());
        if (self) meta->CreateDataProperty(realm, js_dom_string(self->isolate, "url"),
            js_dom_string(self->isolate, self->module_url(module))).Check();
    }

    v8::MaybeLocal<v8::Value> evaluate_module(v8::Local<v8::Context> realm,
        v8::Local<v8::Module> module) {
        if (module->GetStatus() == v8::Module::kUninstantiated
            && !module->InstantiateModule(realm, resolve_module).FromMaybe(false)) return {};
        if (module->GetStatus() == v8::Module::kErrored) {
            isolate->ThrowException(module->GetException()); return {};
        }
        return module->Evaluate(realm);
    }

    static v8::MaybeLocal<v8::Promise> import_module_dynamically(
        v8::Local<v8::Context> realm, v8::Local<v8::Data>,
        v8::Local<v8::Value> resource, v8::Local<v8::String> specifier,
        v8::Local<v8::FixedArray> attributes) {
        auto* isolate = v8::Isolate::GetCurrent();
        auto* self = current(isolate);
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(realm).ToLocal(&resolver)) return {};
        v8::TryCatch caught(isolate);
        v8::Local<v8::Module> module;
        v8::Local<v8::Value> evaluation;
        if (self && attributes->Length() == 0
            && self->fetch_module(realm, to_utf8(isolate, specifier),
                to_utf8(isolate, resource)).ToLocal(&module)
            && self->evaluate_module(realm, module).ToLocal(&evaluation)) {
            auto fulfilled = v8::Function::New(realm, [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(info.Data());
            }, module->GetModuleNamespace()).ToLocalChecked();
            if (evaluation->IsPromise()) return evaluation.As<v8::Promise>()->Then(realm, fulfilled);
            resolver->Resolve(realm, module->GetModuleNamespace()).FromMaybe(false);
        } else {
            auto error = caught.HasCaught() ? caught.Exception()
                : v8::Exception::TypeError(js_dom_string(isolate, "Unable to import module"));
            resolver->Reject(realm, error).FromMaybe(false);
        }
        return resolver->GetPromise();
    }

    bool execute_module(v8::Local<v8::Context> realm, std::string_view source,
        const std::string& url, std::string& error) {
        v8::Context::Scope scope(realm);
        v8::TryCatch caught(isolate);
        v8::Local<v8::Module> module;
        v8::Local<v8::Value> evaluation;
        if (!compile_module(realm, source, url).ToLocal(&module)
            || !evaluate_module(realm, module).ToLocal(&evaluation)) {
            error = describe_reported_exception(caught, realm); return false;
        }
        perform_microtask_checkpoint();
        if (evaluation->IsPromise() && evaluation.As<v8::Promise>()->State() == v8::Promise::kRejected) {
            error = to_utf8(isolate, evaluation.As<v8::Promise>()->Result()); return false;
        }
        return true;
    }
