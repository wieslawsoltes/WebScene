    static void request_animation_frame(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api",
            "Window.requestAnimationFrame",
            info.Length() >= 1 && info[0]->IsFunction() ? "supported" : "invalid-authoring",
            info.Length() >= 1 && info[0]->IsFunction()
                ? std::string{} : "a callback function is required",
            "web-api-binding");
        if (info.Length() < 1 || !info[0]->IsFunction()) return;
        auto* self = current(info.GetIsolate());
        if (self->work_metrics_enabled.load(std::memory_order_relaxed)) {
            ++self->work.animation_frames_requested;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->profile_startup) ++self->startup_raf_scheduled;
#endif
        const auto id = self->next_timer_id++;
        constexpr auto frame_period = std::chrono::duration<double, std::milli>(1000.0 / 60.0);
        self->timers.push_back(timer_task{
            id,
            true,
            false,
            // requestAnimationFrame is released only by the host compositor's
            // WEBSCENE_INPUT_FRAME signal. A wall-clock deadline lets a nested
            // callback escape between host frames with an unrelated timestamp,
            // making CSS transition evidence nondeterministic.
            std::chrono::steady_clock::time_point::max(),
            frame_period,
            v8::Global<v8::Context>(info.GetIsolate(), info.GetIsolate()->GetCurrentContext()),
            v8::Global<v8::Function>(info.GetIsolate(), info[0].As<v8::Function>()),
            {},
            -1});
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        self->inspector_async_task_scheduled(
            "requestAnimationFrame",
            id,
            false);
#endif
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(info.GetIsolate(), id));
    }

    static void schedule_timer(const v8::FunctionCallbackInfo<v8::Value>& info, bool repeating)
    {
        if (info.Length() < 1 || !info[0]->IsFunction()) return;
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "web-api",
            repeating ? "Window.setInterval" : "Window.setTimeout",
            "supported",
            {},
            "web-api-binding");
        const auto id = self->next_timer_id++;
        auto delay = info.Length() > 1
            ? info[1]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0)
            : 0;
        if (!std::isfinite(delay) || delay < 0) delay = 0;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->profile_startup) {
            ++self->startup_timer_scheduled;
            self->startup_max_timer_delay_ms = std::max(
                self->startup_max_timer_delay_ms,
                delay);
        }
#endif
        if (repeating) delay = std::max(1.0, delay);
        if (self->work_metrics_enabled.load(std::memory_order_relaxed)) {
            ++self->work.timers_scheduled;
        }
        const auto interval = std::chrono::duration<double, std::milli>(delay);
        std::vector<v8::Global<v8::Value>> arguments;
        arguments.reserve(info.Length() > 2 ? static_cast<size_t>(info.Length() - 2) : 0U);
        for (int index = 2; index < info.Length(); ++index) {
            arguments.emplace_back(info.GetIsolate(), info[index]);
        }
        self->timers.push_back(timer_task{
            id,
            false,
            repeating,
            std::chrono::steady_clock::now()
                + std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval),
            interval,
            v8::Global<v8::Context>(info.GetIsolate(), info.GetIsolate()->GetCurrentContext()),
            v8::Global<v8::Function>(info.GetIsolate(), info[0].As<v8::Function>()),
            std::move(arguments),
            -1});
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        self->inspector_async_task_scheduled(
            repeating ? "setInterval" : "setTimeout",
            id,
            repeating);
#endif
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(info.GetIsolate(), id));
    }

    static void set_timeout(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        schedule_timer(info, false);
    }

    static void set_interval(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        schedule_timer(info, true);
    }

    static void clear_timeout(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Window.clearTimer", "supported", {}, "web-api-binding");
        if (info.Length() < 1) return;
        auto* self = current(info.GetIsolate());
        const auto id = info[0]->Uint32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
        uint64_t cancelled_animation_frames = 0;
        const auto removed = std::erase_if(
            self->timers,
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            [self, id, &cancelled_animation_frames](const auto& timer) {
#else
            [id, &cancelled_animation_frames](const auto& timer) {
#endif
                if (timer.id != id) return false;
                if (timer.animation_frame) ++cancelled_animation_frames;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                self->inspector_async_task_canceled(id);
#endif
                return true;
            });
        if (self->work_metrics_enabled.load(std::memory_order_relaxed)) {
            self->work.animation_frames_cancelled += cancelled_animation_frames;
            self->work.timers_cancelled += removed - cancelled_animation_frames;
        }
        if (self->active_timer_id == id) self->active_timer_cancelled = true;
    }

    static void performance_now(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Performance.now", "supported", {}, "web-api-binding");
        const auto now = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), now));
    }

    static void performance_get_entries_by_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api",
            "Performance.getEntriesByName",
            "unsupported",
            "performance entries are not retained",
            "web-api-binding");
        // The probe does not retain a Performance Timeline yet. Returning an
        // empty sequence is the browser-correct result when no matching entry
        // has been recorded and lets consumers feature-test marks safely.
        info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
    }

    static void performance_entry(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api",
            "Performance.mark/measure",
            "partially-supported",
            "entry-shaped return values without Performance Timeline retention",
            "web-api-binding");
        auto result = v8::Object::New(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "name"),
            info.Length() > 0 ? info[0]
                : v8::Local<v8::Value>(js_string(info.GetIsolate(), ""))).Check();
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "startTime"),
            v8::Number::New(info.GetIsolate(), std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch()).count())).Check();
        result->Set(local_context, js_string(info.GetIsolate(), "duration"),
            v8::Number::New(info.GetIsolate(), 0)).Check();
        info.GetReturnValue().Set(result);
    }

    static void image_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("img");
        if (info.Length() > 0) node.attributes["width"] = to_utf8(info.GetIsolate(), info[0]);
        if (info.Length() > 1) node.attributes["height"] = to_utf8(info.GetIsolate(), info[1]);
        self->apply_css_rules(node);
        auto object = self->wrap_node(node);
        auto local_context = info.GetIsolate()->GetCurrentContext();
        object->Set(local_context, js_string(info.GetIsolate(), "complete"), v8::False(info.GetIsolate())).Check();
        object->Set(local_context, js_string(info.GetIsolate(), "naturalWidth"),
            v8::Integer::New(info.GetIsolate(), info.Length() > 0
                ? info[0]->Int32Value(local_context).FromMaybe(0) : 0)).Check();
        object->Set(local_context, js_string(info.GetIsolate(), "naturalHeight"),
            v8::Integer::New(info.GetIsolate(), info.Length() > 1
                ? info[1]->Int32Value(local_context).FromMaybe(0) : 0)).Check();
        info.GetReturnValue().Set(object);
    }

    static void image_decode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        resolver->Resolve(local_context, v8::Undefined(info.GetIsolate())).Check();
        info.GetReturnValue().Set(resolver->GetPromise());
    }

    static void window_post_message(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() < 1) return;

        auto isolate = info.GetIsolate();
        auto target_context = self->context.Get(isolate);
        for (const auto& [frame_id, persistent_window] :
            self->actual_frame_windows) {
            auto frame_window = persistent_window.Get(isolate);
            if (frame_window.IsEmpty() || !frame_window->StrictEquals(info.This())) {
                continue;
            }
            const auto frame_context = self->actual_frame_contexts.find(frame_id);
            if (frame_context != self->actual_frame_contexts.end()) {
                target_context = frame_context->second.Get(isolate);
            }
            break;
        }
        auto source_context = isolate->GetIncumbentContext();
        if (target_context.IsEmpty()) return;
        if (source_context.IsEmpty()) source_context = target_context;

        const auto read_origin = [&](v8::Local<v8::Context> local_context) {
            v8::Context::Scope scope(local_context);
            v8::Local<v8::Value> location_value;
            v8::Local<v8::Value> origin_value;
            if (local_context->Global()->Get(
                    local_context,
                    js_string(isolate, "location")).ToLocal(&location_value)
                && location_value->IsObject()
                && location_value.As<v8::Object>()->Get(
                    local_context,
                    js_string(isolate, "origin")).ToLocal(&origin_value)
                && !origin_value->IsNullOrUndefined()) {
                return to_utf8(isolate, origin_value);
            }
            return std::string("null");
        };

        self->pending_window_messages.push_back(pending_window_message{
            v8::Global<v8::Context>(isolate, target_context),
            v8::Global<v8::Context>(isolate, source_context),
            v8::Global<v8::Value>(isolate, info[0]),
            info.Length() > 1 && !info[1]->IsUndefined()
                ? to_utf8(isolate, info[1])
                : std::string("*"),
            read_origin(source_context)});
    }

    static void get_window_frames(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto isolate = info.GetIsolate();
        auto current_context = isolate->GetCurrentContext();
        auto current_global = current_context->Global();

        uint32_t owner_frame_id = 0U;
        for (const auto& [frame_id, persistent_window] :
            self->actual_frame_windows) {
            auto frame_window = persistent_window.Get(isolate);
            if (!frame_window.IsEmpty() && frame_window->StrictEquals(current_global)) {
                owner_frame_id = frame_id;
                break;
            }
        }

        std::vector<v8::Local<v8::Object>> children;
        for (auto* frame :
            self->document.query_selector_all(self->document.body(), "iframe")) {
            if (frame == nullptr || frame->parent == nullptr) continue;
            uint32_t direct_owner_id = 0U;
            for (auto* ancestor = frame->parent;
                ancestor != nullptr;
                ancestor = ancestor->parent) {
                if (ancestor->tag != "iframe") continue;
                direct_owner_id = ancestor->id;
                break;
            }
            if (direct_owner_id != owner_frame_id) continue;
            const auto actual = self->actual_frame_windows.find(frame->id);
            auto frame_window = actual != self->actual_frame_windows.end()
                ? actual->second.Get(isolate)
                : self->frame_window(*frame);
            if (!frame_window.IsEmpty()) children.push_back(frame_window);
        }
        auto result = v8::Array::New(isolate, static_cast<int>(children.size()));
        for (uint32_t index = 0; index < children.size(); ++index) {
            result->Set(current_context, index, children[index]).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void window_focus(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self != nullptr) {
            self->record_feature(
                "web-api",
                "Window.focus",
                "supported",
                {},
                "web-api-binding");
        }
    }

    static v8::MaybeLocal<v8::Value> call_event_listener(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> listener,
        v8::Local<v8::Object> current_target,
        int argument_count,
        v8::Local<v8::Value> arguments[])
    {
        if (listener->IsFunction()) {
            return listener.As<v8::Function>()->Call(
                local_context,
                current_target,
                argument_count,
                arguments);
        }
        if (!listener->IsObject()) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "EventListener is not an object")));
            return {};
        }
        auto listener_object = listener.As<v8::Object>();
        v8::Local<v8::Value> handle_event;
        if (!listener_object->Get(
                local_context,
                js_string(isolate, "handleEvent")).ToLocal(&handle_event)) {
            return {};
        }
        if (!handle_event->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "EventListener.handleEvent is not callable")));
            return {};
        }
        return handle_event.As<v8::Function>()->Call(
            local_context,
            listener_object,
            argument_count,
            arguments);
    }

    static void add_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        feature_decision decision(
            *self,
            "dom",
            "method:EventTarget.addEventListener",
            "dom-binding");
        if (info.Length() < 2) {
            decision.classification = "invalid-authoring";
            decision.semantic_slice = "an event type and listener are required";
            return;
        }
        const auto type = to_utf8(info.GetIsolate(), info[0]);
        auto local_context = info.GetIsolate()->GetCurrentContext();
        bool use_capture = false;
        bool use_once = false;
        bool use_passive = false;
        v8::Local<v8::Object> use_signal;
        if (info.Length() > 2) {
            if (info[2]->IsBoolean()) {
                use_capture = info[2]->BooleanValue(info.GetIsolate());
            } else if (info[2]->IsObject()) {
                auto options = info[2].As<v8::Object>();
                v8::Local<v8::Value> capture;
                if (options->Get(
                        local_context,
                        js_string(info.GetIsolate(), "capture")).ToLocal(&capture)) {
                    use_capture = capture->BooleanValue(info.GetIsolate());
                } else {
                    return;
                }
                v8::Local<v8::Value> once;
                if (options->Get(
                        local_context,
                        js_string(info.GetIsolate(), "once")).ToLocal(&once)) {
                    use_once = once->BooleanValue(info.GetIsolate());
                } else {
                    return;
                }
                v8::Local<v8::Value> passive;
                if (options->Get(
                        local_context,
                        js_string(info.GetIsolate(), "passive")).ToLocal(&passive)) {
                    use_passive = passive->BooleanValue(info.GetIsolate());
                } else {
                    return;
                }
                v8::Local<v8::Value> signal;
                if (!options->Get(
                        local_context,
                        js_string(info.GetIsolate(), "signal")).ToLocal(&signal)) {
                    return;
                }
                if (!signal->IsUndefined()) {
                    if (!signal->IsObject() || signal->IsNull()) {
                        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                            js_string(info.GetIsolate(),
                                "Failed to execute 'addEventListener': signal is not an AbortSignal")));
                        return;
                    }
                    use_signal = signal.As<v8::Object>();
                    v8::Local<v8::Value> aborted;
                    if (!use_signal->Get(
                            local_context,
                            js_string(info.GetIsolate(), "aborted")).ToLocal(&aborted)) {
                        return;
                    }
                    if (aborted->BooleanValue(info.GetIsolate())) return;
                }
            }
        }
        if (info[1]->IsNullOrUndefined()) return;
        if (!info[1]->IsObject()) {
            decision.classification = "unsupported";
            decision.semantic_slice = "an EventListener must be a function or object";
            return;
        }
        self->record_feature(
            "event",
            "type:" + type,
            "unobserved-code-path",
            "listener registered but native dispatch has not been observed",
            "event-listener-registration");
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        const auto standalone_target_id = generated_standalone_event_target_id(
            info.GetIsolate(), info.This());
