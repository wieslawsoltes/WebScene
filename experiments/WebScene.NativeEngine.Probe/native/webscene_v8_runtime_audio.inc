#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA)
struct audio_context_binding {
    std::unique_ptr<webscene::media::audio_graph> graph;
    std::unordered_map<uint64_t, uint32_t> sources;
    std::unordered_map<uint64_t, const webscene::media::audio_buffer *> attached;
};
std::unordered_map<uint32_t, audio_context_binding> audio_contexts;
std::unordered_set<uint64_t> media_audio_source_elements;
v8::Global<v8::Private> audio_track_key;
uint32_t next_audio_context{1}, next_audio_track{1};
std::unordered_map<uint32_t, std::shared_ptr<webscene::media::audio_track>> audio_tracks;
// Recording adapters retain these native PCM consumers, independently of
// JS/context lifetime. Each track has its own cursor and stop state.

std::unique_ptr<webscene::media::audio_graph> media_default_audio;
std::unordered_map<uint64_t, uint32_t> media_default_sources;
void start_default_media_audio(uint64_t key, media_binding &b) {
    if (!b.frame.audio || !b.control->playing.load())
        return;
    if (media_audio_source_elements.contains(key))
        return;
    if (!media_default_audio)
        media_default_audio = std::make_unique<webscene::media::audio_graph>();
    if (!media_default_sources.contains(key)) {
        auto id = media_default_audio->create(webscene::media::audio_graph::kind::source);
        media_default_audio->set_source(id, b.frame.audio, b.control);
        media_default_audio->connect(id, 0);
        media_default_sources.emplace(key, id);
    }
    media_default_audio->resume();
}
void release_default_media_audio(uint64_t key) {
    auto found = media_default_sources.find(key);
    if (found != media_default_sources.end()) {
        media_default_audio->disconnect(found->second);
        media_default_sources.erase(found);
        if (media_default_sources.empty())
            media_default_audio.reset();
    }
}

