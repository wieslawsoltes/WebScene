    bool require_runtime_html()
    {
        if (allow_runtime_html) return true;
        isolate->ThrowException(v8::Exception::Error(js_string(isolate,
            "Runtime HTML parsing is disabled for this compiled document; use createCompiledTemplate or DOM construction")));
        return false;
    }

    static void create_compiled_template(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        const auto fail = [&](const char* message) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, message)));
        };
        if (info.Length() < 1 || !info[0]->IsString()) {
            fail("createCompiledTemplate requires a registered template name");
            return;
        }
        const auto name = to_utf8(isolate, info[0]);
        const auto found = self->compiled_templates.find(name);
        if (found == self->compiled_templates.end()) {
            fail("Unknown compiled template");
            return;
        }
        // Copy before serialization: toJSON may re-enter native application code.
        const auto factory = found->second;
        std::string data = "{}";
        if (info.Length() > 1 && !info[1]->IsUndefined()) {
            v8::Local<v8::String> json;
            if (!v8::JSON::Stringify(isolate->GetCurrentContext(), info[1]).ToLocal(&json))
                return;
            data = to_utf8(isolate, json);
        }
        try {
            auto& node = factory(self->document, data);
            if (self->document.find_by_native_id(node.id) != &node || node.parent != nullptr) {
                fail("Compiled template must return a detached node owned by this document");
                return;
            }
            info.GetReturnValue().Set(self->wrap_node(node));
        } catch (const std::exception& error) {
            isolate->ThrowException(v8::Exception::Error(js_string(isolate, error.what())));
        } catch (...) {
            isolate->ThrowException(v8::Exception::Error(
                js_string(isolate, "Compiled template construction failed")));
        }
    }

    static void create_compiled_template_parts(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto context = isolate->GetCurrentContext();
        const auto fail = [&](const char* error) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, error)));
        };
        if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsArray()) {
            fail("createCompiledTemplateParts requires a name and an argument array"); return;
        }
        const auto found = self->compiled_dom_templates.find(to_utf8(isolate, info[0]));
        if (found == self->compiled_dom_templates.end()) { fail("Unknown compiled DOM template"); return; }
        const auto factory = found->second;
        const auto input = info[1].As<v8::Array>();
        std::vector<compiled_template_argument> arguments;
        for (uint32_t i=0; i<input->Length(); ++i) {
            v8::Local<v8::Value> value;
            if (!input->Get(context, i).ToLocal(&value)) return;
            dom_node* node = value->IsObject() && self->node_template.Get(isolate)->HasInstance(value)
                ? unwrap_node(value.As<v8::Object>()) : nullptr;
            if (node && self->document.find_by_native_id(node->id) != node) {
                fail("Template argument must belong to this document"); return;
            }
            if (node) arguments.push_back({{}, node});
            else {
                v8::Local<v8::String> text;
                if (!value->ToString(context).ToLocal(&text)) return;
                arguments.push_back({to_utf8(isolate, text), nullptr});
            }
        }
        try {
            auto& node = factory(self->document, arguments);
            if (self->document.find_by_native_id(node.id) != &node || node.parent) {
                fail("Compiled DOM template must return a detached node in this document"); return;
            }
            self->activate_html5ever_subtree(node, false);
            info.GetReturnValue().Set(self->wrap_node(node));
        } catch (const std::exception& error) {
            isolate->ThrowException(v8::Exception::Error(js_string(isolate, error.what())));
        } catch (...) {
            fail("Compiled DOM template failed");
        }
    }