#endif
        if (self->in_frame_context() || type != "resize"
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
            || standalone_target_id.has_value()
#endif
        ) {
            uint32_t target_node_id = self->is_document_object(info.This())
                ? std::numeric_limits<uint32_t>::max()
                : 0U;
            if (target_node_id == 0U) {
                for (const auto& [key, wrapper] : self->node_wrappers) {
                    auto local_wrapper = wrapper.Get(info.GetIsolate());
                    if (!local_wrapper.IsEmpty() && local_wrapper->StrictEquals(info.This())) {
                        target_node_id = static_cast<uint32_t>(key);
                        break;
                    }
                }
            }
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
            if (target_node_id == 0U) {
                target_node_id = standalone_target_id.value_or(0U);
            }
#endif
            auto& registrations = self->frame_event_listeners[type];
            const auto requested = info[1];
            for (const auto& registration : registrations) {
                auto callback = registration.callback.Get(info.GetIsolate());
                if (!callback.IsEmpty()
                    && registration.target == target_node_id
                    && registration.context.Get(info.GetIsolate()) == local_context
                    && registration.capture == use_capture
                    && callback->StrictEquals(requested)) {
                    return;
                }
            }
            auto name = requested->IsFunction()
                ? to_utf8(info.GetIsolate(), requested.As<v8::Function>()->GetName())
                : std::string("<object listener>");
            if (name.empty()) name = "<anonymous>";
            event_listener_registration registration{
                v8::Global<v8::Value>(info.GetIsolate(), requested),
                v8::Global<v8::Context>(info.GetIsolate(), local_context),
                std::move(name),
                self->current_input_sequence,
                target_node_id,
                use_capture,
                use_once,
                use_passive,
                {}};
            if (!use_signal.IsEmpty()) {
                registration.signal.Reset(info.GetIsolate(), use_signal);
            }
            registrations.push_back(std::move(registration));
        } else if (type == "resize") {
            const auto requested = info[1];
            for (const auto& registration : self->resize_listeners) {
                auto callback = registration.callback.Get(info.GetIsolate());
                if (!callback.IsEmpty()
                    && registration.context.Get(info.GetIsolate()) == local_context
                    && registration.capture == use_capture
                    && callback->StrictEquals(requested)) {
                    return;
                }
            }
            auto name = requested->IsFunction()
                ? to_utf8(info.GetIsolate(), requested.As<v8::Function>()->GetName())
                : std::string("<object listener>");
            if (name.empty()) name = "<anonymous>";
            event_listener_registration registration{
                v8::Global<v8::Value>(info.GetIsolate(), requested),
                v8::Global<v8::Context>(info.GetIsolate(), local_context),
                std::move(name),
                self->current_input_sequence,
                0U,
                use_capture,
                use_once,
                use_passive,
                {}};
            if (!use_signal.IsEmpty()) {
                registration.signal.Reset(info.GetIsolate(), use_signal);
            }
            self->resize_listeners.push_back(std::move(registration));
        }
    }

    static void remove_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom",
            "method:EventTarget.removeEventListener",
            info.Length() >= 2 && (info[1]->IsObject() || info[1]->IsNullOrUndefined())
                ? "supported" : "invalid-authoring",
            info.Length() >= 2 && (info[1]->IsObject() || info[1]->IsNullOrUndefined())
                ? std::string{} : "an event type and object listener are required",
            "dom-binding");
        if (info.Length() < 2 || info[1]->IsNullOrUndefined() || !info[1]->IsObject()) return;
        const auto type = to_utf8(info.GetIsolate(), info[0]);
        const auto requested = info[1];
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        const auto standalone_target_id = generated_standalone_event_target_id(
            info.GetIsolate(), info.This());
