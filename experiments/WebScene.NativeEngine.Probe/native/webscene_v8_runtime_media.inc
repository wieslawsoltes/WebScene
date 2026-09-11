#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA)
#if defined(__APPLE__)
struct media_image_wake final : webscene::graphics::completion_wake {
    std::atomic<bool> ready{};
    std::shared_ptr<webscene::graphics::completion_wake> host;
    void signal() noexcept override {
        ready.store(true, std::memory_order_release);
        if (host)
            host->signal();
    }
};
std::shared_ptr<media_image_wake> media_images_ready;
#endif
struct media_binding {
    std::unique_ptr<webscene::media::media_session> session;
    std::unique_ptr<webscene::media::native_stream> stream;
    std::shared_ptr<webscene::media::playback_control> control =
        std::make_shared<webscene::media::playback_control>();
    v8::Global<v8::Context> realm;
    v8::Global<v8::Function> deliver;
    v8::Global<v8::Object> owner;
#if defined(__APPLE__)
    std::unique_ptr<webscene::graphics::iosurface_canvas_images> images;
    bool image_pending{};
#endif
    uint64_t generation{}, version{};
    webscene::media::media_session::snapshot frame;
};
std::unordered_map<uint64_t, std::unique_ptr<media_binding>> media_bindings;
v8::Global<v8::Private> media_initializer_key;
uint64_t media_dom_generation = UINT64_MAX;
std::atomic<bool> media_work_ready{false};
bool media_presentation_ready{};
double media_host_offset{}, media_last_host_timestamp{-1}, media_presentation_time{};
std::array<double, 8> media_refresh_intervals{};
size_t media_refresh_count{}, media_refresh_index{};
void signal_media_presentation(double timestamp_ms) {
    if (media_bindings.empty()) return;
    const auto now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto host = timestamp_ms / 1000.0;
    if (media_last_host_timestamp < 0 || host < media_last_host_timestamp ||
        host - media_last_host_timestamp > .25)
        media_host_offset = now - host;
    const auto interval = host - media_last_host_timestamp;
    if (interval >= .004 && interval <= .05) {
        media_refresh_intervals[media_refresh_index++ % media_refresh_intervals.size()] = interval;
        media_refresh_count = std::min(media_refresh_count + 1, media_refresh_intervals.size());
    }
    media_last_host_timestamp = host;
    auto intervals = media_refresh_intervals;
    std::sort(intervals.begin(), intervals.begin() + media_refresh_count);
    const auto refresh = media_refresh_count ? intervals[(media_refresh_count - 1) / 2] : 1. / 60.;
    // Map the host clock once, rather than incorporating JS/task latency into
    // every sample. The host currently supplies a vsync timestamp, not verified
    // physical scanout feedback; retain that distinction in qualification.
    // Select by coverage of the upcoming refresh interval. Its midpoint
    // avoids unstable boundary decisions when source/display rates match.
    media_presentation_time = host + media_host_offset + refresh * .5;
    media_presentation_ready = true;
}
void clear_media_bindings() {
    clear_audio_bindings();
    media_dom_generation = UINT64_MAX;
    media_bindings.clear();
    media_work_ready = false;
    media_presentation_ready = false;
    media_last_host_timestamp = -1;
    media_refresh_count = media_refresh_index = 0;
#if defined(__APPLE__)
    media_images_ready.reset();
#endif
}
static void media_native_call(const v8::FunctionCallbackInfo<v8::Value> &info) {
    auto *self = current(info.GetIsolate());
    if (!self)
        return;
    try {
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (info.Length() < 2 || !info[1]->IsObject())
            throw std::invalid_argument("Media element required");
        auto *node = unwrap_node(info[1].As<v8::Object>());
        if (!node || (node->tag != "video" && node->tag != "audio"))
            throw std::invalid_argument("Media element required");
        auto key = self->wrapper_key(*node);
        auto op = to_utf8(info.GetIsolate(), info[0]);
        if (op == "brand")
            return;
        if (op == "videoSupported") {
            info.GetReturnValue().Set(webscene::media::native_video_decode_available());
            return;
        }
        const auto stream_allowed = [&](const std::string& url) {
            const auto flags = self->native_media_policy.load();
            return node->tag == "video" && webscene::media::native_stream_supported() &&
                ((url.starts_with("file://") && (flags & WEBSCENE_NATIVE_MEDIA_LOCAL_FILES)) ||
                 ((url.starts_with("http://") || url.starts_with("https://")) &&
                  (flags & WEBSCENE_NATIVE_MEDIA_NETWORK)));
        };
        if (op == "streamAllowed") {
            info.GetReturnValue().Set(info.Length() > 2 && stream_allowed(to_utf8(info.GetIsolate(), info[2])));
            return;
        }
        if (op == "release") {
            auto old = self->media_bindings.find(key);
            if (old != self->media_bindings.end())
                old->second->control->set(0, 1, false, 1, false);
            self->release_default_media_audio(key);
            self->media_bindings.erase(key);
            node->mutable_canvas().gpu_image.reset();
            self->document.mark_scene_changed();
            return;
        }
        if (op == "load" || op == "loadURL") {
            const bool streaming = op == "loadURL";
            if (info.Length() < 5 || (!streaming && !info[2]->IsArrayBuffer()) || !info[4]->IsFunction())
                throw std::invalid_argument("Invalid media load");
            auto source = std::make_shared<webscene::media::encoded_source>();
            std::string url;
            if (streaming) {
                url = to_utf8(info.GetIsolate(), info[2]);
                if (!stream_allowed(url)) throw std::invalid_argument("Native media URL is not enabled by the application");
                if (self->media_audio_source_elements.contains(key))
                    throw std::invalid_argument("Native URL media audio cannot be routed through Web Audio");
            } else {
                auto backing = info[2].As<v8::ArrayBuffer>()->GetBackingStore();
                if (backing->ByteLength() > webscene::media::decode_limits{}.encoded_bytes)
                    throw std::length_error("Media input too large");
                if (backing->ByteLength()) {
                    auto bytes = static_cast<const uint8_t*>(backing->Data());
                    source->bytes.assign(bytes, bytes + backing->ByteLength());
                }
                source->extension = to_utf8(info.GetIsolate(), info[3]);
            }
            self->media_bindings.erase(key);
            if (self->media_bindings.size() >= 32)
                throw std::length_error("Media session limit reached");
            auto binding = std::make_unique<media_binding>();
            binding->realm.Reset(info.GetIsolate(), realm);
            binding->owner.Reset(info.GetIsolate(), info[1].As<v8::Object>());
            binding->deliver.Reset(info.GetIsolate(), info[4].As<v8::Function>());
#if defined(__APPLE__)
            if (!self->media_images_ready) {
                self->media_images_ready = std::make_shared<media_image_wake>();
#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
                self->media_images_ready->host = self->webgpu_wake;
#endif
            }
            binding->images = std::make_unique<webscene::graphics::iosurface_canvas_images>(
                128ULL * 1024 * 1024, self->media_images_ready);
#endif
            auto notify = [self] {
                self->media_work_ready.store(true, std::memory_order_release);
                if (self->runtime_work_available) self->runtime_work_available();
            };
            if (streaming) {
                binding->stream = webscene::media::open_native_stream(std::move(url), notify);
            } else {
                binding->session = std::make_unique<webscene::media::media_session>(notify);
                binding->session->load(std::move(source), node->tag == "video");
            }
            self->media_bindings.emplace(key, std::move(binding));
            return;
        }
        auto found = self->media_bindings.find(key);
        if (found == self->media_bindings.end())
            return;
        if (op == "time") {
            if (found->second->stream) {
                const auto state = found->second->stream->read();
                info.GetReturnValue().Set(state.ended ? state.duration : state.time);
            } else info.GetReturnValue().Set(found->second->control->time());
            return;
        }
        if (op == "state") {
            if (info.Length() < 7)
                throw std::invalid_argument("Invalid media control");
            found->second->control->set(
                info[2]->NumberValue(realm).FromMaybe(0), info[3]->NumberValue(realm).FromMaybe(1),
                info[4]->BooleanValue(info.GetIsolate()), info[5]->NumberValue(realm).FromMaybe(1),
                info[6]->BooleanValue(info.GetIsolate()));
            if (found->second->stream) {
                const auto& control = *found->second->control;
                found->second->stream->control(control.rate, control.playing, control.volume, control.muted);
            } else self->start_default_media_audio(key, *found->second);
            return;
        }
        if (op == "seek") {
            if (info.Length() < 3)
                throw std::invalid_argument("Seek time required");
            const double time = info[2]->NumberValue(realm).FromMaybe(-1);
            if (found->second->stream) found->second->stream->seek(time);
            else found->second->session->seek(time);
            return;
        }
        throw std::invalid_argument("Unknown media operation");
    } catch (const std::exception &e) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(js_string(info.GetIsolate(), e.what())));
    }
}
void synchronize_media_elements() {
    if (media_dom_generation == document.scene_generation() || media_initializer_key.IsEmpty())
        return;
    media_dom_generation = document.scene_generation();
    if (document.media_elements().empty())
        return;
    auto realm = context.Get(isolate);
    v8::Context::Scope scope(realm);
    v8::Local<v8::Value> initializer;
    if (!realm->Global()->GetPrivate(realm, media_initializer_key.Get(isolate)).ToLocal(&initializer) ||
        !initializer->IsFunction())
        return;
    // Registry is O(media elements), never a traversal of the application's
    // DOM. Runs only on native content changes, including parser mutations.
    auto registry = document.media_elements();
    std::vector<dom_node*> elements(registry.begin(),registry.end());
    for (auto *node : elements) {
        if (node->namespace_uri() != dom_node::html_namespace_uri)
            continue;
        auto object = wrap_node(*node);
        v8::Local<v8::Value> args[] = {object, v8::Boolean::New(isolate, is_connected(*node))};
        auto result = initializer.As<v8::Function>()->Call(realm, realm->Global(), 2, args);
        static_cast<void>(result);
    }
}
bool drain_media() {
    const bool presentation = std::exchange(media_presentation_ready, false);
    bool ready = media_work_ready.exchange(false, std::memory_order_acq_rel) || presentation;
#if defined(__APPLE__)
    if (media_images_ready)
        ready = media_images_ready->ready.exchange(false, std::memory_order_acq_rel) || ready;
#endif
    if (!ready)
        return false;
    drain_audio_decodes();
    perform_microtask_checkpoint();
    // Callback may call load()/release(); collect keys then reacquire each binding.
    std::vector<uint64_t> keys;
    for (auto &[key, binding] : media_bindings)
        keys.push_back(key);
    for (auto key : keys) {
        auto found = media_bindings.find(key);
        if (found == media_bindings.end())
            continue;
        auto &b = *found->second;
        webscene::media::media_session::snapshot frame;
        bool buffering = false;
        if (b.stream) {
            auto streamed = b.stream->read();
            frame.generation = 1; frame.version = streamed.version;
            frame.duration = streamed.duration; frame.ready = streamed.ready;
            frame.seeking = streamed.seeking; frame.error = std::move(streamed.error);
            frame.video = std::move(streamed.video); buffering = streamed.buffering;
        } else {
            frame = presentation && b.control->playing.load()
                ? b.session->present(b.control->time_at(media_presentation_time)) : b.session->read();
        }
        if (!frame.version)
            continue;
        const bool changed = frame.generation != b.generation || frame.version != b.version;
#if defined(__APPLE__)
        if (!changed && !b.image_pending)
            continue;
#else
        if (!changed)
            continue;
#endif
        if (presentation && std::getenv("WEBSCENE_MEDIA_TRACE")) {
            std::fprintf(stderr, "media_present host=%.6f media=%.6f pts=%.6f selected=%llu dropped=%llu repeated=%llu changed=%d\n",
                media_presentation_time, b.control->time_at(media_presentation_time), frame.video.timestamp,
                static_cast<unsigned long long>(frame.selected), static_cast<unsigned long long>(frame.dropped),
                static_cast<unsigned long long>(frame.repeated), changed ? 1 : 0);
        }
        if (changed) {
            b.generation = frame.generation;
            b.version = frame.version;
            b.frame = std::move(frame);
            refresh_audio_source(key, b);
        }
#if defined(__APPLE__)
        try {
            if (b.frame.video.native_surface) {
                auto *node = unwrap_node(b.owner.Get(isolate));
                auto &backing = node->mutable_canvas().backing;
                backing.claim_context(webscene::graphics::canvas_context_mode::media);
                if (backing.width() != b.frame.video.width || backing.height() != b.frame.video.height) {
                    backing.reset_bitmap(b.frame.video.width, b.frame.video.height);
                    document.mark_dirty();
                } else
                    backing.publish_content();
                webscene::graphics::image_metadata metadata;
                metadata.canvas = backing.identity();
                metadata.allocation_generation = backing.allocation_generation();
                metadata.content_serial = backing.content_serial();
                metadata.producer_timeline = backing.identity();
                metadata.producer_value = metadata.content_serial;
                metadata.width = backing.width();
                metadata.height = backing.height();
                metadata.format = webscene::graphics::image_format::bgra8_unorm;
                metadata.alpha = webscene::graphics::image_alpha::opaque;
                auto color = webscene::graphics::iosurface_color::adopt_bgra8(
                    static_cast<CVPixelBufferRef>(b.frame.video.native_surface.get()),
                    b.frame.video.native_surface);
                b.image_pending = true;
                if (auto image = b.images->adopt(metadata, std::move(color))) {
                    document.publish_gpu_canvas_image(
                        *node, std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image)));
                    b.image_pending = false;
                }
            }
        } catch (const std::exception &error) {
            b.image_pending = false;
            b.frame.ready = false;
            b.frame.error = error.what();
            b.frame.video = {};
        }
