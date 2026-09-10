    struct clone_packet {
        std::vector<uint8_t> bytes;
        std::vector<std::shared_ptr<v8::BackingStore>> transfers;
    };
    struct clone_delegate : v8::ValueSerializer::Delegate {
        const v8::FunctionCallbackInfo<v8::Value>& info;
        explicit clone_delegate(const v8::FunctionCallbackInfo<v8::Value>& value) : info(value) {}
        void ThrowDataCloneError(v8::Local<v8::String> message) override {
            const auto text = to_utf8(info.GetIsolate(), message);
            throw_dom_exception(info, text.c_str(), "DataCloneError");
        }
    };
    static bool serialize_clone_impl(const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Value> value, v8::Local<v8::Value> transfer, clone_packet& packet) {
        auto* isolate = info.GetIsolate(); auto realm = isolate->GetCurrentContext();
        clone_delegate delegate(info);
        v8::ValueSerializer serializer(isolate, &delegate);
        std::vector<v8::Local<v8::ArrayBuffer>> buffers;
        if (!transfer->IsUndefined()) {
            if (!transfer->IsArray()) {
                isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "transfer must be an array"))); return false;
            }
            auto list = transfer.As<v8::Array>();
            for (uint32_t i = 0; i < list->Length(); ++i) {
                v8::Local<v8::Value> candidate;
                if (!list->Get(realm, i).ToLocal(&candidate)) return false;
                if (!candidate->IsArrayBuffer()) {
                    throw_dom_exception(info, "Unsupported transferable", "DataCloneError"); return false;
                }
                auto buffer = candidate.As<v8::ArrayBuffer>();
                if (!buffer->IsDetachable() || buffer->WasDetached()
                    || std::find(buffers.begin(), buffers.end(), buffer) != buffers.end()) {
                    throw_dom_exception(info, "Invalid or duplicate transferable", "DataCloneError"); return false;
                }
                buffers.push_back(buffer);
                serializer.TransferArrayBuffer(i, buffer);
            }
        }
        serializer.WriteHeader();
        if (!serializer.WriteValue(realm, value).FromMaybe(false)) return false;
        auto [data, size] = serializer.Release();
        std::unique_ptr<uint8_t, decltype(&std::free)> memory(data, std::free);
        packet.bytes.assign(data, data + size);
        for (auto buffer : buffers) packet.transfers.push_back(buffer->GetBackingStore());
        // Serialization must succeed before ownership changes on the sender.
        for (auto buffer : buffers) if (!buffer->Detach({}).FromMaybe(false)) return false;
        return true;
    }
    static bool serialize_clone(const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Value> value, v8::Local<v8::Value> transfer, clone_packet& packet) {
        try {return serialize_clone_impl(info,value,transfer,packet);}
        catch(const std::exception& e){
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(js_string(info.GetIsolate(),e.what())));
            return false;
        }
    }
    static v8::MaybeLocal<v8::Value> deserialize_clone(v8::Isolate* isolate,
        v8::Local<v8::Context> realm, const clone_packet& packet) {
        v8::ValueDeserializer deserializer(isolate, packet.bytes.data(), packet.bytes.size());
        for (uint32_t i = 0; i < packet.transfers.size(); ++i)
            deserializer.TransferArrayBuffer(i, v8::ArrayBuffer::New(isolate, packet.transfers[i]));
        if (!deserializer.ReadHeader(realm).FromMaybe(false)) return {};
        return deserializer.ReadValue(realm);
    }
    static void structured_clone(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (info.Length() == 0) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(js_string(info.GetIsolate(), "structuredClone requires a value"))); return;
        }
        auto realm = info.GetIsolate()->GetCurrentContext();
        if(info.Length()>1&&!info[1]->IsNullOrUndefined()&&!info[1]->IsObject()){
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(js_string(info.GetIsolate(),"StructuredSerializeOptions must be a dictionary")));return;
        }
        v8::Local<v8::Value> transfer = v8::Undefined(info.GetIsolate());
        if (info.Length() > 1 && info[1]->IsObject()
            && !info[1].As<v8::Object>()->Get(realm, js_string(info.GetIsolate(), "transfer")).ToLocal(&transfer)) return;
        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet)) return;
        v8::Local<v8::Value> result;
        if (deserialize_clone(info.GetIsolate(), realm, packet).ToLocal(&result)) info.GetReturnValue().Set(result);
    }