#endif
        bool requested_capture = false;
        if (info.Length() > 2) {
            if (info[2]->IsBoolean()) {
                requested_capture = info[2]->BooleanValue(info.GetIsolate());
            } else if (info[2]->IsObject()) {
                v8::Local<v8::Value> capture;
                if (info[2].As<v8::Object>()->Get(
                        info.GetIsolate()->GetCurrentContext(),
                        js_string(info.GetIsolate(), "capture")).ToLocal(&capture)) {
                    requested_capture = capture->BooleanValue(info.GetIsolate());
                }
            }
        }
        if (self->in_frame_context() || type != "resize"
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
            || standalone_target_id.has_value()
#endif
        ) {
            uint32_t target_node_id = self->is_document_object(info.This())
                ? std::numeric_limits<uint32_t>::max()
                : 0U;
            if (target_node_id == 0U) {
                for (const auto& [key, wrapper] : self->node_wrappers) {
                    auto local_wrapper = wrapper.Get(info.GetIsolate());
                    if (!local_wrapper.IsEmpty() && local_wrapper->StrictEquals(info.This())) {
                        target_node_id = static_cast<uint32_t>(key);
                        break;
                    }
                }
            }
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
            if (target_node_id == 0U) {
                target_node_id = standalone_target_id.value_or(0U);
            }
#endif
            auto listeners = self->frame_event_listeners.find(type);
            if (listeners == self->frame_event_listeners.end()) return;
            auto& registrations = listeners->second;
            auto local_context = info.GetIsolate()->GetCurrentContext();
            for (auto& registration : registrations) {
                auto callback = registration.callback.Get(info.GetIsolate());
                if (registration.target != target_node_id
                    || registration.context.Get(info.GetIsolate()) != local_context
                    || registration.capture != requested_capture
                    || callback.IsEmpty()
                    || !callback->StrictEquals(requested)) {
                    continue;
                }
                // Do not erase while an event may be iterating the vector. The
                // dispatch tail compacts this tombstone.
                registration.callback.Reset();
                registration.signal.Reset();
                break;
            }
            return;
        }
        if (type != "resize") return;
        for (auto& registration : self->resize_listeners) {
            auto callback = registration.callback.Get(info.GetIsolate());
            if (registration.context.Get(info.GetIsolate()) != info.GetIsolate()->GetCurrentContext()
                || registration.capture != requested_capture
                || callback.IsEmpty()
                || !callback->StrictEquals(requested)) {
                continue;
            }
            registration.callback.Reset();
            registration.signal.Reset();
            break;
        }
    }

    static void set_pointer_capture(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node != nullptr) self->pointer_capture_target = node;
    }

    static void release_pointer_capture(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self->pointer_capture_target == node) self->pointer_capture_target = nullptr;
    }

    static void has_pointer_capture(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        info.GetReturnValue().Set(
            self->pointer_capture_target == node
                ? v8::True(info.GetIsolate())
                : v8::False(info.GetIsolate()));
    }

    static void initialize_constructed_event(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Object> event,
        bool mouse_event)
    {
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        event->Set(
            local_context,
            js_string(isolate, "type"),
            info.Length() > 0 ? info[0] : v8::Local<v8::Value>(js_string(isolate, ""))).Check();
        event->Set(local_context, js_string(isolate, "bubbles"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "composed"), v8::False(isolate)).Check();
        event->Set(
            local_context,
            js_string(isolate, "__webSceneComposedPath"),
            v8::Array::New(isolate)).Check();
        event->Set(local_context, js_string(isolate, "defaultPrevented"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "eventPhase"), v8::Integer::New(isolate, 0)).Check();
        if (mouse_event) {
            const auto set_number = [&](const char* name, double value) {
                event->Set(
                    local_context,
                    js_string(isolate, name),
                    v8::Number::New(isolate, value)).Check();
            };
            for (const auto* name : {
                    "detail", "screenX", "screenY", "clientX", "clientY",
                    "button", "buttons", "movementX", "movementY"}) {
                set_number(name, 0);
            }
            for (const auto* name : {"ctrlKey", "shiftKey", "altKey", "metaKey"}) {
                event->Set(local_context, js_string(isolate, name), v8::False(isolate)).Check();
            }
            event->Set(local_context, js_string(isolate, "view"), v8::Null(isolate)).Check();
            event->Set(
                local_context,
                js_string(isolate, "relatedTarget"),
                v8::Null(isolate)).Check();
        }
        if (info.Length() > 1 && info[1]->IsObject()) {
            auto options = info[1].As<v8::Object>();
            const auto copy_option = [&](const char* name) {
                v8::Local<v8::Value> value;
                if (options->Get(local_context, js_string(isolate, name)).ToLocal(&value)
                    && !value->IsUndefined()) {
                    event->Set(local_context, js_string(isolate, name), value).Check();
                }
            };
            copy_option("detail");
            copy_option("bubbles");
            copy_option("cancelable");
            copy_option("composed");
            if (mouse_event) {
                copy_option("view");
                copy_option("screenX");
                copy_option("screenY");
                copy_option("clientX");
                copy_option("clientY");
                copy_option("ctrlKey");
                copy_option("shiftKey");
                copy_option("altKey");
                copy_option("metaKey");
                copy_option("button");
                copy_option("buttons");
                copy_option("relatedTarget");
                copy_option("movementX");
                copy_option("movementY");
            }
        }
    }

    static void event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto event = info.IsConstructCall() ? info.This() : v8::Object::New(info.GetIsolate());
        initialize_constructed_event(info, event, false);
        info.GetReturnValue().Set(event);
    }

    static void document_create_event(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (info.Length() < 1) {
            self->record_feature(
                "dom",
                "method:Document.createEvent",
                "invalid-authoring",
                "an event interface name is required",
                "dom-binding");
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Document.createEvent requires an interface name")));
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::String> interface_value;
        if (!info[0]->ToString(local_context).ToLocal(&interface_value)) return;
        const auto interface_name = to_utf8(info.GetIsolate(), interface_value);
        const auto supported = interface_name == "Event";
        self->record_feature(
            "dom",
            "method:Document.createEvent",
            supported ? "supported" : "unsupported",
            supported ? std::string{} : "only the bounded Event interface is supported",
            "dom-binding");
        if (!supported) {
            throw_dom_exception(
                info,
                "The requested event interface is not supported",
                "NotSupportedError");
            return;
        }
        v8::Local<v8::Value> constructor_value;
        if (!local_context->Global()->Get(
                local_context,
                js_string(info.GetIsolate(), "Event")).ToLocal(&constructor_value)
            || !constructor_value->IsFunction()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Event constructor is unavailable")));
            return;
        }
        v8::Local<v8::Value> arguments[] = {js_string(info.GetIsolate(), "")};
        v8::Local<v8::Object> event;
        if (!constructor_value.As<v8::Function>()->NewInstance(
                local_context,
                1,
                arguments).ToLocal(&event)) return;
        info.GetReturnValue().Set(event);
    }

    static void event_init_event(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (info.Length() < 1) {
            self->record_feature(
                "dom",
                "method:Event.initEvent",
                "invalid-authoring",
                "an event type is required",
                "dom-binding");
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Event.initEvent requires an event type")));
            return;
        }
        self->record_feature(
            "dom",
            "method:Event.initEvent",
            "supported",
            {},
            "dom-binding");
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto event = info.This();
        v8::Local<v8::String> type;
        if (!info[0]->ToString(local_context).ToLocal(&type)) return;
        event->Set(local_context, js_string(info.GetIsolate(), "type"), type).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "bubbles"),
            v8::Boolean::New(
                info.GetIsolate(),
                info.Length() > 1 && info[1]->BooleanValue(info.GetIsolate()))).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "cancelable"),
            v8::Boolean::New(
                info.GetIsolate(),
                info.Length() > 2 && info[2]->BooleanValue(info.GetIsolate()))).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "defaultPrevented"),
            v8::False(info.GetIsolate())).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "__webscenePropagationStopped"),
            v8::False(info.GetIsolate())).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "__websceneImmediatePropagationStopped"),
            v8::False(info.GetIsolate())).Check();
    }

    static void mouse_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto event = info.IsConstructCall() ? info.This() : v8::Object::New(info.GetIsolate());
        initialize_constructed_event(info, event, true);
        info.GetReturnValue().Set(event);
    }

    static void css_supports(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
    }

    static void css_escape(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (info.Length() == 0) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "CSS.escape requires one argument")));
            return;
        }
        auto input = info[0]->ToString(local_context).ToLocalChecked();
        const auto length = input->Length();
        std::vector<uint16_t> units(static_cast<size_t>(length));
        input->WriteV2(
            isolate,
            0,
            static_cast<uint32_t>(length),
            units.data());
        std::u16string output;
        const auto append_hex_escape = [&output](uint16_t unit) {
            static constexpr char16_t digits[] = u"0123456789abcdef";
            char16_t reversed[4];
            size_t count = 0;
            auto value = static_cast<uint32_t>(unit);
            do {
                reversed[count++] = digits[value & 0xfU];
                value >>= 4U;
            } while (value != 0U);
            output.push_back(u'\\');
            while (count > 0U) output.push_back(reversed[--count]);
            output.push_back(u' ');
        };
        for (int index = 0; index < length; ++index) {
            const auto unit = units[static_cast<size_t>(index)];
            const auto first = units.empty() ? 0U : units.front();
            if (unit == 0U) {
                output.push_back(u'\ufffd');
            } else if ((unit >= 1U && unit <= 0x1fU) || unit == 0x7fU
                || (index == 0 && unit >= u'0' && unit <= u'9')
                || (index == 1 && first == u'-' && unit >= u'0' && unit <= u'9')) {
                append_hex_escape(unit);
            } else if (index == 0 && unit == u'-' && length == 1) {
                output.push_back(u'\\');
                output.push_back(unit);
            } else if (unit >= 0x80U || unit == u'-' || unit == u'_'
                || (unit >= u'0' && unit <= u'9')
                || (unit >= u'A' && unit <= u'Z')
                || (unit >= u'a' && unit <= u'z')) {
                output.push_back(unit);
            } else {
                output.push_back(u'\\');
                output.push_back(unit);
            }
        }
        info.GetReturnValue().Set(v8::String::NewFromTwoByte(
            isolate,
            reinterpret_cast<const uint16_t*>(output.data()),
            v8::NewStringType::kNormal,
            static_cast<int>(output.size())).ToLocalChecked());
    }

    static resize_observer_state* resize_observer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return info.Data()->IsExternal()
            ? static_cast<resize_observer_state*>(info.Data().As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault))
            : nullptr;
    }

    static void resize_observer_observe(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* observer = resize_observer(info);
        auto* node = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (observer == nullptr || node == nullptr) return;
        if (std::find(observer->nodes.begin(), observer->nodes.end(), node) == observer->nodes.end()) {
            observer->nodes.push_back(node);
            observer->delivered_sizes.erase(node->id);
            current(info.GetIsolate())->resize_observers_pending = true;
        }
    }

    static void resize_observer_unobserve(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* observer = resize_observer(info);
        auto* node = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (observer != nullptr && node != nullptr) {
            std::erase(observer->nodes, node);
            observer->delivered_sizes.erase(node->id);
        }
    }

    static void resize_observer_disconnect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* observer = resize_observer(info);
        if (observer != nullptr) {
            observer->nodes.clear();
            observer->delivered_sizes.clear();
        }
    }

    static void resize_observer_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "web-api",
            "ResizeObserver.constructor",
            "partially-supported",
            "element content-box size delivery without the complete box-option surface",
            "web-api-binding");
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto object = info.IsConstructCall() ? info.This() : v8::Object::New(info.GetIsolate());
        if (info.Length() < 1 || !info[0]->IsFunction()) {
            info.GetReturnValue().Set(object);
            return;
        }
        auto observer = std::make_unique<resize_observer_state>();
        observer->context.Reset(info.GetIsolate(), local_context);
        observer->callback.Reset(info.GetIsolate(), info[0].As<v8::Function>());
        auto* state = observer.get();
        self->resize_observers.push_back(std::move(observer));
        auto data = v8::External::New(
            info.GetIsolate(),
            state,
            v8::kExternalPointerTypeTagDefault);
        object->Set(
            local_context,
            js_string(info.GetIsolate(), "observe"),
            v8::Function::New(local_context, resize_observer_observe, data).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(info.GetIsolate(), "unobserve"),
            v8::Function::New(local_context, resize_observer_unobserve, data).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(info.GetIsolate(), "disconnect"),
            v8::Function::New(local_context, resize_observer_disconnect, data).ToLocalChecked()).Check();
        info.GetReturnValue().Set(object);
    }

    static void observer_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto feature = info.Data()->IsString()
            ? to_utf8(info.GetIsolate(), info.Data())
            : std::string("Observer.constructor");
        current(info.GetIsolate())->record_feature(
            "web-api",
            std::move(feature),
            "unsupported",
            "observe and disconnect are inert",
            "web-api-binding");
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto observer = info.IsConstructCall() ? info.This() : v8::Object::New(isolate);
        auto no_op = v8::Function::New(local_context, console_log).ToLocalChecked();
        observer->Set(local_context, js_string(isolate, "observe"), no_op).Check();
        observer->Set(local_context, js_string(isolate, "disconnect"), no_op).Check();
        observer->Set(local_context, js_string(isolate, "takeRecords"), v8::Function::New(local_context, empty_array).ToLocalChecked()).Check();
        info.GetReturnValue().Set(observer);
    }

    static void abort_signal_throw_if_aborted(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> aborted;
        if (!info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "aborted")).ToLocal(&aborted)
            || !aborted->BooleanValue(info.GetIsolate())) {
            return;
        }
        v8::Local<v8::Value> reason;
        if (info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "reason")).ToLocal(&reason)) {
            info.GetIsolate()->ThrowException(reason);
        }
    }

    static void abort_controller_abort(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || !info.Data()->IsObject()) return;
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto signal = info.Data().As<v8::Object>();
        v8::Local<v8::Value> aborted;
        if (!signal->Get(
                local_context,
                js_string(info.GetIsolate(), "aborted")).ToLocal(&aborted)
            || aborted->BooleanValue(info.GetIsolate())) {
            return;
        }

        const auto default_reason = info.Length() == 0 || info[0]->IsUndefined();
        auto reason = default_reason
            ? v8::Exception::Error(
                js_string(info.GetIsolate(), "This operation was aborted"))
            : info[0];
        if (default_reason && reason->IsObject()) {
            reason.As<v8::Object>()->Set(
                local_context,
                js_string(info.GetIsolate(), "name"),
                js_string(info.GetIsolate(), "AbortError")).Check();
        }
        signal->Set(
            local_context,
            js_string(info.GetIsolate(), "aborted"),
            v8::True(info.GetIsolate())).Check();
        signal->Set(
            local_context,
            js_string(info.GetIsolate(), "reason"),
            reason).Check();

        const auto remove_aborted = [&](auto& registrations) {
            for (auto& registration : registrations) {
                if (registration.signal.IsEmpty()) continue;
                auto registered_signal = registration.signal.Get(info.GetIsolate());
                if (!registered_signal->StrictEquals(signal)) continue;
                registration.callback.Reset();
                registration.signal.Reset();
            }
        };
        for (auto& [type, registrations] : self->frame_event_listeners) {
            static_cast<void>(type);
            remove_aborted(registrations);
        }
        remove_aborted(self->resize_listeners);

        auto event = self->create_event_instance(local_context);
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "type"),
            js_string(info.GetIsolate(), "abort")).Check();
        v8::Local<v8::Value> dispatcher;
        if (signal->Get(
                local_context,
                js_string(info.GetIsolate(), "dispatchEvent")).ToLocal(&dispatcher)
            && dispatcher->IsFunction()) {
            v8::Local<v8::Value> arguments[] = {event};
            static_cast<void>(dispatcher.As<v8::Function>()->Call(
                local_context, signal, 1, arguments));
        }
    }

    static void abort_controller_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'AbortController': Please use the 'new' operator")));
            return;
        }
        self->record_feature(
            "web-api",
            "AbortController.constructor",
            "partially-supported",
            "abort state, reason, listener delivery, and EventTarget listener cleanup; static AbortSignal composition helpers are not implemented",
            "web-api-binding");
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto controller = info.This();
        v8::Local<v8::Value> event_target_value;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "EventTarget")).ToLocal(&event_target_value)
            || !event_target_value->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "AbortController requires EventTarget")));
            return;
        }
        auto signal = event_target_value.As<v8::Function>()->NewInstance(
            local_context, 0, nullptr).ToLocalChecked();
        signal->Set(local_context, js_string(isolate, "aborted"), v8::False(isolate)).Check();
        signal->Set(local_context, js_string(isolate, "reason"), v8::Undefined(isolate)).Check();
        signal->Set(local_context, js_string(isolate, "onabort"), v8::Null(isolate)).Check();
        signal->Set(
            local_context,
            js_string(isolate, "throwIfAborted"),
            v8::Function::New(
                local_context,
                abort_signal_throw_if_aborted).ToLocalChecked()).Check();
        controller->Set(local_context, js_string(isolate, "signal"), signal).Check();
        controller->Set(
            local_context,
            js_string(isolate, "abort"),
            v8::Function::New(
                local_context,
                abort_controller_abort,
                signal).ToLocalChecked()).Check();
        info.GetReturnValue().Set(controller);
    }

    static media_query_list_state* media_query_list(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return info.Data()->IsExternal()
            ? static_cast<media_query_list_state*>(
                info.Data().As<v8::External>()->Value(
                    v8::kExternalPointerTypeTagDefault))
            : nullptr;
    }

    static void add_media_query_list_listener(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool legacy)
    {
        auto* state = media_query_list(info);
        if (state == nullptr) return;
        const auto callback_index = legacy ? 0 : 1;
        if ((!legacy && (info.Length() < 2
                || to_utf8(info.GetIsolate(), info[0]) != "change"))
            || info.Length() <= callback_index
            || info[callback_index]->IsNullOrUndefined()
            || !info[callback_index]->IsObject()) {
            return;
        }
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        const auto requested = info[callback_index];
        for (const auto& registration : state->listeners) {
            auto callback = registration.callback.Get(isolate);
            if (!callback.IsEmpty() && callback->StrictEquals(requested)) return;
        }
        auto once = false;
        if (!legacy && info.Length() > 2 && info[2]->IsObject()) {
            v8::Local<v8::Value> once_value;
            if (info[2].As<v8::Object>()->Get(
                    local_context,
                    js_string(isolate, "once")).ToLocal(&once_value)) {
                once = once_value->BooleanValue(isolate);
            }
        }
        auto name = requested->IsFunction()
            ? to_utf8(isolate, requested.As<v8::Function>()->GetName())
            : std::string("<object listener>");
        if (name.empty()) name = "<anonymous>";
        state->listeners.push_back(event_listener_registration{
            v8::Global<v8::Value>(isolate, requested),
            v8::Global<v8::Context>(isolate, local_context),
            std::move(name),
            current(isolate)->current_input_sequence,
            0U,
            false,
            once,
            false,
            {}});
    }

    static void media_query_list_add_event_listener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        add_media_query_list_listener(info, false);
    }

    static void media_query_list_add_listener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        add_media_query_list_listener(info, true);
    }

    static void remove_media_query_list_listener(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool legacy)
    {
        auto* state = media_query_list(info);
        if (state == nullptr) return;
        const auto callback_index = legacy ? 0 : 1;
        if ((!legacy && (info.Length() < 2
                || to_utf8(info.GetIsolate(), info[0]) != "change"))
            || info.Length() <= callback_index
            || info[callback_index]->IsNullOrUndefined()) {
            return;
        }
        const auto requested = info[callback_index];
        for (auto& registration : state->listeners) {
            auto callback = registration.callback.Get(info.GetIsolate());
            if (callback.IsEmpty() || !callback->StrictEquals(requested)) continue;
            registration.callback.Reset();
            registration.signal.Reset();
            break;
        }
    }

    static void media_query_list_remove_event_listener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        remove_media_query_list_listener(info, false);
    }

    static void media_query_list_remove_listener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        remove_media_query_list_listener(info, true);
    }

    static void match_media(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto result = v8::Object::New(isolate);
        const auto query = info.Length() > 0 ? to_utf8(isolate, info[0]) : std::string{};
        const auto query_supported = self->inventory_media_query(query);
        self->record_feature(
            "web-api",
            "Window.matchMedia",
            query_supported ? "supported" : "unsupported",
            query_supported
                ? "live matches with modern and legacy change-event delivery"
                : "query contains an unsupported media type or condition",
            "web-api-binding");
        auto state = std::make_unique<media_query_list_state>();
        state->query = query;
        state->matches = self->media_query_matches(query);
        state->context.Reset(isolate, local_context);
        auto* state_pointer = state.get();
        auto data = v8::External::New(
            isolate,
            state_pointer,
            v8::kExternalPointerTypeTagDefault);
        result->Set(
            local_context,
            js_string(isolate, "matches"),
            v8::Boolean::New(isolate, state->matches)).Check();
        result->Set(local_context, js_string(isolate, "media"), info.Length() > 0 ? info[0] : v8::Local<v8::Value>(js_string(isolate, ""))).Check();
        result->Set(
            local_context,
            js_string(isolate, "addEventListener"),
            v8::Function::New(
                local_context,
                media_query_list_add_event_listener,
                data).ToLocalChecked()).Check();
        result->Set(
            local_context,
            js_string(isolate, "removeEventListener"),
            v8::Function::New(
                local_context,
                media_query_list_remove_event_listener,
                data).ToLocalChecked()).Check();
        result->Set(
            local_context,
            js_string(isolate, "addListener"),
            v8::Function::New(
                local_context,
                media_query_list_add_listener,
                data).ToLocalChecked()).Check();
        result->Set(
            local_context,
            js_string(isolate, "removeListener"),
            v8::Function::New(
                local_context,
                media_query_list_remove_listener,
                data).ToLocalChecked()).Check();
        result->Set(
            local_context,
            js_string(isolate, "onchange"),
            v8::Null(isolate)).Check();
        state->wrapper.Reset(isolate, result);
        self->media_query_lists.push_back(std::move(state));
        info.GetReturnValue().Set(result);
    }

    bool set_visible(bool visible)
    {
        if (document_visible == visible) return true;
        document_visible = visible;
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = context.Get(isolate);
        if (local_context.IsEmpty()) return true;
        v8::Context::Scope context_scope(local_context);
        auto target = document_object.Get(isolate);
        if (target.IsEmpty()) return true;
        auto event = create_event_instance(local_context);
        event->Set(
            local_context,
            js_string(isolate, "type"),
            js_string(isolate, "visibilitychange")).Check();
        event->Set(
            local_context,
            js_string(isolate, "bubbles"),
            v8::False(isolate)).Check();
        event->Set(
            local_context,
            js_string(isolate, "cancelable"),
            v8::False(isolate)).Check();
        v8::Local<v8::Value> dispatcher;
        if (!target->Get(
                local_context,
                js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
            || !dispatcher->IsFunction()) {
            last_error = "Document visibilitychange dispatcher is unavailable";
            return false;
        }
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Value> arguments[] = {event};
        if (dispatcher.As<v8::Function>()->Call(
                local_context,
                target,
                1,
                arguments).IsEmpty()) {
            last_error = "Document visibilitychange dispatch failed: "
                + describe_exception(try_catch, local_context);
            ++frame_script_error_count;
            return false;
        }
        perform_microtask_checkpoint();
        return true;
    }

    bool dispatch_transition_events()
    {
        auto events = document.take_transition_events();
        if (events.empty()) return true;
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = frame_context.IsEmpty()
            ? context.Get(isolate)
            : frame_context.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        v8::TryCatch try_catch(isolate);
        for (const auto& transition : events) {
            auto* node = document.find_by_native_id(transition.node_id);
            if (node == nullptr) continue;
            auto target = wrap_node(*node);
            auto event = v8::Object::New(isolate);
            event->Set(local_context, js_string(isolate, "type"),
                js_string(isolate, transition.type.c_str())).Check();
            event->Set(local_context, js_string(isolate, "bubbles"), v8::True(isolate)).Check();
            event->Set(local_context, js_string(isolate, "cancelable"), v8::False(isolate)).Check();
            event->Set(local_context, js_string(isolate, "propertyName"),
                js_string(isolate, transition.property_name.c_str())).Check();
            event->Set(local_context, js_string(isolate, "elapsedTime"),
                v8::Number::New(isolate, transition.elapsed_time_seconds)).Check();
            event->Set(local_context, js_string(isolate, "pseudoElement"),
                js_string(isolate, "")).Check();
            v8::Local<v8::Value> dispatcher;
            if (!target->Get(local_context, js_string(isolate, "dispatchEvent"))
                    .ToLocal(&dispatcher)
                || !dispatcher->IsFunction()) continue;
            v8::Local<v8::Value> arguments[] = {event};
            if (dispatcher.As<v8::Function>()->Call(
                    local_context, target, 1, arguments).IsEmpty()) {
                last_error = "CSS transition-event dispatch failed: "
                    + describe_exception(try_catch, local_context);
                ++frame_script_error_count;
                return false;
            }
        }
        perform_microtask_checkpoint();
        return true;
    }

    static void empty_array(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
    }

    static void text_encoder_encode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        const auto value = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto buffer = v8::ArrayBuffer::New(info.GetIsolate(), value.size());
        if (!value.empty()) {
            std::memcpy(buffer->GetBackingStore()->Data(), value.data(), value.size());
        }
        info.GetReturnValue().Set(v8::Uint8Array::New(buffer, 0, value.size()));
    }

    static void dispatch_event(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom",
            "method:EventTarget.dispatchEvent",
            info.Length() >= 1 && info[0]->IsObject() ? "supported" : "invalid-authoring",
            info.Length() >= 1 && info[0]->IsObject()
                ? std::string{} : "an Event object is required",
            "dom-binding");
        if (info.Length() < 1 || !info[0]->IsObject()) return;
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto event = info[0].As<v8::Object>();
        v8::Local<v8::Value> type_value;
        if (!event->Get(local_context, js_string(info.GetIsolate(), "type")).ToLocal(&type_value)) return;
        const auto type = to_utf8(info.GetIsolate(), type_value);
        self->record_feature(
            "event",
            "type:" + type,
            "supported",
            {},
            "event-dispatch");
        auto receiver = info.This()->IsObject()
            ? info.This().As<v8::Object>()
            : v8::Local<v8::Object> {};
        const auto receiver_is_document = !receiver.IsEmpty()
            && self->is_document_object(receiver);
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        const auto standalone_target_id = receiver.IsEmpty()
            ? std::optional<uint32_t>{}
            : generated_standalone_event_target_id(info.GetIsolate(), receiver);