#endif
        if (!changed)
            continue;
        auto realm = b.realm.Get(isolate);
        v8::Context::Scope scope(realm);
        auto value = v8::Object::New(isolate);
        auto put = [&](const char *name, v8::Local<v8::Value> data) {
            value->CreateDataProperty(realm, js_string(isolate, name), data).Check();
        };
        put("seeking", v8::Boolean::New(isolate, b.frame.seeking));
        put("ready", v8::Boolean::New(isolate, b.frame.ready));
        put("buffering", v8::Boolean::New(isolate, buffering));
        put("error", js_string(isolate, b.frame.error.c_str()));
        put("duration", v8::Number::New(isolate, b.frame.duration));
        put("width", v8::Integer::NewFromUnsigned(isolate, b.frame.video.width));
        put("height", v8::Integer::NewFromUnsigned(isolate, b.frame.video.height));
        auto callback = b.deliver.Get(isolate);
        auto owner = b.owner.Get(isolate);
        v8::Local<v8::Value> args[] = {value};
        v8::TryCatch caught(isolate);
        auto ignored = callback->Call(realm, owner, 1, args);
        static_cast<void>(ignored);
        if (caught.HasCaught())
            last_error = describe_exception(caught, realm);
        perform_microtask_checkpoint();
    }
    return true;
}
void install_media_globals(v8::Local<v8::Context> realm) {
    realm->Global()
        ->Set(realm, js_string(isolate, "__websceneMedia"),
              v8::Function::New(realm, media_native_call).ToLocalChecked())
        .Check();
    constexpr std::string_view source =
#include "media/media_platform.js.inc"
        ;
    auto script = v8::Script::Compile(realm, js_dom_string(isolate, std::string(source))).ToLocalChecked();
    script->Run(realm).ToLocalChecked();
    auto init = realm->Global()->Get(realm, js_string(isolate, "__websceneInitializeMedia")).ToLocalChecked();
    if (media_initializer_key.IsEmpty())
        media_initializer_key.Reset(isolate, v8::Private::New(isolate));
    realm->Global()->SetPrivate(realm, media_initializer_key.Get(isolate), init).Check();
    realm->Global()->Delete(realm, js_string(isolate, "__websceneInitializeMedia")).Check();
    install_audio_globals(realm);
}
void attach_media_prototype(v8::Local<v8::Context> realm, dom_node &node, v8::Local<v8::Object> object) {
    if (node.namespace_uri() != dom_node::html_namespace_uri ||
        (node.tag != "audio" && node.tag != "video") || media_initializer_key.IsEmpty())
        return;
    v8::Local<v8::Value> initializer, prototype;
    if (!realm->Global()->GetPrivate(realm, media_initializer_key.Get(isolate)).ToLocal(&initializer) ||
        !initializer->IsFunction())
        return;
    if (!initializer.As<v8::Object>()
             ->Get(realm, js_string(isolate, node.tag == "video" ? "videoPrototype" : "audioPrototype"))
             .ToLocal(&prototype) ||
        !prototype->IsObject())
        return;
    object->SetPrototype(realm, prototype).Check();
    v8::Local<v8::Value> args[] = {object, v8::Boolean::New(isolate, is_connected(node))};
    auto result = initializer.As<v8::Function>()->Call(realm, realm->Global(), 2, args);
    static_cast<void>(result);
}

#endif

#include "webscene_v8_runtime_audio.inc"
