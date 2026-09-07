    // Never invoke authored getters, proxies, toString or prepareStackTrace
    // while copying telemetry. No V8 handles leave this runtime thread.
    std::string diagnostic_string(v8::Local<v8::String> value, bool& truncated)
    {
        if (value.IsEmpty()) return {};
        std::array<char, runtime_diagnostics::maximum_text> buffer;
        size_t characters = 0;
        const auto bytes = value->WriteUtf8(isolate, buffer.data(), buffer.size(),
            v8::String::WriteFlags::kReplaceInvalidUtf8, &characters);
        truncated |= characters < static_cast<size_t>(value->Length());
        return std::string(buffer.data(), bytes);
    }

    std::pair<std::string, std::string> diagnostic_value(v8::Local<v8::Value> value, bool& truncated)
    {
        if (value.IsEmpty() || value->IsUndefined()) return {"undefined", "undefined"};
        if (value->IsString()) return {"string", diagnostic_string(value.As<v8::String>(), truncated)};
        if (value->IsNull()) return {"null", "null"};
        if (value->IsBoolean()) return {"boolean", value->BooleanValue(isolate) ? "true" : "false"};
        if (value->IsNumber()) return {"number", to_utf8(isolate, value)};
        if (value->IsBigInt()) return {"bigint", "[BigInt]"};
        if (value->IsSymbol()) return {"symbol", "[Symbol]"};
        return {"object", value->IsNativeError() ? "[Error]" : "[Object]"};
    }

    void diagnostic_location(runtime_diagnostic& record, v8::Local<v8::Context> local_context)
    {
        record.document_url = document_base_address;
        if (local_context.IsEmpty()) return;
        const auto* root = static_cast<const dom_node*>(local_context->GetAlignedPointerFromEmbedderData(
            1, v8::kEmbedderDataTypeTagDefault));
        if (const auto found = frame_base_addresses.find(root); found != frame_base_addresses.end()) {
            record.document_url = found->second;
            if (root != nullptr) record.frame_id = root->id;
        }
    }

    void diagnostic_stack(runtime_diagnostic& record, v8::Local<v8::StackTrace> trace)
    {
        if (trace.IsEmpty()) return;
        for (int index = 0; index < trace->GetFrameCount() && index < 32; ++index) {
            auto frame = trace->GetFrame(isolate, index);
            auto source = diagnostic_string(frame->GetScriptNameOrSourceURL(), record.truncated);
            if (index == 0 && record.source.empty()) {
                record.source = source;
                record.line = frame->GetLineNumber();
                record.column = frame->GetColumn();
            }
            record.stack += "at " + diagnostic_string(frame->GetFunctionName(), record.truncated)
                + " (" + source + ':' + std::to_string(frame->GetLineNumber())
                + ':' + std::to_string(frame->GetColumn()) + ")\n";
            if (record.stack.size() > runtime_diagnostics::maximum_text) {
                record.truncated = true;
                break;
            }
        }
        record.truncated |= trace->GetFrameCount() > 32;
    }

    runtime_diagnostic exception_diagnostic(v8::Local<v8::Value> value,
        v8::Local<v8::Message> message, v8::Local<v8::Context> local_context)
    {
        runtime_diagnostic record;
        diagnostic_location(record, local_context);
        record.message = diagnostic_value(value, record.truncated).second;
        if (!message.IsEmpty()) {
            record.message = diagnostic_string(message->Get(), record.truncated);
            auto resource = message->GetScriptOrigin().ResourceName();
            if (!resource.IsEmpty() && resource->IsString())
                record.source = diagnostic_string(resource.As<v8::String>(), record.truncated);
            record.line = message->GetLineNumber(local_context).FromMaybe(0);
            record.column = message->GetStartColumn(local_context).FromMaybe(-1) + 1;
            diagnostic_stack(record, message->GetStackTrace());
        }
        if (record.stack.empty() && !value.IsEmpty())
            diagnostic_stack(record, v8::Exception::GetStackTrace(value));
        return record;
    }

    std::string describe_reported_exception(v8::TryCatch& caught,
        v8::Local<v8::Context> local_context = {})
    {
        if (local_context.IsEmpty()) local_context = isolate->GetCurrentContext();
        if (diagnostics != nullptr && diagnostics->enabled(WEBSCENE_DIAGNOSTIC_EXCEPTIONS)) {
            diagnostics->publish(exception_diagnostic(caught.Exception(), caught.Message(), local_context));
        }
        return describe_exception(caught, local_context);
    }

    void install_console(v8::Local<v8::Context> local_context, v8::Local<v8::Object> global)
    {
        auto console = v8::Object::New(isolate);
        constexpr std::array<const char*, 7> names{"log", "warn", "error", "info", "debug", "trace", "assert"};
        for (size_t index = 0; index < names.size(); ++index) {
            console->Set(local_context, js_string(isolate, names[index]),
                v8::Function::New(local_context, console_log,
                    v8::Integer::New(isolate, static_cast<int>(index))).ToLocalChecked()).Check();
        }
        global->Set(local_context, js_string(isolate, "console"), console).Check();
    }