#endif
        auto* target = receiver.IsEmpty() || receiver_is_document
            ? nullptr
            : unwrap_node(receiver);
        auto target_value = target != nullptr
            ? self->wrap_node(*target)
            : receiver_is_document
                ? receiver
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
                : standalone_target_id.has_value()
                    ? receiver
#endif
                : v8::Local<v8::Object>(local_context->Global());
        event->Set(local_context, js_string(info.GetIsolate(), "target"), target_value).Check();
        event->Set(local_context, js_string(info.GetIsolate(), "srcElement"), target_value).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "__webscenePropagationStopped"),
            v8::False(info.GetIsolate())).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "__websceneImmediatePropagationStopped"),
            v8::False(info.GetIsolate())).Check();
        const auto read_boolean = [&](const char* name) {
            v8::Local<v8::Value> value;
            return event->Get(local_context, js_string(info.GetIsolate(), name)).ToLocal(&value)
                && value->BooleanValue(info.GetIsolate());
        };
        const auto propagation_stopped = [&]() {
            return read_boolean("__webscenePropagationStopped");
        };
        const auto immediate_propagation_stopped = [&]() {
            return read_boolean("__websceneImmediatePropagationStopped");
        };
        const auto retargeted_node = [&](const dom_node* current_target) {
            if (target == nullptr) return static_cast<dom_node*>(nullptr);
            const auto* root = self->document.containing_shadow_root(*target);
            if (root == nullptr) return target;
            if (current_target != nullptr
                && self->document.containing_shadow_root(*current_target) == root) {
                return target;
            }
            const auto* state = self->document.shadow_dom(*root);
            return state == nullptr || state->host == nullptr ? target : state->host;
        };
        const auto set_retargeted_target = [&](const dom_node* current_target) {
            if (target == nullptr) return;
            auto wrapper = self->wrap_node(*retargeted_node(current_target));
            event->Set(
                local_context, js_string(info.GetIsolate(), "target"), wrapper).Check();
            event->Set(
                local_context, js_string(info.GetIsolate(), "srcElement"), wrapper).Check();
        };
        const auto set_phase = [&](v8::Local<v8::Object> current_target, int phase) {
            event->Set(
                local_context,
                js_string(info.GetIsolate(), "currentTarget"),
                current_target).Check();
            event->Set(
                local_context,
                js_string(info.GetIsolate(), "eventPhase"),
                v8::Integer::New(info.GetIsolate(), phase)).Check();
        };
        const auto listeners_at_dispatch_start =
            self->frame_event_listeners.find(type);
        const auto listener_count_at_dispatch_start =
            listeners_at_dispatch_start == self->frame_event_listeners.end()
                ? 0U
                : listeners_at_dispatch_start->second.size();
        v8::Local<v8::Value> arguments[] = {event};
        const auto invoke_callback = [&](
            v8::Local<v8::Value> callback,
            v8::Local<v8::Object> current_target) {
            v8::TryCatch try_catch(info.GetIsolate());
            if (!call_event_listener(
                    info.GetIsolate(),
                    local_context,
                    callback,
                    current_target,
                    1,
                    arguments).IsEmpty()) {
                return true;
            }

            self->frame_last_error_value = "Uncaught exception in '" + type
                + "' event listener: "
                + self->describe_exception(try_catch, local_context);
            ++self->frame_script_error_count;
            {
                std::lock_guard lock(self->console_message_mutex);
                constexpr size_t maximum_console_messages = 1024U;
                if (self->console_messages.size() >= maximum_console_messages) {
                    self->console_messages.pop_front();
                }
                self->console_messages.push_back(
                    "error\n" + self->frame_last_error_value);
            }
            return false;
        };
        const auto invoke_listeners = [&] (
            uint32_t target_id,
            v8::Local<v8::Object> current_target,
            bool capture,
            int phase) {
            auto listeners = self->frame_event_listeners.find(type);
            if (listeners == self->frame_event_listeners.end()) return true;
            set_retargeted_target(self->document.find_by_native_id(target_id));
            set_phase(current_target, phase);
            // Listener eligibility is fixed when dispatch starts. A listener
            // installed by an overlay anchor must not observe that same click.
            const auto listener_count = std::min(
                listener_count_at_dispatch_start,
                listeners->second.size());
            for (size_t index = 0; index < listener_count; ++index) {
                auto& listener = listeners->second[index];
                if (listener.context.Get(info.GetIsolate()) != local_context
                    || listener.target != target_id
                    || listener.capture != capture) continue;
                auto callback = listener.callback.Get(info.GetIsolate());
                if (callback.IsEmpty()) continue;
                if (!listener.signal.IsEmpty()) {
                    auto signal = listener.signal.Get(info.GetIsolate());
                    v8::Local<v8::Value> aborted;
                    if (signal->Get(
                            local_context,
                            js_string(info.GetIsolate(), "aborted")).ToLocal(&aborted)
                        && aborted->BooleanValue(info.GetIsolate())) {
                        listener.callback.Reset();
                        listener.signal.Reset();
                        continue;
                    }
                }
                const auto passive = listener.passive;
                if (listener.once) {
                    listener.callback.Reset();
                    listener.signal.Reset();
                }
                event->Set(
                    local_context,
                    js_string(info.GetIsolate(), "__websceneInPassiveListener"),
                    v8::Boolean::New(info.GetIsolate(), passive)).Check();
                invoke_callback(callback, current_target);
                event->Set(
                    local_context,
                    js_string(info.GetIsolate(), "__websceneInPassiveListener"),
                    v8::False(info.GetIsolate())).Check();
                if (immediate_propagation_stopped()) break;
            }
            return true;
        };
        const auto property_name = std::string("on") + type;
        const auto invoke_property = [&](dom_node& node, int phase) {
            auto wrapper = self->wrap_node(node);
            v8::Local<v8::Value> handler;
            if (!wrapper->Get(
                    local_context,
                    js_string(info.GetIsolate(), property_name.c_str())).ToLocal(&handler)
                || !handler->IsFunction()) return true;
            set_retargeted_target(&node);
            set_phase(wrapper, phase);
            invoke_callback(handler.As<v8::Function>(), wrapper);
            return true;
        };

        auto global = local_context->Global();
        auto document_target = global;
        v8::Local<v8::Value> document_value;
        if (global->Get(
                local_context,
                js_string(info.GetIsolate(), "document")).ToLocal(&document_value)
            && document_value->IsObject()) {
            document_target = document_value.As<v8::Object>();
        }
        const auto invoke_outer_resize_listeners = [&](bool capture) {
            if (type != "resize" || self->in_frame_context() || receiver.IsEmpty()
                || !receiver->StrictEquals(global)) return true;
            set_phase(global, 2);
            const auto listener_count = self->resize_listeners.size();
            for (size_t index = 0; index < listener_count; ++index) {
                auto& listener = self->resize_listeners[index];
                if (listener.context.Get(info.GetIsolate()) != local_context
                    || listener.capture != capture) continue;
                auto callback = listener.callback.Get(info.GetIsolate());
                if (callback.IsEmpty()) continue;
                if (!listener.signal.IsEmpty()) {
                    auto signal = listener.signal.Get(info.GetIsolate());
                    v8::Local<v8::Value> aborted;
                    if (signal->Get(
                            local_context,
                            js_string(info.GetIsolate(), "aborted")).ToLocal(&aborted)
                        && aborted->BooleanValue(info.GetIsolate())) {
                        listener.callback.Reset();
                        listener.signal.Reset();
                        continue;
                    }
                }
                const auto passive = listener.passive;
                if (listener.once) {
                    listener.callback.Reset();
                    listener.signal.Reset();
                }
                event->Set(
                    local_context,
                    js_string(info.GetIsolate(), "__websceneInPassiveListener"),
                    v8::Boolean::New(info.GetIsolate(), passive)).Check();
                const auto callback_succeeded = invoke_callback(callback, global);
                event->Set(
                    local_context,
                    js_string(info.GetIsolate(), "__websceneInPassiveListener"),
                    v8::False(info.GetIsolate())).Check();
                if (!callback_succeeded) return false;
                if (immediate_propagation_stopped()) break;
            }
            return true;
        };
        if (target == nullptr) {
            const auto direct_target_id = receiver_is_document
                ? std::numeric_limits<uint32_t>::max()
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
                : standalone_target_id.value_or(0U);
#else
                : 0U;
#endif
            const auto direct_target = receiver_is_document
                ? document_target
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
                : standalone_target_id.has_value() ? receiver : global;
#else
                : global;
#endif
            if (receiver_is_document
                && !invoke_listeners(0U, global, true, 1)) return;
            if (!propagation_stopped()
                && (!invoke_listeners(direct_target_id, direct_target, true, 2)
                || !invoke_outer_resize_listeners(true)
                || (!immediate_propagation_stopped()
                    && (!invoke_listeners(direct_target_id, direct_target, false, 2)
                        || !invoke_outer_resize_listeners(false))))) return;
            if (receiver_is_document
                && read_boolean("bubbles")
                && !propagation_stopped()
                && !invoke_listeners(0U, global, false, 3)) return;
            if (!receiver_is_document && !immediate_propagation_stopped()) {
                v8::Local<v8::Value> handler;
                if (direct_target->Get(
                        local_context,
                        js_string(info.GetIsolate(), property_name.c_str())).ToLocal(&handler)
                    && handler->IsFunction()) {
                    set_phase(direct_target, 2);
                    invoke_callback(handler.As<v8::Function>(), direct_target);
                }
            }
        } else {
            std::vector<dom_node*> ancestry;
            const auto composed = read_boolean("composed");
            for (auto* node = target; node != nullptr;
                node = self->document.event_parent(*node)) {
                ancestry.push_back(node);
                if (!composed && self->document.is_shadow_root(*node)) break;
            }
            const auto reaches_document = !ancestry.empty()
                && ancestry.back() == &self->document.body();
            auto composed_path = v8::Array::New(
                info.GetIsolate(),
                static_cast<int>(ancestry.size() + (reaches_document ? 2U : 0U)));
            for (uint32_t index = 0; index < ancestry.size(); ++index) {
                composed_path->Set(
                    local_context, index, self->wrap_node(*ancestry[index])).Check();
            }
            if (reaches_document) {
                composed_path->Set(
                    local_context,
                    static_cast<uint32_t>(ancestry.size()),
                    document_target).Check();
                composed_path->Set(
                    local_context,
                    static_cast<uint32_t>(ancestry.size() + 1U),
                    global).Check();
            }
            event->Set(
                local_context,
                js_string(info.GetIsolate(), "__webSceneComposedPath"),
                composed_path).Check();
            if (reaches_document && !invoke_listeners(0U, global, true, 1)) return;
            if (reaches_document && !invoke_listeners(
                    std::numeric_limits<uint32_t>::max(),
                    document_target,
                    true,
                    1)) return;
            for (size_t cursor = ancestry.size(); !propagation_stopped() && cursor-- > 1;) {
                auto* node = ancestry[cursor];
                if (!invoke_listeners(node->id, self->wrap_node(*node), true, 1)) return;
            }
            if (!propagation_stopped()) {
                if (!invoke_listeners(target->id, target_value, true, 2)) return;
                if (!immediate_propagation_stopped()
                    && !invoke_listeners(target->id, target_value, false, 2)) return;
                if (!immediate_propagation_stopped() && !invoke_property(*target, 2)) return;
            }
            if (read_boolean("bubbles") && !propagation_stopped()) {
                for (size_t cursor = 1; cursor < ancestry.size() && !propagation_stopped(); ++cursor) {
                    auto* node = ancestry[cursor];
                    auto wrapper = self->wrap_node(*node);
                    if (!invoke_listeners(node->id, wrapper, false, 3)
                        || (!immediate_propagation_stopped() && !invoke_property(*node, 3))) return;
                }
                if (reaches_document && !propagation_stopped()
                    && !invoke_listeners(
                        std::numeric_limits<uint32_t>::max(),
                        document_target,
                        false,
                        3)) return;
                if (reaches_document && !propagation_stopped()
                    && !invoke_listeners(0U, global, false, 3)) return;
            }
        }
        if (type == "innerWindowLoad") {
            auto received = false;
            for (auto& [node_id, listener] : self->frame_load_listeners) {
                static_cast<void>(node_id);
                auto owner_context = self->context.Get(info.GetIsolate());
                {
                    v8::Context::Scope owner_scope(owner_context);
                    auto detail = v8::Object::New(info.GetIsolate());
                    detail->Set(owner_context, js_string(info.GetIsolate(), "received"), v8::False(info.GetIsolate())).Check();
                    auto owner_event = v8::Object::New(info.GetIsolate());
                    owner_event->Set(owner_context, js_string(info.GetIsolate(), "detail"), detail).Check();
                    v8::Local<v8::Value> arguments[] = {owner_event};
                    auto callback = listener.Get(info.GetIsolate());
                    if (!callback.IsEmpty()) {
                        callback->Call(owner_context, owner_context->Global(), 1, arguments).ToLocalChecked();
                    }
                    v8::Local<v8::Value> received_value;
                    if (detail->Get(
                            owner_context,
                            js_string(info.GetIsolate(), "received")).ToLocal(&received_value)) {
                        received = received || received_value->BooleanValue(info.GetIsolate());
                    }
                }
            }
            if (received) {
                v8::Context::Scope source_scope(local_context);
                v8::Local<v8::Value> detail_value;
                if (event->Get(
                        local_context,
                        js_string(info.GetIsolate(), "detail")).ToLocal(&detail_value)
                    && detail_value->IsObject()) {
                    detail_value.As<v8::Object>()->Set(
                        local_context,
                        js_string(info.GetIsolate(), "received"),
                        v8::True(info.GetIsolate())).Check();
                }
            }
        }
        if (auto listeners = self->frame_event_listeners.find(type);
            listeners != self->frame_event_listeners.end()) {
            std::erase_if(
                listeners->second,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
        }
        if (type == "resize") {
            std::erase_if(
                self->resize_listeners,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
        }
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "currentTarget"),
            v8::Null(info.GetIsolate())).Check();
        event->Set(
            local_context,
            js_string(info.GetIsolate(), "eventPhase"),
            v8::Integer::New(info.GetIsolate(), 0)).Check();
        info.GetReturnValue().Set(
            read_boolean("defaultPrevented")
                ? v8::False(info.GetIsolate())
                : v8::True(info.GetIsolate()));
    }

    static std::string base64_encode_bytes(const uint8_t* bytes, size_t length)
    {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        encoded.reserve(((length + 2U) / 3U) * 4U);
        for (size_t index = 0; index < length; index += 3U) {
            const auto remaining = length - index;
            const auto value = (static_cast<uint32_t>(bytes[index]) << 16U)
                | (remaining > 1U ? static_cast<uint32_t>(bytes[index + 1U]) << 8U : 0U)
                | (remaining > 2U ? static_cast<uint32_t>(bytes[index + 2U]) : 0U);
            encoded.push_back(alphabet[(value >> 18U) & 0x3FU]);
            encoded.push_back(alphabet[(value >> 12U) & 0x3FU]);
            encoded.push_back(remaining > 1U ? alphabet[(value >> 6U) & 0x3FU] : '=');
            encoded.push_back(remaining > 2U ? alphabet[value & 0x3FU] : '=');
        }
        return encoded;
    }

    static void write_clipboard(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (self == nullptr || info.Length() < 2 || !info[1]->IsObject()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        const auto content_type = to_utf8(info.GetIsolate(), info[0]);
        if (content_type.empty()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        auto payload = info[1].As<v8::Object>();
        auto request = v8::Object::New(info.GetIsolate());
        request->Set(
            local_context,
            js_string(info.GetIsolate(), "kind"),
            js_string(info.GetIsolate(), "writeClipboard")).Check();
        request->Set(
            local_context,
            js_string(info.GetIsolate(), "contentType"),
            js_string(info.GetIsolate(), content_type.c_str())).Check();

        v8::Local<v8::Value> canvas_node_id;
        if (payload->Get(
                local_context,
                js_string(info.GetIsolate(), "_canvasNodeId")).ToLocal(&canvas_node_id)
            && canvas_node_id->IsUint32()) {
            request->Set(
                local_context,
                js_string(info.GetIsolate(), "canvasNodeId"),
                canvas_node_id).Check();
        } else {
            v8::Local<v8::Value> bytes_value;
            if (!payload->Get(
                    local_context,
                    js_string(info.GetIsolate(), "_bytes")).ToLocal(&bytes_value)
                || !bytes_value->IsArrayBufferView()) {
                info.GetReturnValue().Set(v8::False(info.GetIsolate()));
                return;
            }
            auto view = bytes_value.As<v8::ArrayBufferView>();
            auto backing = view->Buffer()->GetBackingStore();
            const auto* bytes = static_cast<const uint8_t*>(backing->Data())
                + view->ByteOffset();
            const auto data_url = "data:" + content_type + ";base64,"
                + base64_encode_bytes(bytes, view->ByteLength());
            request->Set(
                local_context,
                js_string(info.GetIsolate(), "url"),
                js_string(info.GetIsolate(), data_url.c_str())).Check();
        }
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(), self->enqueue_host_request(local_context, request)));
    }

    static void create_object_url(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto payload = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        std::string download_payload;
        uint32_t canvas_node_id = 0;
        if (info.Length() > 0 && info[0]->IsObject()) {
            auto local_context = info.GetIsolate()->GetCurrentContext();
            auto object = info[0].As<v8::Object>();
            v8::Local<v8::Value> canvas_node_id_value;
            if (object->Get(
                    local_context,
                    js_string(info.GetIsolate(), "_canvasNodeId"))
                    .ToLocal(&canvas_node_id_value)
                && canvas_node_id_value->IsUint32()) {
                canvas_node_id = canvas_node_id_value.As<v8::Uint32>()->Value();
            }
            v8::Local<v8::Value> bytes_value;
            if (object->Get(
                    local_context,
                    js_string(info.GetIsolate(), "_bytes")).ToLocal(&bytes_value)
                && bytes_value->IsArrayBufferView()) {
                auto view = bytes_value.As<v8::ArrayBufferView>();
                auto backing = view->Buffer()->GetBackingStore();
                const auto* bytes = static_cast<const uint8_t*>(backing->Data())
                    + view->ByteOffset();
                v8::Local<v8::Value> type_value;
                const auto type = object->Get(
                        local_context,
                        js_string(info.GetIsolate(), "type")).ToLocal(&type_value)
                    ? to_utf8(info.GetIsolate(), type_value)
                    : std::string{};
                const auto is_textual = type.starts_with("text/")
                    || type == "application/xhtml+xml"
                    || type == "application/xml"
                    || type == "image/svg+xml";
                if (is_textual) {
                    payload.assign(
                        reinterpret_cast<const char*>(bytes),
                        view->ByteLength());
                }
                download_payload = "data:"
                    + (type.empty() ? std::string{"application/octet-stream"} : type)
                    + ";base64," + base64_encode_bytes(bytes, view->ByteLength());
            }
        }
        const auto url = "blob:webscene-native/" + std::to_string(self->next_object_url_id++);
        self->object_urls[url] = std::move(payload);
        if (!download_payload.empty()) {
            self->object_url_download_payloads[url] = std::move(download_payload);
        }
        if (canvas_node_id != 0) {
            self->object_url_canvas_node_ids[url] = canvas_node_id;
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), url.c_str()));
    }

    static void resolve_url(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto value = info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : std::string{};
        auto base = info.Length() > 1
            ? to_utf8(info.GetIsolate(), info[1])
            : std::string{};
        if (base.empty()) base = self->document_base_address;
        const auto resolved = resolve_resource_url(value, base);
        info.GetReturnValue().Set(js_string(info.GetIsolate(), resolved.c_str()));
    }

    static void get_inner_width(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Window.innerWidth.get"));
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            self->viewport_provider();
        static_cast<void>(height);
        static_cast<void>(device_scale_factor);
        static_cast<void>(preferred_color_scheme);
        info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), width));
    }

    static void get_inner_height(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Window.innerHeight.get"));
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            self->viewport_provider();
        static_cast<void>(width);
        static_cast<void>(device_scale_factor);
        static_cast<void>(preferred_color_scheme);
        info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), height));
    }

    static dom_node* frame_owner_for_window(
        implementation& self,
        v8::Local<v8::Object> holder)
    {
        if (auto* node = unwrap_node(holder); node != nullptr && node->tag == "iframe") {
            return node;
        }
        for (const auto& [id, persistent] : self.actual_frame_windows) {
            if (!persistent.IsEmpty() && persistent.Get(self.isolate)->StrictEquals(holder)) {
                auto* node = self.document.find_by_native_id(id);
                return node != nullptr && node->tag == "iframe" ? node : nullptr;
            }
        }
        return nullptr;
    }

    static void get_window_scroll_x(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Window.scrollX.get"));
        const auto* frame = frame_owner_for_window(*self, info.Holder());
        const auto known = frame == nullptr
            ? self->frame_window_scroll_offsets.end()
            : self->frame_window_scroll_offsets.find(frame->id);
        info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(),
            known == self->frame_window_scroll_offsets.end()
                ? self->active_root().scroll_left
                : known->second.first));
    }

    static void get_window_scroll_y(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Window.scrollY.get"));
        const auto* frame = frame_owner_for_window(*self, info.Holder());
        const auto known = frame == nullptr
            ? self->frame_window_scroll_offsets.end()
            : self->frame_window_scroll_offsets.find(frame->id);
        info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(),
            known == self->frame_window_scroll_offsets.end()
                ? self->active_root().scroll_top
                : known->second.second));
    }

    static void window_scroll_to(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Window.scrollTo"));
        self->ensure_layout();

        auto& root = self->active_root();
        auto* frame = frame_owner_for_window(*self, info.This());
        const auto known = frame == nullptr
            ? self->frame_window_scroll_offsets.end()
            : self->frame_window_scroll_offsets.find(frame->id);
        auto left = static_cast<double>(known == self->frame_window_scroll_offsets.end()
            ? root.scroll_left : known->second.first);
        auto top = static_cast<double>(known == self->frame_window_scroll_offsets.end()
            ? root.scroll_top : known->second.second);
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        if (info.Length() > 0 && info[0]->IsObject()) {
            const auto options = info[0].As<v8::Object>();
            const auto read_axis = [&](const char* name, double current_value) {
                v8::Local<v8::Value> value;
                if (!options->Get(local_context, js_string(info.GetIsolate(), name))
                        .ToLocal(&value)
                    || value->IsUndefined()) {
                    return current_value;
                }
                return value->NumberValue(local_context).FromMaybe(0.0);
            };
            left = read_axis("left", left);
            top = read_axis("top", top);
        } else {
            left = info.Length() > 0
                ? info[0]->NumberValue(local_context).FromMaybe(0.0)
                : 0.0;
            top = info.Length() > 1
                ? info[1]->NumberValue(local_context).FromMaybe(0.0)
                : 0.0;
        }
        if (!std::isfinite(left)) left = 0.0;
        if (!std::isfinite(top)) top = 0.0;

        if (frame != nullptr) {
            self->frame_window_scroll_offsets[frame->id] = {
                std::max(0.0F, static_cast<float>(left)),
                std::max(0.0F, static_cast<float>(top))};
            return;
        }

        const auto maximum_left = std::max(
            0.0F,
            root.scroll_content_width - root.scroll_viewport_width);
        const auto maximum_top = std::max(
            0.0F,
            root.scroll_content_height - root.scroll_viewport_height);
        const auto next_left = std::clamp(static_cast<float>(left), 0.0F, maximum_left);
        const auto next_top = std::clamp(static_cast<float>(top), 0.0F, maximum_top);
        if (std::abs(next_left - root.scroll_left) <= 0.01F
            && std::abs(next_top - root.scroll_top) <= 0.01F) {
            return;
        }
        root.scroll_left = next_left;
        root.scroll_top = next_top;
        self->document.mark_dirty();
        self->enqueue_programmatic_scroll_event(root);
    }

    static void get_device_pixel_ratio(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Window.devicePixelRatio.get"));
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            self->viewport_provider();
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(preferred_color_scheme);
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            std::isfinite(device_scale_factor) && device_scale_factor > 0.0
                ? device_scale_factor
                : 1.0));
    }

    static void get_screen_width(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Screen.width.get"));
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            self->viewport_provider();
        static_cast<void>(height);
        static_cast<void>(device_scale_factor);
        static_cast<void>(preferred_color_scheme);
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            std::max(0.0F, width)));
    }

    static void get_screen_height(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Screen.height.get"));
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            self->viewport_provider();
        static_cast<void>(width);
        static_cast<void>(device_scale_factor);
        static_cast<void>(preferred_color_scheme);
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            std::max(0.0F, height)));
    }

    static void console_log(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.Data()->IsInt32()) return;
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        const auto level_index = std::clamp(info.Data().As<v8::Int32>()->Value(), 0, 2);
        constexpr std::array<std::string_view, 3> levels{"log", "warn", "error"};
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        const auto report_to_inspector =
            self->inspector_instrumentation_active();
        constexpr std::array<v8::Isolate::MessageErrorLevel, 3> inspector_levels{
            v8::Isolate::kMessageLog,
            v8::Isolate::kMessageWarning,
            v8::Isolate::kMessageError};
        std::vector<v8::Local<v8::Value>> arguments;
        if (report_to_inspector) {
            arguments.reserve(static_cast<size_t>(info.Length()));
        }