struct audio_decode_binding {
    std::future<webscene::media::audio_buffer> result;
    v8::Global<v8::Context> realm;
    v8::Global<v8::Promise::Resolver> promise;
};
std::vector<audio_decode_binding> audio_decodes;
std::unique_ptr<webscene::media::decode_service> audio_decoder;
void clear_audio_bindings() {
    audio_track_key.Reset();
    audio_tracks.clear();
    media_audio_source_elements.clear();
    media_default_audio.reset();
    media_default_sources.clear();
    audio_decoder.reset();
    audio_decodes.clear();
    audio_contexts.clear();
}
void refresh_audio_source(uint64_t key, media_binding &b) {
    if (!b.frame.audio)
        return;
    for (auto &[id, c] : audio_contexts) {
        auto found = c.sources.find(key);
        if (found != c.sources.end() && c.attached[key] != b.frame.audio.get()) {
            c.graph->set_source(found->second, b.frame.audio, b.control);
            c.attached[key] = b.frame.audio.get();
        }
    }
}
void drain_audio_decodes() {
    for (size_t i = 0; i < audio_decodes.size();) {
        if (audio_decodes[i].result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }
        auto job = std::move(audio_decodes[i]);
        audio_decodes.erase(audio_decodes.begin() + i);
        auto realm = job.realm.Get(isolate);
        v8::Context::Scope scope(realm);
        auto resolver = job.promise.Get(isolate);
        try {
            auto pcm = job.result.get();
            auto value = v8::Object::New(isolate);
            value
                ->CreateDataProperty(realm, js_string(isolate, "sampleRate"),
                                     v8::Integer::NewFromUnsigned(isolate, pcm.sample_rate))
                .Check();
            auto channels = v8::Array::New(isolate, pcm.channels);
            for (uint32_t ch = 0; ch < pcm.channels; ++ch) {
                auto buffer = v8::ArrayBuffer::New(isolate, pcm.frames() * sizeof(float));
                auto data = static_cast<float *>(buffer->GetBackingStore()->Data());
                for (uint64_t f = 0; f < pcm.frames(); ++f)
                    data[f] = pcm.samples[f * pcm.channels + ch];
                channels->Set(realm, ch, v8::Float32Array::New(buffer, 0, pcm.frames())).Check();
            }
            value->CreateDataProperty(realm, js_string(isolate, "channels"), channels).Check();
            resolver->Resolve(realm, value).Check();
        } catch (const std::exception &e) {
            resolver->Reject(realm, v8::Exception::Error(js_string(isolate, e.what()))).Check();
        }
    }
}
std::shared_ptr<webscene::media::audio_track> resolve_audio_track(v8::Local<v8::Value> value) {
    if (!value->IsObject() || audio_track_key.IsEmpty())
        throw std::invalid_argument("Native audio track required");
    auto realm = isolate->GetCurrentContext();
    v8::Local<v8::Value> id;
    if (!value.As<v8::Object>()->GetPrivate(realm, audio_track_key.Get(isolate)).ToLocal(&id) ||
        !id->IsUint32())
        throw std::invalid_argument("Native audio track required");
    auto found = audio_tracks.find(id.As<v8::Uint32>()->Value());
    if (found == audio_tracks.end())
        throw std::invalid_argument("Audio track has been released");
    return found->second;
}
static void audio_native_call(const v8::FunctionCallbackInfo<v8::Value> &info) {
    auto *self = current(info.GetIsolate());
    if (!self)
        return;
    auto realm = info.GetIsolate()->GetCurrentContext();
    try {
        if (info.Length() < 1)
            throw std::invalid_argument("Audio operation required");
        auto op = to_utf8(info.GetIsolate(), info[0]);
        if (op == "trackBind" || op == "trackClone" || op == "trackStop" || op == "trackEnded" ||
            op == "trackEnabled") {
            auto id = info[1]->Uint32Value(realm).FromMaybe(0);
            auto found = self->audio_tracks.find(id);
            if (found == self->audio_tracks.end())
                throw std::invalid_argument("Audio track unavailable");
            auto track = found->second;
            if (op == "trackBind") {
                if (info.Length() < 3 || !info[2]->IsObject())
                    throw std::invalid_argument("Audio track object required");
                if (self->audio_track_key.IsEmpty())
                    self->audio_track_key.Reset(info.GetIsolate(), v8::Private::New(info.GetIsolate()));
                info[2]
                    .As<v8::Object>()
                    ->SetPrivate(realm, self->audio_track_key.Get(info.GetIsolate()),
                                 v8::Integer::NewFromUnsigned(info.GetIsolate(), id))
                    .Check();
                return;
            }
            if (op == "trackStop") {
                track->stop();
                return;
            }
            if (op == "trackEnded") {
                info.GetReturnValue().Set(track->ended());
                return;
            }
            if (op == "trackEnabled") {
                track->enabled = info[2]->BooleanValue(info.GetIsolate());
                return;
            }
            if (self->audio_tracks.size() >= 256)
                throw std::length_error("Audio track limit");
            auto clone = self->next_audio_track++;
            self->audio_tracks.emplace(clone, track->clone());
            info.GetReturnValue().Set(clone);
            return;
        }
        if (op == "create") {
            if (self->audio_contexts.size() >= 8)
                throw std::length_error("Audio context limit");
            auto id = self->next_audio_context++;
            auto rate = info.Length() > 1 ? info[1]->Uint32Value(realm).FromMaybe(48000) : 48000;
            self->audio_contexts.emplace(
                id,
                audio_context_binding{std::make_unique<webscene::media::audio_graph>(true, rate), {}, {}});
            info.GetReturnValue().Set(id);
            return;
        }
        if (op == "decode") {
            if (info.Length() < 2 || !info[1]->IsArrayBuffer())
                throw std::invalid_argument("decodeAudioData requires ArrayBuffer");
            if (self->audio_decodes.size() >= 8)
                throw std::length_error("Audio decode queue full");
            if (!self->audio_decoder)
                self->audio_decoder = std::make_unique<webscene::media::decode_service>([self] {
                    self->media_work_ready.store(true, std::memory_order_release);
                    if (self->runtime_work_available)
                        self->runtime_work_available();
                });
            auto buffer = info[1].As<v8::ArrayBuffer>();
            if (buffer->WasDetached())
                throw std::invalid_argument("Detached audio data");
            auto bytes = buffer->GetBackingStore();
            if (bytes->ByteLength() > webscene::media::decode_limits{}.encoded_bytes)
                throw std::length_error("Audio input too large");
            auto source = std::make_shared<webscene::media::encoded_source>();
            auto p = static_cast<const uint8_t *>(bytes->Data());
            if (bytes->ByteLength())
                source->bytes.assign(p, p + bytes->ByteLength());
            auto resolver = v8::Promise::Resolver::New(realm).ToLocalChecked();
            auto future = self->audio_decoder->audio(
                source, {}, info.Length() > 2 ? info[2]->Uint32Value(realm).FromMaybe(0) : 0);
            buffer->Detach(v8::Local<v8::Value>()).Check();
            self->audio_decodes.push_back({std::move(future),
                                           v8::Global<v8::Context>(info.GetIsolate(), realm),
                                           v8::Global<v8::Promise::Resolver>(info.GetIsolate(), resolver)});
            info.GetReturnValue().Set(resolver->GetPromise());
            return;
        }
        if (info.Length() < 2)
            throw std::invalid_argument("Audio context required");
        auto id = info[1]->Uint32Value(realm).FromMaybe(0);
        auto found = self->audio_contexts.find(id);
        if (found == self->audio_contexts.end())
            throw std::invalid_argument("Audio context unavailable");
        auto &c = found->second;
        auto number = [&](int i, double fallback = 0.) {
            return info.Length() > i ? info[i]->NumberValue(realm).FromMaybe(fallback) : fallback;
        };
        if (op == "close") {
            self->audio_contexts.erase(found);
            return;
        }
        if (op == "resume") {
            c.graph->resume();
            return;
        }
        if (op == "suspend") {
            c.graph->suspend();
            return;
        }
        if (op == "time") {
            info.GetReturnValue().Set(c.graph->time());
            return;
        }
        if (op == "node") {
            auto kind = static_cast<webscene::media::audio_graph::kind>(static_cast<int>(number(2)));
            if (number(2) < 1 || number(2) > 4)
                throw std::invalid_argument("Invalid audio node kind");
            info.GetReturnValue().Set(c.graph->create(kind));
            return;
        }
        if (op == "connect") {
            c.graph->connect(number(2), number(3));
            return;
        }
        if (op == "disconnect") {
            c.graph->disconnect(number(2));
            return;
        }
        if (op == "gain") {
            c.graph->set_gain(number(2), number(3), number(4), number(5));
            return;
        }
        if (op == "capture") {
            if (self->audio_tracks.size() >= 256)
                throw std::length_error("Audio track limit");
            auto track = c.graph->capture(number(2));
            auto track_id = self->next_audio_track++;
            self->audio_tracks.emplace(track_id, std::move(track));
            info.GetReturnValue().Set(track_id);
            return;
        }
        if (op == "validateSource") {
            if (info.Length() < 3 || !info[2]->IsObject())
                throw std::invalid_argument("Media element required");
            auto *element = unwrap_node(info[2].As<v8::Object>());
            if (!element || (element->tag != "audio" && element->tag != "video"))
                throw std::invalid_argument("Media element required");
            const auto media = self->media_bindings.find(self->wrapper_key(*element));
            if (media != self->media_bindings.end() && media->second->stream)
                throw std::invalid_argument("Native URL media audio cannot be routed through Web Audio");
            if (self->media_audio_source_elements.contains(self->wrapper_key(*element)))
                throw std::invalid_argument("Media element already has an audio source node");
            return;
        }
        if (op == "source") {
            if (info.Length() < 4 || !info[3]->IsObject())
                throw std::invalid_argument("Media source required");
            auto *element = unwrap_node(info[3].As<v8::Object>());
            if (!element)
                throw std::invalid_argument("Media source required");
            auto key = self->wrapper_key(*element);
            if (self->media_audio_source_elements.contains(key))
                throw std::invalid_argument("Media element already has an audio source node");
            self->media_audio_source_elements.insert(key);
            self->release_default_media_audio(key);
            c.sources.emplace(key, number(2));
            auto media = self->media_bindings.find(key);
            if (media != self->media_bindings.end() && media->second->frame.audio) {
                c.graph->set_source(number(2), media->second->frame.audio, media->second->control);
                c.attached[key] = media->second->frame.audio.get();
            }
            return;
        }
        if (op == "samples") {
            if (info.Length() < 4 || !info[3]->IsFloat32Array())
                throw std::invalid_argument("Analyser requires Float32Array");
            auto array = info[3].As<v8::Float32Array>();
            auto backing = array->Buffer()->GetBackingStore();
            if (array->Buffer()->WasDetached())
                throw std::invalid_argument("Detached analyser array");
            c.graph->analyser(number(2), std::span<float>(reinterpret_cast<float *>(
                                                              static_cast<uint8_t *>(backing->Data()) +
                                                              array->ByteOffset()),
                                                          array->Length()));
            return;
        }
        throw std::invalid_argument("Unknown audio operation");
    } catch (const std::exception &e) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(js_string(info.GetIsolate(), e.what())));
    }
}
void install_audio_globals(v8::Local<v8::Context> realm) {
    realm->Global()
        ->Set(realm, js_string(isolate, "__websceneAudio"),
              v8::Function::New(realm, audio_native_call).ToLocalChecked())
        .Check();
    constexpr std::string_view source =
#include "media/audio_platform.js.inc"
        ;
    auto script = v8::Script::Compile(realm, js_dom_string(isolate, std::string(source))).ToLocalChecked();
    script->Run(realm).ToLocalChecked();
}
#endif