#endif
        std::ostringstream message;
        for (int index = 0; index < info.Length(); ++index) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            if (report_to_inspector) arguments.push_back(info[index]);
#endif
            auto rendered = to_utf8(isolate, info[index]);
            if (info[index]->IsObject()) {
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::Value> stack;
                if (info[index].As<v8::Object>()
                        ->Get(local_context, js_string(isolate, "stack"))
                        .ToLocal(&stack)
                    && stack->IsString()) {
                    const auto stack_text = to_utf8(isolate, stack);
                    if (!stack_text.empty() && stack_text != rendered) {
                        rendered += "\n" + stack_text;
                    }
                }
            }
            message << (index == 0 ? "" : " | ") << rendered;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        if (report_to_inspector) {
            self->report_inspector_console_message(
                local_context,
                inspector_levels[static_cast<size_t>(level_index)],
                arguments);
        }
#endif
        const auto payload = std::string(levels[static_cast<size_t>(level_index)])
            + '\n' + message.str();
        {
            std::lock_guard lock(self->console_message_mutex);
            constexpr size_t maximum_console_messages = 1024U;
            if (self->console_messages.size() >= maximum_console_messages) {
                self->console_messages.pop_front();
            }
            self->console_messages.push_back(payload);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (std::getenv("WEBSCENE_PROBE_CONSOLE") != nullptr) {
            std::cerr << "[V8 console." << levels[static_cast<size_t>(level_index)]
                << "] " << message.str() << '\n';
        }
#endif
    }
