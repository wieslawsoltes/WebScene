    bool promote_pending_promise_error()
    {
        if (pending_promise_rejections.empty()) return true;
        last_error = std::move(pending_promise_rejections.front().error);
        pending_promise_rejections.clear();
        return false;
    }

    void pump_v8_platform_tasks()
    {
        constexpr uint32_t maximum_tasks = 8U;
        constexpr auto maximum_time = std::chrono::milliseconds(1);
        const auto started = std::chrono::steady_clock::now();
        uint32_t count = 0U;
        while (count < maximum_tasks
            && std::chrono::steady_clock::now() - started < maximum_time
            && v8::platform::PumpMessageLoop(
                v8_platform.get(),
                isolate,
                v8::platform::MessageLoopBehavior::kDoNotWait)) {
            ++count;
        }
    }

    void perform_microtask_checkpoint()
    {
        isolate->PerformMicrotaskCheckpoint();
        if (work_metrics_enabled.load(std::memory_order_relaxed)) {
            ++work.microtask_checkpoints;
        }
    }

    bool execute_timer_task(
        timer_task task,
        std::chrono::steady_clock::time_point now_time)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            if (task.animation_frame) ++startup_raf_executed;
            else ++startup_timer_executed;
        }
#endif
        auto task_context = task.context.Get(isolate);
        if (task_context.IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            inspector_async_task_canceled(task.id);
#endif
            return true;
        }
        v8::Context::Scope task_context_scope(task_context);
        auto callback = task.callback.Get(isolate);
        if (callback.IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            inspector_async_task_canceled(task.id);
#endif
            return true;
        }
        const auto collect_work_metrics =
            work_metrics_enabled.load(std::memory_order_relaxed);
        if (collect_work_metrics && task.animation_frame) {
            ++work.animation_frames_invoked;
        } else if (collect_work_metrics) {
            ++work.timers_fired;
            constexpr auto late_threshold = std::chrono::milliseconds(1);
            if (now_time > task.deadline + late_threshold) {
                ++work.late_timers;
                work.total_timer_lateness_nanoseconds +=
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            now_time - task.deadline).count());
            }
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        auto task_profile_name = std::string{};
        auto task_profile_started = std::chrono::steady_clock::time_point{};
        std::array<binding_callback_stats, binding_category_count>
            task_binding_profile_before{};
        if (profile_startup) {
            const auto origin = callback->GetScriptOrigin().ResourceName();
            task_profile_name = origin.IsEmpty() || origin->IsUndefined()
                ? std::string("<anonymous>")
                : resource_leaf_name(to_utf8(isolate, origin));
            auto function_name = to_utf8(isolate, callback->GetName());
            if (function_name.empty()) {
                function_name = to_utf8(isolate, callback->GetInferredName());
            }
            if (!function_name.empty()) task_profile_name += ':' + function_name;
            else {
                const auto line = callback->GetScriptLineNumber();
                const auto column = callback->GetScriptColumnNumber();
                if (line >= 0) {
                    task_profile_name += ':' + std::to_string(line + 1);
                    if (column >= 0) task_profile_name += ':' + std::to_string(column + 1);
                }
            }
            task_profile_started = std::chrono::steady_clock::now();
        }
        if (profile_bindings) {
            task_binding_profile_before = binding_totals;
        }
#endif
        v8::TryCatch try_catch(isolate);
        std::vector<v8::Local<v8::Value>> arguments;
        if (task.animation_frame) {
            const auto timestamp = task.animation_timestamp_ms >= 0
                ? task.animation_timestamp_ms
                : std::chrono::duration<double, std::milli>(
                    now_time.time_since_epoch()).count();
            arguments.push_back(v8::Number::New(isolate, timestamp));
        } else {
            arguments.reserve(task.arguments.size());
            for (auto& argument : task.arguments) {
                arguments.push_back(argument.Get(isolate));
            }
        }
        active_timer_id = task.id;
        active_timer_cancelled = false;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        inspector_async_task_started(task.id);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer callback_timer(
            profile_startup ? &startup_task_callbacks : nullptr);
#endif
        if (callback->Call(
                task_context,
                task_context->Global(),
                static_cast<int>(arguments.size()),
                arguments.empty() ? nullptr : arguments.data()).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, task_context);
            inspector_async_task_finished(task.id);
            if (task.repeating) inspector_async_task_canceled(task.id);
#endif
            active_timer_id = 0;
            last_error = std::string(task.animation_frame
                    ? "requestAnimationFrame task failed: "
                    : "timer task failed: ")
                + describe_exception(try_catch, task_context);
            return false;
        }
        perform_microtask_checkpoint();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        inspector_async_task_finished(task.id);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            auto& stats = startup_task_profiles[task_profile_name];
            ++stats.calls;
            stats.nanoseconds += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - task_profile_started).count());
            if (std::getenv("WEBSCENE_PROBE_TRACE_STARTUP_TASKS") != nullptr) {
                std::cerr << "[Native Engine Probe] startup-task t=" << std::fixed
                    << std::setprecision(1)
                    << std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - startup_profile_started).count()
                    << "ms elapsed=" << std::fixed << std::setprecision(3)
                    << std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - task_profile_started).count()
                    << "ms kind=" << (task.animation_frame ? "raf" : "timer")
                    << " delay=" << task.interval.count() << "ms"
                    << " name=" << task_profile_name;
                if (profile_bindings) {
                    std::cerr << " bindings=[";
                    auto wrote_binding = false;
                    for (size_t index = 0;
                        index < binding_category_count;
                        ++index) {
                        const auto calls =
                            binding_totals[index].calls
                                - task_binding_profile_before[index].calls;
                        const auto nanoseconds =
                            binding_totals[index].nanoseconds
                                - task_binding_profile_before[index].nanoseconds;
                        if (calls == 0) continue;
                        if (wrote_binding) std::cerr << ',';
                        wrote_binding = true;
                        std::cerr << binding_category_names[index]
                            << ":c" << calls << "/ms" << std::fixed
                            << std::setprecision(3)
                            << nanoseconds / 1'000'000.0;
                    }
                    std::cerr << ']';
                }
                std::cerr << '\n';
            }
        }
#endif
        const auto cancelled = active_timer_cancelled;
        active_timer_id = 0;
        active_timer_cancelled = false;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        if (cancelled && task.repeating) {
            inspector_async_task_canceled(task.id);
        }
#endif
        if (task.repeating && !cancelled) {
            const auto current = std::chrono::steady_clock::now();
            do {
                task.deadline +=
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        task.interval);
            } while (task.deadline <= current);
            timers.push_back(std::move(task));
        }
        return true;
    }

    bool drain_connected_resource_task()
    {
        if (connected_resources.empty()) return true;
        prefetch_connected_resources();
        prefer_timer_task = true;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) ++startup_connected_resources;
#endif
        auto resource = std::move(connected_resources.front());
        connected_resources.erase(connected_resources.begin());
        auto resource_context = resource.context.Get(isolate);
        auto wrapper = resource.wrapper.Get(isolate);
        if (resource.node == nullptr
            || resource_context.IsEmpty()
            || wrapper.IsEmpty()) {
            return true;
        }
        v8::Context::Scope resource_scope(resource_context);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace_resource = profile_startup
            && std::getenv("WEBSCENE_PROBE_TRACE_STARTUP_TASKS") != nullptr;
#else
        constexpr auto trace_resource = false;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto resource_started = trace_resource
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        auto resource_name = std::string{};
        if (trace_resource) {
            const auto attribute_name =
                resource.node->tag == "script" ? "src" : "href";
            if (const auto attribute =
                    resource.node->attributes.find(attribute_name);
                attribute != resource.node->attributes.end()) {
                resource_name = resource_leaf_name(attribute->second);
            }
        }
        if (!load_connected_resource(*resource.node, wrapper)) {
            last_error = frame_last_error_value;
            return false;
        }
        if (trace_resource) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto completed = std::chrono::steady_clock::now();
            std::cerr << "[Native Engine Probe] startup-resource t="
                << std::fixed << std::setprecision(1)
                << std::chrono::duration<double, std::milli>(
                    completed - startup_profile_started).count()
                << "ms elapsed="
                << std::chrono::duration<double, std::milli>(
                    completed - resource_started).count()
                << "ms kind=" << resource.node->tag
                << " name=" << resource_name << '\n';
#endif
        }
        perform_microtask_checkpoint();
        return true;
    }

    bool drain_window_message_task()
    {
        if (pending_window_messages.empty()) return true;
        auto task = std::move(pending_window_messages.front());
        pending_window_messages.pop_front();

        auto target_context = task.target_context.Get(isolate);
        auto source_context = task.source_context.Get(isolate);
        auto data = task.data.Get(isolate);
        if (target_context.IsEmpty() || source_context.IsEmpty() || data.IsEmpty()) {
            return true;
        }

        v8::Context::Scope target_scope(target_context);
        auto target_global = target_context->Global();
        v8::Local<v8::Value> location_value;
        v8::Local<v8::Value> origin_value;
        auto current_target_origin = std::string("null");
        if (target_global->Get(
                target_context,
                js_string(isolate, "location")).ToLocal(&location_value)
            && location_value->IsObject()
            && location_value.As<v8::Object>()->Get(
                target_context,
                js_string(isolate, "origin")).ToLocal(&origin_value)) {
            current_target_origin = to_utf8(isolate, origin_value);
        }
        if (!task.target_origin.empty()
            && task.target_origin != "*"
            && task.target_origin != current_target_origin) {
            return true;
        }

        auto event = v8::Object::New(isolate);
        event->Set(
            target_context,
            js_string(isolate, "type"),
            js_string(isolate, "message")).Check();
        event->Set(
            target_context,
            js_string(isolate, "data"),
            data).Check();
        event->Set(
            target_context,
            js_string(isolate, "origin"),
            js_string(isolate, task.source_origin.c_str())).Check();
        event->Set(
            target_context,
            js_string(isolate, "source"),
            source_context->Global()).Check();
        event->Set(
            target_context,
            js_string(isolate, "lastEventId"),
            js_string(isolate, "")).Check();
        event->Set(
            target_context,
            js_string(isolate, "ports"),
            v8::Array::New(isolate)).Check();
        event->Set(
            target_context,
            js_string(isolate, "bubbles"),
            v8::False(isolate)).Check();
        event->Set(
            target_context,
            js_string(isolate, "cancelable"),
            v8::False(isolate)).Check();

        v8::Local<v8::Value> dispatch_value;
        if (!target_global->Get(
                target_context,
                js_string(isolate, "dispatchEvent")).ToLocal(&dispatch_value)
            || !dispatch_value->IsFunction()) {
            return true;
        }
        v8::Local<v8::Value> arguments[] = {event};
        v8::TryCatch try_catch(isolate);
        if (dispatch_value.As<v8::Function>()->Call(
                target_context,
                target_global,
                1,
                arguments).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, target_context);
#endif
            last_error = "Window.postMessage dispatch failed: "
                + describe_exception(try_catch, target_context);
            ++frame_script_error_count;
            return false;
        }
        perform_microtask_checkpoint();
        return true;
    }

    bool drain_tasks()
    {
        collect_detached_dom_if_requested();
        // V8's default platform uses foreground tasks to finish background
        // compilation and optimization work. An embedder must service that
        // queue; otherwise hot application code remains on its initial tier.
        pump_v8_platform_tasks();
        if (websocket_transport.has_pending_events()) {
            return drain_websocket_event();
        }
        if (!pending_window_messages.empty()) {
            return drain_window_message_task();
        }
        if (!pending_frame_hydrations.empty()) {
            auto* frame = pending_frame_hydrations.front();
            pending_frame_hydrations.erase(pending_frame_hydrations.begin());
            if (frame != nullptr && !hydrate_frame(*frame)) {
                frame->attributes["data-webscene-frame-error"] =
                    frame_last_error_value;
                last_error = frame_last_error_value;
                return false;
            }
            return true;
        }
        const auto timer_due = has_due_timer();
        // Dynamically connected resources and zero-delay timers are separate
        // browser task sources. Do not starve React/datafeed timers behind an
        // entire Webpack resource wave; alternate when both are ready.
        if (!connected_resources.empty()
            && (!timer_due || !prefer_timer_task)) {
            // Preparing a large warm-cache resource wave can itself take a few
            // milliseconds. Only do that work once this task source has won
            // the fairness decision, so an already-due timer runs promptly.
            return drain_connected_resource_task();
        }
        if (resize_observers_pending) {
            resize_observers_pending = false;
            ensure_layout();
            return dispatch_resize_observers();
        }
        const auto now_time = std::chrono::steady_clock::now();
        const auto next = std::min_element(
            timers.begin(),
            timers.end(),
            [](const auto& left, const auto& right) { return left.deadline < right.deadline; });
        if (next == timers.end() || next->deadline > now_time) return true;
        auto task = std::move(*next);
        timers.erase(next);
        prefer_timer_task = false;
        return execute_timer_task(std::move(task), now_time);
    }

    bool has_due_timer() const noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        return std::any_of(
            timers.begin(),
            timers.end(),
            [now](const auto& timer) { return timer.deadline <= now; });
    }

    bool has_due_animation_frame_task() const noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        return std::any_of(
            timers.begin(),
            timers.end(),
            [now](const auto& timer) {
                return timer.animation_frame && timer.deadline <= now;
            });
    }

    bool has_waiting_animation_frame_task() const noexcept
    {
        return std::any_of(
            timers.begin(),
            timers.end(),
            [](const auto& timer) {
                return timer.animation_frame
                    && timer.deadline
                        == std::chrono::steady_clock::time_point::max();
            });
    }

    bool drain_animation_frame_task()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto next = std::min_element(
            timers.begin(),
            timers.end(),
            [now](const auto& left, const auto& right) {
                const auto left_due =
                    left.animation_frame && left.deadline <= now;
                const auto right_due =
                    right.animation_frame && right.deadline <= now;
                if (left_due != right_due) return left_due;
                return left.deadline < right.deadline;
            });
        if (next == timers.end()
            || !next->animation_frame
            || next->deadline > now) {
            return true;
        }
        auto task = std::move(*next);
        timers.erase(next);
        return execute_timer_task(std::move(task), now);
    }

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    void summarize_resize_cpu_profile(const v8::CpuProfile& profile)
    {
        std::unordered_map<std::string, uint64_t> hits;
        std::vector<std::string> path;
        const auto visit = [&](const auto& self, const v8::CpuProfileNode* node) -> void {
            if (node == nullptr) return;
            auto name = std::string(node->GetFunctionNameStr());
            if (name.empty()) name = "(anonymous)";
            const auto resource = std::string(node->GetScriptResourceNameStr());
            if (!resource.empty()) {
                const auto leaf = std::filesystem::path(resource).filename().string();
                name += "@" + (leaf.empty() ? resource : leaf);
            }
            name += "#" + std::to_string(node->GetScriptId())
                + ":" + std::to_string(node->GetLineNumber())
                + "/t" + std::to_string(static_cast<int>(node->GetSourceType()));
            path.push_back(std::move(name));
            const auto hit_count = node->GetHitCount();
            if (hit_count != 0) {
                std::ostringstream key;
                const auto begin = path.size() > 5U ? path.size() - 5U : 0U;
                for (size_t index = begin; index < path.size(); ++index) {
                    if (index != begin) key << '>';
                    key << path[index];
                }
                hits[std::move(key).str()] += hit_count;
            }
            for (int index = 0; index < node->GetChildrenCount(); ++index) {
                self(self, node->GetChild(index));
            }
            path.pop_back();
        };
        visit(visit, profile.GetTopDownRoot());
        std::vector<std::pair<std::string, uint64_t>> ordered(hits.begin(), hits.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
        std::ostringstream result;
        result << "samples=" << profile.GetSamplesCount() << "/top{";
        const auto limit = std::min<size_t>(12U, ordered.size());
        for (size_t index = 0; index < limit; ++index) {
            if (index != 0) result << ';';
            result << ordered[index].first << ':' << ordered[index].second;
        }
        result << '}';
        last_resize_cpu_profile = std::move(result).str();
    }
#endif

    v8::Local<v8::Object> create_event_instance(v8::Local<v8::Context> local_context)
    {
        v8::Local<v8::Value> constructor_value;
        if (local_context->Global()->Get(
                local_context,
                js_string(isolate, "Event")).ToLocal(&constructor_value)
            && constructor_value->IsFunction()) {
            auto constructor = constructor_value.As<v8::Function>();
            if (auto instance = constructor->NewInstance(local_context, 0, nullptr);
                !instance.IsEmpty()) {
                return instance.ToLocalChecked();
            }
        }
        return v8::Object::New(isolate);
    }

    v8::Local<v8::Object> create_window_event(
        v8::Local<v8::Context> local_context,
        std::string_view type)
    {
        auto event = create_event_instance(local_context);
        auto global = local_context->Global();
        event->Set(
            local_context,
            js_string(isolate, "type"),
            js_string(isolate, std::string(type).c_str())).Check();
        event->Set(local_context, js_string(isolate, "target"), global).Check();
        event->Set(local_context, js_string(isolate, "currentTarget"), global).Check();
        event->Set(local_context, js_string(isolate, "srcElement"), global).Check();
        event->Set(local_context, js_string(isolate, "view"), global).Check();
        event->Set(local_context, js_string(isolate, "bubbles"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "defaultPrevented"), v8::False(isolate)).Check();
        return event;
    }

    bool dispatch_resize()
    {
        record_feature(
            "event", "type:resize", "supported", {}, "event-dispatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        struct resize_profile_scope final {
            implementation& owner;

            explicit resize_profile_scope(implementation& value)
                : owner(value)
            {
                owner.resize_profile_active = owner.profile_bindings;
                if (owner.resize_profile_active) {
                    owner.last_resize_style_property_counts.clear();
                    owner.last_resize_style_target_counts.clear();
                    owner.last_resize_attribute_counts.clear();
                    owner.last_resize_geometry_counts.clear();
                }
            }

            ~resize_profile_scope()
            {
                owner.resize_profile_active = false;
            }
        } profile_scope{*this};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto resize_started = std::chrono::steady_clock::now();
#endif
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        {
            auto local_context = context.Get(isolate);
            v8::Context::Scope context_scope(local_context);
            auto global = local_context->Global();
            auto event = create_window_event(local_context, "resize");
            v8::Local<v8::Value> arguments[] = {event};
            const auto listener_count = resize_listeners.size();
            for (size_t index = 0; index < listener_count; ++index) {
                auto& listener = resize_listeners[index];
                auto callback = listener.callback.Get(isolate);
                if (callback.IsEmpty()) continue;
                if (!listener.signal.IsEmpty()) {
                    auto signal = listener.signal.Get(isolate);
                    v8::Local<v8::Value> aborted;
                    if (signal->Get(
                            local_context,
                            js_string(isolate, "aborted")).ToLocal(&aborted)
                        && aborted->BooleanValue(isolate)) {
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
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::Boolean::New(isolate, passive)).Check();
                v8::TryCatch try_catch(isolate);
                const auto callback_succeeded =
                    !call_event_listener(
                        isolate,
                        local_context,
                        callback,
                        global,
                        1,
                        arguments).IsEmpty();
                event->Set(
                    local_context,
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::False(isolate)).Check();
                if (!callback_succeeded) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                    report_inspector_exception(try_catch, local_context);
#endif
                    last_error = describe_exception(try_catch);
                    return false;
                }
            }
            std::erase_if(
                resize_listeners,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
            perform_microtask_checkpoint();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto outer_listeners_finished = std::chrono::steady_clock::now();
        last_resize_outer_listeners_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                outer_listeners_finished - resize_started).count());
#endif

        // Responsive stylesheet rules depend on the current host viewport.
        // The compact runtime retains the active browsing context's stylesheet
        // index. Once an iframe is hydrated, applying that index to the outer
        // document erases its already-cascaded percentage sizing (including a
        // host chart_container and the iframe itself). Re-cascade only the root
        // that owns the active rules; layout below still recomputes the unified
        // native tree against the new host viewport.
        if (refresh_css_media_matches()) {
            // Only a media-query truth transition changes the cascade. Relative
            // lengths (percent, vw/vh, calc) are retained as computed terms and
            // resolve against the new viewport during layout; recascading the
            // entire component tree on every intermediate resize frame turns a
            // 60 Hz drag into a 30 Hz one.
            rebuild_css_rule_indexes_and_root_variables();
            if (frame_context.IsEmpty()) {
                apply_css_rules_subtree(document.body());
            } else {
                auto resize_frame_context = frame_context.Get(isolate);
                v8::Context::Scope resize_frame_scope(resize_frame_context);
                apply_css_rules_subtree(active_root());
            }
        }

        if (frame_context.IsEmpty()) return true;
        {
            auto local_frame_context = frame_context.Get(isolate);
            v8::Context::Scope frame_scope(local_frame_context);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            v8::Local<v8::String> cpu_profile_title;
            auto cpu_profile_started = false;
            if (cpu_profiler != nullptr
                && (profile_bindings || !resize_cpu_profile_captured)) {
                cpu_profile_title = js_string(isolate, "webscene-resize-listener");
                cpu_profile_started = cpu_profiler->StartProfiling(cpu_profile_title, true)
                    == v8::CpuProfilingStatus::kStarted;
            }
            const auto stop_cpu_profile = [&] {
                if (!cpu_profile_started) return;
                if (auto* profile = cpu_profiler->StopProfiling(cpu_profile_title); profile != nullptr) {
                    summarize_resize_cpu_profile(*profile);
                    profile->Delete();
                }
                cpu_profile_started = false;
                resize_cpu_profile_captured = true;
                // Starting and stopping V8's profiler on every resize is itself
                // expensive enough to halve an otherwise 60 Hz stream. The
                // low-overhead mode deliberately captures one representative
                // callback, then removes the profiler from the live isolate.
                if (!profile_bindings && cpu_profiler != nullptr) {
                    cpu_profiler->Dispose();
                    cpu_profiler = nullptr;
                }
            };
            const auto binding_profile_before = binding_totals;
#endif
            auto listeners = frame_event_listeners.find("resize");
            if (listeners != frame_event_listeners.end()) {
                auto event = create_window_event(local_frame_context, "resize");
                v8::Local<v8::Value> arguments[] = {event};
                ++input_event_dispatch_count;
                auto frame_global = local_frame_context->Global();
                const auto listener_count = listeners->second.size();
                for (size_t index = 0; index < listener_count; ++index) {
                    auto& listener = listeners->second[index];
                    if (listener.target != 0U
                        || listener.context.Get(isolate) != local_frame_context) {
                        continue;
                    }
                    auto callback = listener.callback.Get(isolate);
                    if (callback.IsEmpty()) continue;
                    if (!listener.signal.IsEmpty()) {
                        auto signal = listener.signal.Get(isolate);
                        v8::Local<v8::Value> aborted;
                        if (signal->Get(
                                local_frame_context,
                                js_string(isolate, "aborted")).ToLocal(&aborted)
                            && aborted->BooleanValue(isolate)) {
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
                        local_frame_context,
                        js_string(isolate, "__websceneInPassiveListener"),
                        v8::Boolean::New(isolate, passive)).Check();
                    v8::TryCatch try_catch(isolate);
                    const auto callback_succeeded = !call_event_listener(
                            isolate,
                            local_frame_context,
                            callback,
                            frame_global,
                            1,
                            arguments).IsEmpty();
                    event->Set(
                        local_frame_context,
                        js_string(isolate, "__websceneInPassiveListener"),
                        v8::False(isolate)).Check();
                    if (!callback_succeeded) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                        report_inspector_exception(
                            try_catch,
                            local_frame_context);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                        stop_cpu_profile();
#endif
                        last_error = describe_exception(try_catch, local_frame_context);
                        return false;
                    }
                    ++input_callback_invocation_count;
                }
                std::erase_if(
                    listeners->second,
                    [](const auto& listener) { return listener.callback.IsEmpty(); });
            }
            perform_microtask_checkpoint();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            stop_cpu_profile();
            for (size_t index = 0; index < binding_category_count; ++index) {
                last_resize_binding_profile[index] = binding_callback_stats{
                    binding_totals[index].calls - binding_profile_before[index].calls,
                    binding_totals[index].nanoseconds - binding_profile_before[index].nanoseconds};
            }
#endif
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto frame_listeners_finished = std::chrono::steady_clock::now();
        last_resize_frame_listeners_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                frame_listeners_finished - outer_listeners_finished).count());
#endif
        if (shared_isolate != nullptr) {
            isolate->Exit();
            try {
                {
                    v8::Unlocker isolate_unlocker(isolate);
                    ensure_layout();
                }
            } catch (...) {
                isolate->Enter();
                throw;
            }
            isolate->Enter();
        } else {
            ensure_layout();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto layout_finished = std::chrono::steady_clock::now();
        last_resize_layout_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                layout_finished - frame_listeners_finished).count());
#endif
        const auto result = dispatch_resize_observers();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        last_resize_observers_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - layout_finished).count());
#endif
        return result;
    }

    bool dispatch_resize_observers()
    {
        std::vector<resize_observer_state*> pending;
        pending.reserve(resize_observers.size());
        for (const auto& observer : resize_observers) {
            if (observer != nullptr) pending.push_back(observer.get());
        }
        for (auto* observer : pending) {
            if (observer == nullptr || observer->nodes.empty()) continue;
            std::vector<dom_node*> changed_nodes;
            changed_nodes.reserve(observer->nodes.size());
            for (auto* node : observer->nodes) {
                if (node == nullptr) continue;
                const auto size = std::pair{node->layout.width, node->layout.height};
                const auto previous = observer->delivered_sizes.find(node->id);
                if (previous == observer->delivered_sizes.end() || previous->second != size) {
                    changed_nodes.push_back(node);
                    observer->delivered_sizes[node->id] = size;
                }
            }
            if (changed_nodes.empty()) continue;
            auto observer_context = observer->context.Get(isolate);
            auto callback = observer->callback.Get(isolate);
            if (observer_context.IsEmpty() || callback.IsEmpty()) continue;
            v8::Context::Scope observer_scope(observer_context);
            auto entries = v8::Array::New(isolate, static_cast<int>(changed_nodes.size()));
            for (uint32_t index = 0; index < changed_nodes.size(); ++index) {
                auto* node = changed_nodes[index];
                if (node == nullptr) continue;
                auto rect = v8::Object::New(isolate);
                const auto set = [&](const char* name, double value) {
                    rect->Set(
                        observer_context,
                        js_string(isolate, name),
                        v8::Number::New(isolate, value)).Check();
                };
                set("x", node->layout.x);
                set("y", node->layout.y);
                set("width", node->layout.width);
                set("height", node->layout.height);
                set("top", node->layout.y);
                set("left", node->layout.x);
                set("right", node->layout.x + node->layout.width);
                set("bottom", node->layout.y + node->layout.height);
                auto entry = v8::Object::New(isolate);
                entry->Set(observer_context, js_string(isolate, "target"), wrap_node(*node)).Check();
                entry->Set(observer_context, js_string(isolate, "contentRect"), rect).Check();
                auto box_size = v8::Object::New(isolate);
                box_size->Set(
                    observer_context,
                    js_string(isolate, "inlineSize"),
                    v8::Number::New(isolate, node->layout.width)).Check();
                box_size->Set(
                    observer_context,
                    js_string(isolate, "blockSize"),
                    v8::Number::New(isolate, node->layout.height)).Check();
                const auto make_box_array = [&] {
                    auto values = v8::Array::New(isolate, 1);
                    values->Set(observer_context, 0, box_size).Check();
                    return values;
                };
                entry->Set(
                    observer_context,
                    js_string(isolate, "contentBoxSize"),
                    make_box_array()).Check();
                entry->Set(
                    observer_context,
                    js_string(isolate, "borderBoxSize"),
                    make_box_array()).Check();
                entry->Set(
                    observer_context,
                    js_string(isolate, "devicePixelContentBoxSize"),
                    make_box_array()).Check();
                entries->Set(observer_context, index, entry).Check();
            }
            v8::Local<v8::Value> arguments[] = {entries, v8::Undefined(isolate)};
            v8::TryCatch try_catch(isolate);
            if (callback->Call(
                    observer_context,
                    observer_context->Global(),
                    2,
                    arguments).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                report_inspector_exception(try_catch, observer_context);
#endif
                last_error = describe_exception(try_catch, observer_context);
                return false;
            }
            ++input_callback_invocation_count;
            perform_microtask_checkpoint();
        }
        return true;
    }

    static void event_prevent_default(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> passive;
        if (info.This()->Get(
                context,
                js_string(info.GetIsolate(), "__websceneInPassiveListener")).ToLocal(&passive)
            && passive->BooleanValue(info.GetIsolate())) {
            return;
        }
        v8::Local<v8::Value> cancelable;
        if (!info.This()->Get(
                context,
                js_string(info.GetIsolate(), "cancelable")).ToLocal(&cancelable)
            || !cancelable->BooleanValue(info.GetIsolate())) return;
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "defaultPrevented"),
            v8::True(info.GetIsolate())).Check();
    }

    static void event_return_value_get(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> prevented;
        const auto default_prevented = info.This()->Get(
                context,
                js_string(info.GetIsolate(), "defaultPrevented")).ToLocal(&prevented)
            && prevented->BooleanValue(info.GetIsolate());
        info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), !default_prevented));
    }

    static void event_return_value_set(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0 || info[0]->BooleanValue(info.GetIsolate())) return;
        event_prevent_default(info);
    }

    static void event_stop_propagation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.This()->Set(
            info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "__webscenePropagationStopped"),
            v8::True(info.GetIsolate())).Check();
    }

    static void event_stop_immediate_propagation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "__webscenePropagationStopped"),
            v8::True(info.GetIsolate())).Check();
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "__websceneImmediatePropagationStopped"),
            v8::True(info.GetIsolate())).Check();
    }

    static void event_composed_path(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        v8::Local<v8::Value> stored;
        if (!info.This()->Get(
                local_context,
                js_string(isolate, "__webSceneComposedPath")).ToLocal(&stored)
            || !stored->IsArray()) {
            info.GetReturnValue().Set(v8::Array::New(isolate));
            return;
        }
        auto source = stored.As<v8::Array>();
        auto result = v8::Array::New(isolate, static_cast<int>(source->Length()));
        for (uint32_t index = 0; index < source->Length(); ++index) {
            v8::Local<v8::Value> entry;
            if (source->Get(local_context, index).ToLocal(&entry)) {
                result->Set(local_context, index, entry).Check();
            }
        }
        info.GetReturnValue().Set(result);
    }

    static std::string utf8_scalar(uint32_t value)
    {
        if (value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) return {};
        std::string result;
        if (value <= 0x7FU) {
            result.push_back(static_cast<char>(value));
        } else if (value <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else if (value <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        return result;
    }

    static std::string keyboard_key_name(int key_code)
    {
        switch (key_code) {
        case 8: return "Backspace";
        case 9: return "Tab";
        case 13: return "Enter";
        case 27: return "Escape";
        case 32: return " ";
        case 33: return "PageUp";
        case 34: return "PageDown";
        case 35: return "End";
        case 36: return "Home";
        case 37: return "ArrowLeft";
        case 38: return "ArrowUp";
        case 39: return "ArrowRight";
        case 40: return "ArrowDown";
        case 46: return "Delete";
        default:
            if ((key_code >= '0' && key_code <= '9')
                || (key_code >= 'A' && key_code <= 'Z')) {
                return std::string(1, static_cast<char>(key_code));
            }
            return "Unidentified";
        }
    }

    static std::string keyboard_code_name(int key_code)
    {
        if (key_code >= 'A' && key_code <= 'Z') {
            return "Key" + std::string(1, static_cast<char>(key_code));
        }
        if (key_code >= '0' && key_code <= '9') {
            return "Digit" + std::string(1, static_cast<char>(key_code));
        }
        return keyboard_key_name(key_code);
    }

    bool dispatch_input_event_type(
        const webscene_input_event& input,
        const char* type,
        dom_node& target,
        bool* default_prevented = nullptr)
    {
        record_feature(
            "event",
            std::string("type:") + type,
            "supported",
            {},
            "event-dispatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++event_dispatch_counts[type];
        ++event_dispatch_target_counts[type][target.id];
#endif
        const auto callback_count_before = input_callback_invocation_count;
        auto local_context = frame_context.IsEmpty()
            ? context.Get(isolate)
            : frame_context.Get(isolate);
        auto event = create_event_instance(local_context);
        auto target_wrapper = wrap_node(target);
        const auto set_number = [&](const char* name, double value) {
            event->Set(local_context, js_string(isolate, name), v8::Number::New(isolate, value)).Check();
        };
        const auto type_view = std::string_view(type);
        const auto bubbles = type_view != "blur"
            && type_view != "focus"
            && type_view != "mouseenter"
            && type_view != "mouseleave"
            && type_view != "pointerenter"
            && type_view != "pointerleave"
            && type_view != "scroll";
        const auto cancelable = type_view != "blur"
            && type_view != "focus"
            && type_view != "focusin"
            && type_view != "focusout"
            && type_view != "mouseenter"
            && type_view != "mouseleave"
            && type_view != "pointerenter"
            && type_view != "pointerleave"
            && type_view != "input"
            && type_view != "scroll";
        event->Set(local_context, js_string(isolate, "type"), js_string(isolate, type)).Check();
        event->Set(local_context, js_string(isolate, "target"), target_wrapper).Check();
        event->Set(local_context, js_string(isolate, "srcElement"), target_wrapper).Check();
        event->Set(local_context, js_string(isolate, "view"), local_context->Global()).Check();
        event->Set(local_context, js_string(isolate, "bubbles"), v8::Boolean::New(isolate, bubbles)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"), v8::Boolean::New(isolate, cancelable)).Check();
        event->Set(local_context, js_string(isolate, "defaultPrevented"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "pointerType"), js_string(isolate, "mouse")).Check();
        event->Set(local_context, js_string(isolate, "isPrimary"), v8::True(isolate)).Check();
        event->Set(
            local_context,
            js_string(isolate, "relatedTarget"),
            current_related_target == nullptr
                ? v8::Local<v8::Value>(v8::Null(isolate))
                : v8::Local<v8::Value>(wrap_node(*current_related_target))).Check();
        set_number("clientX", input.x);
        set_number("clientY", input.y);
        set_number("pageX", input.x);
        set_number("pageY", input.y);
        set_number("screenX", input.x);
        set_number("screenY", input.y);
        set_number("offsetX", input.x - target.layout.x);
        set_number("offsetY", input.y - target.layout.y);
        set_number("movementX", current_movement_x);
        set_number("movementY", current_movement_y);
        set_number("deltaX", input.delta_x);
        set_number("deltaY", input.delta_y);
        set_number("deltaMode", 0);
        const auto encoded_button = static_cast<int>((input.flags >> 8U) & 0xffU);
        const auto changed_button = encoded_button == 0 ? 0 : encoded_button - 1;
        const auto buttons = static_cast<int>(input.flags & 0x1fU);
        set_number("button", changed_button);
        set_number("buttons", buttons);
        set_number("pointerId", 1);
        const auto mouse_click_sequence_event = std::string_view(type) == "mousedown"
            || std::string_view(type) == "mouseup"
            || std::string_view(type) == "click"
            || std::string_view(type) == "auxclick";
        set_number("detail", mouse_click_sequence_event ? 1 : 0);
        // MouseEvent.which identifies the changed button and remains 1 for a
        // primary-button mouseup/click even though `buttons` has returned to 0.
        // Release and long-press paths in component libraries still consult this legacy
        // field; reporting 0 made a genuine release look like a hover event.
        const auto mouse_button_event = std::string_view(type) == "pointerdown"
            || std::string_view(type) == "mousedown"
            || std::string_view(type) == "pointerup"
            || std::string_view(type) == "mouseup"
            || std::string_view(type) == "click"
            || std::string_view(type) == "auxclick"
            || std::string_view(type) == "contextmenu";
        // Legacy MouseEvent.which also reports the currently held primary
        // button on mousemove. Drag and long-press normalization
        // still reads it in addition to `buttons`; reporting zero here makes
        // an ordered pressed move look like a hover on that path.
        const auto primary_button_held = (buttons & 1) != 0;
        const auto legacy_changed_button = changed_button == 1
            ? 2
            : changed_button == 2 ? 3 : 1;
        set_number(
            "which",
            mouse_button_event ? legacy_changed_button : (primary_button_held ? 1 : 0));
        const auto keyboard_input = input.kind == WEBSCENE_INPUT_KEY_DOWN
            || input.kind == WEBSCENE_INPUT_KEY_UP;
        const auto pointer_input = input.kind == WEBSCENE_INPUT_POINTER_MOVE
            || input.kind == WEBSCENE_INPUT_POINTER_DOWN
            || input.kind == WEBSCENE_INPUT_POINTER_UP
            || input.kind == WEBSCENE_INPUT_WHEEL;
        const auto text_input = input.kind == WEBSCENE_INPUT_TEXT;
        event->Set(local_context, js_string(isolate, "altKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_ALT) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_ALT) != 0U))).Check();
        event->Set(local_context, js_string(isolate, "ctrlKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_CONTROL) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL) != 0U))).Check();
        event->Set(local_context, js_string(isolate, "metaKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_META) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_META) != 0U))).Check();
        event->Set(local_context, js_string(isolate, "shiftKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_SHIFT) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT) != 0U))).Check();
        if (keyboard_input) {
            const auto key_code = static_cast<int>(input.x);
            const auto key = keyboard_key_name(key_code);
            const auto code = keyboard_code_name(key_code);
            event->Set(local_context, js_string(isolate, "key"), js_string(isolate, key.c_str())).Check();
            event->Set(local_context, js_string(isolate, "code"), js_string(isolate, code.c_str())).Check();
            set_number("keyCode", key_code);
            set_number("charCode", 0);
            set_number("which", key_code);
            set_number("location", 0);
            event->Set(local_context, js_string(isolate, "repeat"), v8::Boolean::New(
                isolate, (input.flags & WEBSCENE_INPUT_KEY_REPEAT) != 0U)).Check();
        }
        if (text_input || std::string_view(type) == "beforeinput"
            || std::string_view(type) == "input") {
            const auto deleting = input.kind == WEBSCENE_INPUT_KEY_DOWN
                && (static_cast<int>(input.x) == 8 || static_cast<int>(input.x) == 46);
            const auto inserting_line_break = input.kind == WEBSCENE_INPUT_KEY_DOWN
                && static_cast<int>(input.x) == 13;
            const auto data = text_input ? utf8_scalar(static_cast<uint32_t>(input.x)) : std::string{};
            event->Set(
                local_context,
                js_string(isolate, "data"),
                deleting || inserting_line_break
                    ? v8::Local<v8::Value>(v8::Null(isolate))
                    : v8::Local<v8::Value>(js_string(isolate, data.c_str()))).Check();
            event->Set(
                local_context,
                js_string(isolate, "inputType"),
                js_string(isolate, deleting
                    ? static_cast<int>(input.x) == 8
                        ? "deleteContentBackward"
                        : "deleteContentForward"
                    : inserting_line_break
                        ? "insertLineBreak"
                    : "insertText")).Check();
            event->Set(local_context, js_string(isolate, "isComposing"), v8::False(isolate)).Check();
        }
        set_number("timeStamp", std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        set_number("sequence", static_cast<double>(input.sequence));
        event->Set(
            local_context,
            js_string(isolate, "__webscenePropagationStopped"),
            v8::False(isolate)).Check();
        event->Set(
            local_context,
            js_string(isolate, "__websceneImmediatePropagationStopped"),
            v8::False(isolate)).Check();
        const auto read_event_boolean = [&](const char* name) {
            v8::Local<v8::Value> value;
            return event->Get(local_context, js_string(isolate, name)).ToLocal(&value)
                && value->BooleanValue(isolate);
        };
        const auto propagation_stopped = [&]() {
            return read_event_boolean("__webscenePropagationStopped");
        };
        const auto immediate_propagation_stopped = [&]() {
            return read_event_boolean("__websceneImmediatePropagationStopped");
        };

        std::vector<dom_node*> ancestry;
        for (auto* node = &target; node != nullptr; node = document.event_parent(*node)) {
            ancestry.push_back(node);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (std::string_view(type) == "mousemove") {
            std::ostringstream path;
            for (size_t index = 0; index < ancestry.size(); ++index) {
                if (index != 0) path << '>';
                path << ancestry[index]->id;
            }
            last_mousemove_ancestry = path.str();
        }
#endif
        ++input_event_dispatch_count;
        v8::Local<v8::Value> arguments[] = {event};
        const auto listeners = frame_event_listeners.find(type);
        const auto retargeted_node = [&](const dom_node* current_target) {
            const auto* root = document.containing_shadow_root(target);
            if (root == nullptr) return &target;
            if (current_target != nullptr
                && document.containing_shadow_root(*current_target) == root) {
                return &target;
            }
            const auto* state = document.shadow_dom(*root);
            return state == nullptr || state->host == nullptr ? &target : state->host;
        };
        const auto set_retargeted_target = [&](const dom_node* current_target) {
            auto wrapper = wrap_node(*retargeted_node(current_target));
            event->Set(local_context, js_string(isolate, "target"), wrapper).Check();
            event->Set(local_context, js_string(isolate, "srcElement"), wrapper).Check();
        };
        const auto invoke_listeners = [&](
            uint32_t registered_target,
            v8::Local<v8::Object> current_target,
            bool capture,
            int event_phase) {
            if (listeners == frame_event_listeners.end()) return true;
            set_retargeted_target(document.find_by_native_id(registered_target));
            event->Set(local_context, js_string(isolate, "currentTarget"), current_target).Check();
            set_number("eventPhase", event_phase);
            const auto listener_count = listeners->second.size();
            for (size_t index = 0; index < listener_count; ++index) {
                auto& listener = listeners->second[index];
                if (listener.context.Get(isolate) != local_context
                    || listener.target != registered_target
                    || listener.capture != capture) continue;
                auto callback = listener.callback.Get(isolate);
                if (callback.IsEmpty()) continue;
                if (!listener.signal.IsEmpty()) {
                    auto signal = listener.signal.Get(isolate);
                    v8::Local<v8::Value> aborted;
                    if (signal->Get(
                            local_context,
                            js_string(isolate, "aborted")).ToLocal(&aborted)
                        && aborted->BooleanValue(isolate)) {
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
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::Boolean::New(isolate, passive)).Check();
                const auto callback_succeeded =
                    !call_event_listener(
                        isolate,
                        local_context,
                        callback,
                        current_target,
                        1,
                        arguments).IsEmpty();
                event->Set(
                    local_context,
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::False(isolate)).Check();
                if (!callback_succeeded) return false;
                ++input_callback_invocation_count;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++event_callback_target_counts[type][registered_target];
                ++event_callback_index_counts[type][index];
                if (std::getenv("WEBSCENE_PROBE_PROFILE_INPUT") != nullptr
                    && (std::string_view(type) == "pointerdown"
                        || std::string_view(type) == "mousedown"
                        || std::string_view(type) == "pointermove"
                        || std::string_view(type) == "mousemove"
                        || std::string_view(type) == "pointerup"
                        || std::string_view(type) == "mouseup")) {
                    std::cerr << "[Native Engine Probe] input listener"
                        << ", type=" << type
                        << ", target=" << registered_target
                        << ", capture=" << (capture ? 1 : 0)
                        << ", phase=" << event_phase
                        << ", listener=" << index
                        << ", stopped=" << (propagation_stopped() ? 1 : 0)
                        << ", immediate="
                        << (immediate_propagation_stopped() ? 1 : 0)
                        << '\n';
                }
#endif
                if (immediate_propagation_stopped()) break;
            }
            return true;
        };

        const auto property_name = std::string("on") + type;
        const auto invoke_property = [&](dom_node& node, int event_phase) {
            auto wrapper = wrap_node(node);
            v8::Local<v8::Value> handler;
            if (!wrapper->Get(local_context, js_string(isolate, property_name.c_str())).ToLocal(&handler)
                || !handler->IsFunction()) return true;
            set_retargeted_target(&node);
            event->Set(local_context, js_string(isolate, "currentTarget"), wrapper).Check();
            set_number("eventPhase", event_phase);
            if (handler.As<v8::Function>()->Call(local_context, wrapper, 1, arguments).IsEmpty()) {
                return false;
            }
            ++input_callback_invocation_count;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++event_callback_target_counts[type][node.id];
#endif
            return true;
        };

        // Preserve browser listener phase/ancestry order and propagation stops.
        // A native mouseup may have several gesture recognizers registered on
        // overlapping chart ancestors; continuing after stopPropagation can end
        // the same scale transaction twice.
        auto global = local_context->Global();
        auto document_target = global;
        v8::Local<v8::Value> document_value;
        if (global->Get(local_context, js_string(isolate, "document")).ToLocal(&document_value)
            && document_value->IsObject()) {
            document_target = document_value.As<v8::Object>();
        }
        auto composed_path = v8::Array::New(
            isolate,
            static_cast<int>(ancestry.size() + 2U));
        for (size_t index = 0; index < ancestry.size(); ++index) {
            composed_path->Set(
                local_context,
                static_cast<uint32_t>(index),
                wrap_node(*ancestry[index])).Check();
        }
        composed_path->Set(
            local_context,
            static_cast<uint32_t>(ancestry.size()),
            document_target).Check();
        composed_path->Set(
            local_context,
            static_cast<uint32_t>(ancestry.size() + 1U),
            global).Check();
        event->Set(
            local_context,
            js_string(isolate, "__webSceneComposedPath"),
            composed_path).Check();
        event->Set(
            local_context,
            js_string(isolate, "composed"),
            v8::True(isolate)).Check();
        if (!invoke_listeners(0U, global, true, 1)) return false;
        if (!propagation_stopped() && !invoke_listeners(
                std::numeric_limits<uint32_t>::max(),
                document_target,
                true,
                1)) return false;
        for (size_t cursor = ancestry.size();
             !propagation_stopped() && cursor-- > 1;) {
            auto* node = ancestry[cursor];
            if (!invoke_listeners(node->id, wrap_node(*node), true, 1)) return false;
        }
        if (!propagation_stopped()) {
            auto target_wrapper = wrap_node(target);
            if (!invoke_listeners(target.id, target_wrapper, true, 2)) return false;
            if (!immediate_propagation_stopped()
                && !invoke_listeners(target.id, target_wrapper, false, 2)) return false;
            if (!immediate_propagation_stopped()
                && !invoke_property(target, 2)) return false;
        }
        if (bubbles && !propagation_stopped()) {
            for (size_t cursor = 1;
                cursor < ancestry.size() && !propagation_stopped();
                 ++cursor) {
                auto* node = ancestry[cursor];
                auto wrapper = wrap_node(*node);
                if (!invoke_listeners(node->id, wrapper, false, 3)
                    || (!immediate_propagation_stopped()
                        && !invoke_property(*node, 3))) return false;
            }
            if (!propagation_stopped()
                && !invoke_listeners(
                    std::numeric_limits<uint32_t>::max(),
                    document_target,
                    false,
                    3)) return false;
            if (!propagation_stopped()
                && !invoke_listeners(0U, global, false, 3)) return false;
        }
        set_number("eventPhase", 0);
        event->Set(local_context, js_string(isolate, "currentTarget"), v8::Null(isolate)).Check();
        perform_microtask_checkpoint();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        event_callback_counts[type] +=
            input_callback_invocation_count - callback_count_before;
#else
        static_cast<void>(callback_count_before);
#endif
        if (default_prevented != nullptr) {
            v8::Local<v8::Value> prevented;
            *default_prevented = event->Get(
                    local_context,
                    js_string(isolate, "defaultPrevented"))
                .ToLocal(&prevented)
                && prevented->BooleanValue(isolate);
        }

        // removeEventListener is commonly called by component code from inside
        // its root mouseup callback. Removal tombstones the callback so the
        // active dispatch remains stable; compact tombstones only after every
        // callback for this event has completed.
        if (listeners != frame_event_listeners.end()) {
            std::erase_if(
                listeners->second,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
        }
        return true;
    }

    static bool is_text_control(const dom_node* node)
    {
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")
            || node->attributes.contains("disabled")) {
            return false;
        }
        if (node->tag == "textarea") return true;
        const auto type = node->attributes.find("type");
        if (type == node->attributes.end()) return true;
        return type->second != "checkbox" && type->second != "radio"
            && type->second != "button" && type->second != "submit"
            && type->second != "reset" && type->second != "file"
            && type->second != "image" && type->second != "range"
            && type->second != "color";
    }

    static bool is_natively_focusable(const dom_node* node)
    {
        if (node == nullptr || node->attributes.contains("disabled")) return false;
        if (node->tag == "button" || node->tag == "select" || node->tag == "textarea") {
            return true;
        }
        if (node->tag == "a") return node->attributes.contains("href");
        if (node->tag != "input") return false;
        const auto type = node->attributes.find("type");
        return type == node->attributes.end() || type->second != "hidden";
    }

    bool is_effectively_visible_for_focus(const dom_node* node)
    {
        auto visibility_resolved = false;
        auto visibility_hidden = false;
        for (auto* current_node = node; current_node != nullptr;
             current_node = document.composed_parent(*current_node)) {
            if (current_node->style.display == display_mode::none) return false;
            if (!visibility_resolved && current_node->style.visibility_specified) {
                visibility_hidden = current_node->style.visibility_hidden;
                visibility_resolved = true;
            }
        }
        return !visibility_hidden;
    }

    static bool try_tab_index(const dom_node* node, int& value)
    {
        if (node == nullptr) return false;
        const auto attribute = node->attributes.find("tabindex");
        if (attribute == node->attributes.end() || attribute->second.empty()) return false;
        char* end = nullptr;
        const auto parsed = std::strtol(attribute->second.c_str(), &end, 10);
        if (end == attribute->second.c_str() || *end != '\0') return false;
        value = static_cast<int>(parsed);
        return true;
    }

    bool is_programmatically_focusable(const dom_node* node)
    {
        int tab_index = 0;
        return node != nullptr
            && !node->attributes.contains("disabled")
            && is_effectively_visible_for_focus(node)
            && (try_tab_index(node, tab_index) || is_natively_focusable(node));
    }

    bool is_pointer_focusable(const dom_node* node)
    {
        return is_programmatically_focusable(node);
    }

    dom_node* nearest_pointer_focusable(dom_node* node)
    {
        for (auto* current_node = node; current_node != nullptr;
            current_node = document.composed_parent(*current_node)) {
            if (is_pointer_focusable(current_node)) return current_node;
        }
        return nullptr;
    }

    std::vector<dom_node*> sequential_focus_order()
    {
        struct candidate final {
            dom_node* node;
            int tab_index;
            size_t order;
        };
        std::vector<candidate> candidates;
        size_t order = 0;
        const std::function<void(dom_node&)> visit = [&](dom_node& node) {
            if (!node.tag.starts_with('#') && !node.attributes.contains("disabled")
                && is_effectively_visible_for_focus(&node)) {
                int tab_index = 0;
                const auto explicit_tab_index = try_tab_index(&node, tab_index);
                if ((explicit_tab_index && tab_index >= 0)
                    || (!explicit_tab_index && is_natively_focusable(&node))) {
                    candidates.push_back(candidate{&node, tab_index, order});
                }
                ++order;
            }
            for (auto* child : document.composed_children(node)) visit(*child);
        };
        visit(active_root());
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
            const auto left_positive = left.tab_index > 0;
            const auto right_positive = right.tab_index > 0;
            if (left_positive != right_positive) return left_positive;
            if (left_positive && left.tab_index != right.tab_index) {
                return left.tab_index < right.tab_index;
            }
            return left.order < right.order;
        });
        std::vector<dom_node*> result;
        result.reserve(candidates.size());
        for (const auto& entry : candidates) result.push_back(entry.node);
        return result;
    }

    dom_node* nearest_text_control(dom_node* node)
    {
        for (auto* current_node = node; current_node != nullptr;
            current_node = document.composed_parent(*current_node)) {
            if (is_text_control(current_node)) return current_node;
        }
        return nullptr;
    }

    uint32_t focus_document_owner(const dom_node* node) const
    {
        for (auto* ancestor = node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag == "iframe") return ancestor->id;
            for (const auto& [frame_id, body] : provisional_frame_bodies) {
                if (ancestor == body) return frame_id;
            }
        }
        return 0;
    }

    static dom_node* find_node_by_native_id(dom_node& root, uint32_t id)
    {
        if (root.id == id) return &root;
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (auto* match = find_node_by_native_id(*child, id); match != nullptr) return match;
        }
        return nullptr;
    }

    void recascade_focus_subject(dom_node& subject, std::string_view reason)
    {
        // :focus and :focus-visible can restyle the subject, its descendants,
        // and siblings selected through +/~. They cannot restyle an ancestor
        // without :has(), which this runtime does not advertise. Recomputing
        // the subject's parent subtree therefore preserves all supported
        // selector semantics without cascading the unrelated chart document.
        auto* invalidation_root = subject.parent == nullptr ? &subject : subject.parent;
        recascade_connected_subtree(*invalidation_root, reason);
    }

    bool set_active_element(dom_node* next, const webscene_input_event& input)
    {
        if (active_element == next) return true;
        auto* previous = active_element;
        auto* related_target = previous != nullptr && next != nullptr
                && focus_document_owner(previous) == focus_document_owner(next)
            ? next
            : nullptr;
        if (previous != nullptr) {
            previous->mutable_form_control().input_focused = false;
            previous->mutable_form_control().caret_visible = false;
            // The document regains focus before blur/focusout listeners run.
            // This also allows a listener to focus another element without
            // recursively blurring the element whose transition is in flight.
            active_element = nullptr;
            recascade_focus_subject(*previous, "focus-before-blur");
            current_related_target = related_target;
            if (!dispatch_input_event_type(input, "blur", *previous)) {
                current_related_target = nullptr;
                return false;
            }
            current_related_target = related_target;
            if (!dispatch_input_event_type(input, "focusout", *previous)) {
                current_related_target = nullptr;
                return false;
            }
            // A focus/blur listener may have completed a newer focus change.
            // Preserve that result instead of overwriting it with this request.
            if (active_element != nullptr) {
                current_related_target = nullptr;
                document.mark_dirty();
                return true;
            }
        }
        active_element = next;
        // Focus styles are observable synchronously from focus listeners.
        // Recompute them before dispatching focus/focusin, not only after the
        // event sequence has completed. When `next` is null, the transition to
        // an unfocused document was already recomputed before blur above.
        if (next != nullptr) {
            recascade_focus_subject(*next, "focus-before-focus");
        }
        if (next != nullptr) {
            next->mutable_form_control().input_focused = is_text_control(next);
            next->mutable_form_control().caret_visible = next->mutable_form_control().input_focused;
            caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
            related_target = previous != nullptr
                    && focus_document_owner(previous) == focus_document_owner(next)
                ? previous
                : nullptr;
            current_related_target = related_target;
            if (!dispatch_input_event_type(input, "focus", *next)) {
                current_related_target = nullptr;
                return false;
            }
            current_related_target = related_target;
            if (!dispatch_input_event_type(input, "focusin", *next)) {
                current_related_target = nullptr;
                return false;
            }
        }
        current_related_target = nullptr;
        // The active element has not changed since the pre-event recascade.
        // DOM/class/style writes performed by focus listeners invalidate their
        // own affected subtrees, so a duplicate whole-document pass here only
        // delays publication without changing computed style.
        document.mark_dirty();
        return true;
    }

    bool subtree_contains(const dom_node& root, const dom_node* candidate)
    {
        for (auto* current_node = candidate; current_node != nullptr;
             current_node = document.event_parent(*current_node)) {
            if (current_node == &root) return true;
        }
        return false;
    }

    bool blur_active_element_before_detach(dom_node& detached_root)
    {
        if (!subtree_contains(detached_root, active_element)) return true;
        webscene_input_event synthetic{};
        if (!set_active_element(nullptr, synthetic)) return false;

        // A blur/focusout listener can synchronously focus another node. Preserve
        // focus outside the subtree, but never retain a node that is about to be
        // detached as document.activeElement.
        if (subtree_contains(detached_root, active_element)) {
            auto* detached_active_element = active_element;
            detached_active_element->mutable_form_control().input_focused = false;
            detached_active_element->mutable_form_control().caret_visible = false;
            active_element = nullptr;
            recascade_focus_subject(
                *detached_active_element,
                "focus-detached-reentrant");
            document.mark_dirty();
        }
        return true;
    }

    bool blur_active_descendant_before_replacing_children(dom_node& parent)
    {
        for (auto* child : parent.children) {
            if (child != nullptr && subtree_contains(*child, active_element)) {
                return blur_active_element_before_detach(*child);
            }
        }
        return true;
    }

    void ensure_form_value(dom_node& node)
    {
        if (node.mutable_form_control().value_initialized) return;
        const auto value = node.attributes.find("value");
        node.mutable_form_control().value = value == node.attributes.end() ? std::string{} : value->second;
        node.mutable_form_control().value_initialized = true;
        node.mutable_form_control().selection_start = node.mutable_form_control().value.size();
        node.mutable_form_control().selection_end = node.mutable_form_control().value.size();
        node.mutable_form_control().selection_direction = text_selection_direction::none;
    }

    static size_t previous_utf8_boundary(const std::string& value, size_t index)
    {
        if (index == 0) return 0;
        --index;
        while (index > 0
            && (static_cast<unsigned char>(value[index]) & 0xC0U) == 0x80U) {
            --index;
        }
        return index;
    }

    static size_t next_utf8_boundary(const std::string& value, size_t index)
    {
        if (index >= value.size()) return value.size();
        ++index;
        while (index < value.size()
            && (static_cast<unsigned char>(value[index]) & 0xC0U) == 0x80U) {
            ++index;
        }
        return index;
    }

    bool dispatch_keyboard_or_text(const webscene_input_event& input)
    {
        auto* target = active_element == nullptr ? &active_root() : active_element;
        if (input.kind == WEBSCENE_INPUT_KEY_UP) {
            return dispatch_input_event_type(input, "keyup", *target);
        }

        if (input.kind == WEBSCENE_INPUT_TEXT) {
            if (!is_text_control(target)) return true;
            bool prevented = false;
            if (!dispatch_input_event_type(input, "beforeinput", *target, &prevented)) return false;
            if (prevented) return true;
            ensure_form_value(*target);
            const auto text = utf8_scalar(static_cast<uint32_t>(input.x));
            const auto start = std::min(target->mutable_form_control().selection_start, target->mutable_form_control().value.size());
            const auto end = std::min(
                std::max(target->mutable_form_control().selection_start, target->mutable_form_control().selection_end),
                target->mutable_form_control().value.size());
            target->mutable_form_control().value.replace(start, end - start, text);
            target->mutable_form_control().selection_start = start + text.size();
            target->mutable_form_control().selection_end = target->mutable_form_control().selection_start;
            target->mutable_form_control().selection_direction = text_selection_direction::none;
            target->mutable_form_control().caret_visible = true;
            caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
            document.mark_dirty();
            return dispatch_input_event_type(input, "input", *target);
        }

        bool prevented = false;
        if (!dispatch_input_event_type(input, "keydown", *target, &prevented)) return false;
        const auto key_code = static_cast<int>(input.x);
        if (!prevented && key_code == 9) {
            const auto focus_order = sequential_focus_order();
            if (!focus_order.empty()) {
                const auto current = std::find(focus_order.begin(), focus_order.end(), active_element);
                const auto backwards = (input.flags & WEBSCENE_INPUT_MODIFIER_SHIFT) != 0U;
                dom_node* next = nullptr;
                if (current == focus_order.end()) {
                    next = backwards ? focus_order.back() : focus_order.front();
                } else if (backwards) {
                    next = current == focus_order.begin() ? focus_order.back() : *(current - 1);
                } else {
                    next = current + 1 == focus_order.end() ? focus_order.front() : *(current + 1);
                }
                focus_visible = true;
                return set_active_element(next, input);
            }
        }
        if (!prevented && target->tag == "select"
            && !target->attributes.contains("multiple")
            && (key_code == 35 || key_code == 36
                || key_code == 38 || key_code == 40)) {
            const auto changed = move_single_select_selection(
                *target,
                key_code == 38 ? -1 : key_code == 40 ? 1 : 0,
                key_code == 36,
                key_code == 35,
                false);
            if (changed) {
                document.mark_dirty();
                if (!dispatch_input_event_type(input, "input", *target)
                    || !dispatch_input_event_type(input, "change", *target)) {
                    return false;
                }
            }
            return true;
        }
        if (prevented || !is_text_control(target)) return true;
        ensure_form_value(*target);
        auto start = std::min(target->mutable_form_control().selection_start, target->mutable_form_control().value.size());
        auto end = std::min(
            std::max(target->mutable_form_control().selection_start, target->mutable_form_control().selection_end),
            target->mutable_form_control().value.size());
        bool changed = false;
        bool inserting_line_break = false;
        const auto shift = (input.flags & WEBSCENE_INPUT_MODIFIER_SHIFT) != 0U;
        if (key_code == 8) {
            if (start == end) start = previous_utf8_boundary(target->mutable_form_control().value, start);
            changed = end > start;
        } else if (key_code == 46) {
            if (start == end) end = next_utf8_boundary(target->mutable_form_control().value, end);
            changed = end > start;
        } else if (key_code == 13 && target->tag == "textarea") {
            changed = true;
            inserting_line_break = true;
        } else if (key_code == 37 || key_code == 38
            || key_code == 39 || key_code == 40
            || key_code == 36 || key_code == 35) {
            const auto moving_backward = key_code == 37 || key_code == 38 || key_code == 36;
            const auto boundary = key_code == 36 || key_code == 38
                ? size_t{0}
                : key_code == 35 || key_code == 40
                    ? target->mutable_form_control().value.size()
                    : moving_backward
                        ? previous_utf8_boundary(target->mutable_form_control().value, start)
                        : next_utf8_boundary(target->mutable_form_control().value, end);
            if (!shift) {
                const auto position = start != end && (key_code == 37 || key_code == 38)
                    ? start
                    : start != end && (key_code == 39 || key_code == 40)
                        ? end
                        : boundary;
                target->mutable_form_control().selection_start = target->mutable_form_control().selection_end = position;
                target->mutable_form_control().selection_direction = text_selection_direction::none;
            } else {
                const auto anchor =
                    target->mutable_form_control().selection_direction == text_selection_direction::backward
                    ? end
                    : start;
                const auto focus =
                    target->mutable_form_control().selection_direction == text_selection_direction::backward
                    ? start
                    : end;
                const auto next_focus = key_code == 36 || key_code == 38
                    ? size_t{0}
                    : key_code == 35 || key_code == 40
                        ? target->mutable_form_control().value.size()
                        : moving_backward
                            ? previous_utf8_boundary(target->mutable_form_control().value, focus)
                            : next_utf8_boundary(target->mutable_form_control().value, focus);
                target->mutable_form_control().selection_start = std::min(anchor, next_focus);
                target->mutable_form_control().selection_end = std::max(anchor, next_focus);
                target->mutable_form_control().selection_direction = next_focus < anchor
                    ? text_selection_direction::backward
                    : next_focus > anchor
                        ? text_selection_direction::forward
                        : text_selection_direction::none;
            }
        }
        if (changed) {
            bool before_input_prevented = false;
            if (!dispatch_input_event_type(input, "beforeinput", *target, &before_input_prevented)) {
                return false;
            }
            if (!before_input_prevented) {
                if (inserting_line_break) {
                    target->mutable_form_control().value.replace(start, end - start, "\n");
                    ++start;
                } else {
                    target->mutable_form_control().value.erase(start, end - start);
                }
                target->mutable_form_control().selection_start =
                    target->mutable_form_control().selection_end = start;
                target->mutable_form_control().selection_direction = text_selection_direction::none;
                if (!dispatch_input_event_type(input, "input", *target)) return false;
            }
        }
        target->mutable_form_control().caret_visible = true;
        caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
        document.mark_dirty();
        return true;
    }

    bool move_single_select_selection(
        dom_node& select,
        int offset,
        bool first,
        bool last,
        bool wrap)
    {
        std::vector<dom_node*> options;
        const auto collect = [&](const auto& self, dom_node& node) -> void {
            for (auto* child : node.children) {
                if (child == nullptr) continue;
                if (child->tag == "option" && !child->attributes.contains("disabled")) {
                    options.push_back(child);
                }
                self(self, *child);
            }
        };
        collect(collect, select);
        if (options.empty()) return false;

        auto selected = options.end();
        selected = std::find_if(options.begin(), options.end(), [](const auto* option) {
            return option->form_control().selectedness_initialized
                && option->form_control().selectedness;
        });
        if (selected == options.end()
            && !select.form_control().selection_explicitly_empty) {
            selected = std::find_if(options.begin(), options.end(), [](const auto* option) {
                return option->attributes.contains("selected");
            });
        }
        if (selected == options.end()
            && !select.form_control().selection_explicitly_empty) {
            selected = options.begin();
        }
        auto next = selected;
        if (first) next = options.begin();
        else if (last) next = options.end() - 1;
        else if (selected == options.end()) next = options.begin();
        else if (offset > 0) {
            if (selected + 1 != options.end()) next = selected + 1;
            else if (wrap) next = options.begin();
        } else if (offset < 0) {
            if (selected != options.begin()) next = selected - 1;
            else if (wrap) next = options.end() - 1;
        }
        if (next == options.end() || next == selected) return false;
        select.mutable_form_control().selection_explicitly_empty = false;
        for (auto* option : options) {
            option->mutable_form_control().selectedness_initialized = true;
            option->mutable_form_control().selectedness = option == *next;
        }
        return true;
    }

    bool apply_form_control_click_default(dom_node& target)
    {
        if (target.attributes.contains("disabled")) return false;
        if (target.tag == "select" && !target.attributes.contains("multiple")) {
            // WebScene has no operating-system popup surface. A direct native
            // activation advances the collapsed selection, while Arrow keys
            // retain the usual bounded navigation. This keeps ordinary two-
            // choice component controls usable without a presenter widget.
            return move_single_select_selection(target, 1, false, false, true);
        }
        if (target.tag != "input") return false;
        const auto type_iterator = target.attributes.find("type");
        auto type = type_iterator == target.attributes.end()
            ? std::string("text")
            : type_iterator->second;
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (type != "checkbox" && type != "radio") return false;

        auto local_context = frame_context.IsEmpty()
            ? context.Get(isolate)
            : frame_context.Get(isolate);
        auto target_wrapper = wrap_node(target);
        v8::Local<v8::Value> current_value;
        const auto was_checked = target_wrapper->Get(
                local_context,
                js_string(isolate, "checked"))
            .ToLocal(&current_value)
            && current_value->BooleanValue(isolate);
        const auto checked = type == "radio" ? true : !was_checked;
        if (type == "radio" && checked) {
            const auto name_iterator = target.attributes.find("name");
            const auto name = name_iterator == target.attributes.end()
                ? std::string{}
                : name_iterator->second;
            if (!name.empty()) {
                for (auto* candidate : document.query_selector_all(active_root(), "input")) {
                    if (candidate == &target) continue;
                    const auto candidate_type = candidate->attributes.find("type");
                    const auto candidate_name = candidate->attributes.find("name");
                    if (candidate_type == candidate->attributes.end()
                        || candidate_type->second != "radio"
                        || candidate_name == candidate->attributes.end()
                        || candidate_name->second != name) {
                        continue;
                    }
                    wrap_node(*candidate)->Set(
                        local_context,
                        js_string(isolate, "checked"),
                        v8::False(isolate)).Check();
                }
            }
        }
        target_wrapper->Set(
            local_context,
            js_string(isolate, "checked"),
            checked ? v8::Local<v8::Value>(v8::True(isolate))
                    : v8::Local<v8::Value>(v8::False(isolate))).Check();
        return checked != was_checked;
    }

    dom_node* label_control_for_click(dom_node& target)
    {
        if (target.tag == "input" || target.tag == "select") return &target;
        auto* label = &target;
        while (label != nullptr && label->tag != "label") label = label->parent;
        if (label == nullptr) return nullptr;

        const auto explicit_control = label->attributes.find("for");
        if (explicit_control != label->attributes.end() && !explicit_control->second.empty()) {
            auto matches = document.query_selector_all(
                active_root(),
                "#" + explicit_control->second);
            if (!matches.empty()
                && (matches.front()->tag == "input" || matches.front()->tag == "select")) {
                return matches.front();
            }
        }
        auto descendants = document.query_selector_all(*label, "input");
        if (!descendants.empty()) return descendants.front();
        descendants = document.query_selector_all(*label, "select");
        return descendants.empty() ? nullptr : descendants.front();
    }

    bool dispatch_input(const webscene_input_event& input)
    {
        current_input_sequence = input.sequence;
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = frame_context.IsEmpty()
            ? context.Get(isolate)
            : frame_context.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        ensure_layout();
        v8::TryCatch try_catch(isolate);
        const auto report_input_exception = [&] {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, local_context);
#endif
        };
        if (input.kind == WEBSCENE_INPUT_KEY_DOWN
            || input.kind == WEBSCENE_INPUT_KEY_UP
            || input.kind == WEBSCENE_INPUT_TEXT) {
            if (!dispatch_keyboard_or_text(input)) {
                last_error = "Native keyboard input dispatch failed: "
                    + describe_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            return true;
        }
        auto* hit_target = document.hit_test(
            active_root(),
            static_cast<float>(input.x),
            static_cast<float>(input.y));
        if (hit_target == nullptr) hit_target = &active_root();
        auto* target = pointer_capture_target != nullptr
            && input.kind != WEBSCENE_INPUT_POINTER_DOWN
            ? pointer_capture_target
            : hit_target;
        current_movement_x = has_pointer_position ? input.x - last_pointer_x : 0;
        current_movement_y = has_pointer_position ? input.y - last_pointer_y : 0;
        if (input.kind != WEBSCENE_INPUT_WHEEL) {
            last_pointer_x = input.x;
            last_pointer_y = input.y;
            has_pointer_position = true;
        }
        if ((input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN)
            && hit_target != hover_target) {
            auto* previous_hover_target = hover_target;
            // Browsers update :hover before dispatching the boundary events.
            // Event handlers are therefore allowed to observe the new
            // computed style synchronously from mouseover/pointerover.
            hover_target = hit_target;
            recascade_hover_transition(
                previous_hover_target,
                hover_target,
                "hover-target-change");
            dom_node* common_ancestor = previous_hover_target;
            while (common_ancestor != nullptr) {
                auto* candidate = hit_target;
                while (candidate != nullptr && candidate != common_ancestor) {
                    candidate = candidate->parent;
                }
                if (candidate == common_ancestor) break;
                common_ancestor = common_ancestor->parent;
            }
            if (previous_hover_target != nullptr) {
                current_related_target = hit_target;
                if (!dispatch_input_event_type(input, "pointerout", *previous_hover_target)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
                for (auto* exited = previous_hover_target;
                     exited != nullptr && exited != common_ancestor;
                     exited = exited->parent) {
                    if (!dispatch_input_event_type(input, "pointerleave", *exited)) {
                        last_error = "Native input transition dispatch failed: "
                            + describe_exception(try_catch, local_context);
                        report_input_exception();
                        return false;
                    }
                }
                if (!dispatch_input_event_type(input, "mouseout", *previous_hover_target)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
                for (auto* exited = previous_hover_target;
                     exited != nullptr && exited != common_ancestor;
                     exited = exited->parent) {
                    if (!dispatch_input_event_type(input, "mouseleave", *exited)) {
                        last_error = "Native input transition dispatch failed: "
                            + describe_exception(try_catch, local_context);
                        report_input_exception();
                        return false;
                    }
                }
            }
            current_related_target = previous_hover_target;
            std::vector<dom_node*> entered_nodes;
            for (auto* entered = hit_target;
                 entered != nullptr && entered != common_ancestor;
                 entered = entered->parent) {
                entered_nodes.push_back(entered);
            }
            std::reverse(entered_nodes.begin(), entered_nodes.end());
            if (!dispatch_input_event_type(input, "pointerover", *hit_target)) {
                last_error = "Native input transition dispatch failed: "
                    + describe_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            for (auto* entered : entered_nodes) {
                if (!dispatch_input_event_type(input, "pointerenter", *entered)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
            }
            if (!dispatch_input_event_type(input, "mouseover", *hit_target)) {
                last_error = "Native input transition dispatch failed: "
                    + describe_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            for (auto* entered : entered_nodes) {
                if (!dispatch_input_event_type(input, "mouseenter", *entered)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
            }
            current_related_target = nullptr;

            // Hover handlers are allowed to replace the element under the
            // pointer (several component libraries do this for icon buttons). The
            // subsequent pointerdown must target the post-hover DOM, not the
            // now-detached node selected before pointerover/mouseenter ran.
            // Keeping the stale target can make a modal disappear through an
            // outside-click handler without invoking its Close/Cancel action,
            // leaving the toolbar opener permanently in its `isOpened` state.
            ensure_layout();
            hit_target = document.hit_test(
                active_root(),
                static_cast<float>(input.x),
                static_cast<float>(input.y));
            if (hit_target == nullptr) hit_target = &active_root();
            if (pointer_capture_target == nullptr
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN) {
                target = hit_target;
            }
            if (hover_target != hit_target) {
                auto* previous_hover_target = hover_target;
                hover_target = hit_target;
                recascade_hover_transition(
                    previous_hover_target,
                    hover_target,
                    "hover-target-replaced");
            }
        }
        const char* types[3]{};
        size_t type_count = 0;
        bool form_control_changed = false;
        dom_node* activated_form_control = nullptr;
        bool activate_label_control_after_click = false;
        dom_node* click_target = nullptr;
        dom_node* auxiliary_click_target = nullptr;
        dom_node* context_menu_target = nullptr;
        const auto encoded_button = static_cast<int>((input.flags >> 8U) & 0xffU);
        const auto changed_button = encoded_button == 0 ? 0 : encoded_button - 1;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (std::getenv("WEBSCENE_PROBE_PROFILE_INPUT") != nullptr
            && (input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN
                || input.kind == WEBSCENE_INPUT_POINTER_UP)) {
            std::cerr << "[Native Engine Probe] input dispatch"
                << ", kind=" << input.kind
                << ", sequence=" << input.sequence
                << ", hit=" << hit_target->id
                << ", target=" << target->id
                << ", capture-target="
                << (pointer_capture_target == nullptr
                    ? 0U : pointer_capture_target->id)
                << ", flags=" << input.flags
                << ", x=" << input.x
                << ", y=" << input.y
                << ", movement-x=" << current_movement_x
                << ", movement-y=" << current_movement_y
                << '\n';
        }
#endif
        switch (input.kind) {
        case WEBSCENE_INPUT_POINTER_MOVE:
            types[type_count++] = "pointermove";
            types[type_count++] = "mousemove";
            break;
        case WEBSCENE_INPUT_POINTER_DOWN:
            pointer_down_target = target;
            pointer_down_x = input.x;
            pointer_down_y = input.y;
            types[type_count++] = "pointerdown";
            types[type_count++] = "mousedown";
            if (changed_button == 2) context_menu_target = target;
            break;
        case WEBSCENE_INPUT_POINTER_UP:
            types[type_count++] = "pointerup";
            types[type_count++] = "mouseup";
            if (pointer_down_target != nullptr
                && std::hypot(input.x - pointer_down_x, input.y - pointer_down_y) <= 4.0) {
                // UI Events targets click at the nearest common inclusive
                // ancestor of the press and release targets. Interactive rows
                // reveal/replace icon children on hover, so requiring the same
                // leaf suppressed an otherwise valid click between down/up.
                auto* common_target = pointer_down_target;
                while (common_target != nullptr) {
                    auto* release_ancestor = hit_target;
                    while (release_ancestor != nullptr
                        && release_ancestor != common_target) {
                        release_ancestor = release_ancestor->parent;
                    }
                    if (release_ancestor == common_target) break;
                    common_target = common_target->parent;
                }
                if (common_target == nullptr) common_target = &active_root();
                if (changed_button == 0) {
                    click_target = common_target;
                    activated_form_control = label_control_for_click(*click_target);
                    activate_label_control_after_click = activated_form_control != nullptr
                        && activated_form_control != click_target;
                    if (activated_form_control != nullptr && !activate_label_control_after_click) {
                        // For a direct input activation the checked state changes
                        // before the input's click listeners run, matching HTML.
                        form_control_changed = apply_form_control_click_default(
                            *activated_form_control);
                    }
                } else {
                    auxiliary_click_target = common_target;
                }
            }
            break;
        case WEBSCENE_INPUT_WHEEL:
            types[type_count++] = "wheel";
            break;
        default:
            return true;
        }
        bool wheel_default_prevented = false;
        bool pointer_focus_default_prevented = false;
        for (size_t index = 0; index < type_count; ++index) {
            bool event_default_prevented = false;
            const auto observe_default = input.kind == WEBSCENE_INPUT_WHEEL
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN;
            if (!dispatch_input_event_type(
                    input,
                    types[index],
                    *target,
                    observe_default ? &event_default_prevented : nullptr)) {
                last_error = "Native input dispatch failed: " + describe_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            if (input.kind == WEBSCENE_INPUT_WHEEL) {
                wheel_default_prevented = wheel_default_prevented || event_default_prevented;
            } else if (input.kind == WEBSCENE_INPUT_POINTER_DOWN) {
                pointer_focus_default_prevented = pointer_focus_default_prevented
                    || event_default_prevented;
            }
        }
        if (input.kind == WEBSCENE_INPUT_WHEEL && !wheel_default_prevented) {
            for (auto* scroll_target = target; scroll_target != nullptr;
                scroll_target = scroll_target->parent) {
                // A loaded document exposes HTML beneath the retained native
                // viewport.  Geometry accessors project that HTML root onto
                // the viewport scroll owner, so wheel defaults must skip the
                // exposed wrapper and mutate the same owner as scrollTop.
                if (scroll_target->tag == "html"
                    && scroll_target->parent == &document.body()) {
                    continue;
                }
                const auto maximum_x = std::max(
                    0.0F,
                    scroll_target->scroll_content_width - scroll_target->scroll_viewport_width);
                const auto maximum_y = std::max(
                    0.0F,
                    scroll_target->scroll_content_height - scroll_target->scroll_viewport_height);
                const auto* exposed_root = &active_root();
                const auto root_viewport_scroll_x = scroll_target == &document.body()
                    && (exposed_root->style.overflow_x == overflow_mode::visible
                        || exposed_root->style.scroll_x_enabled);
                const auto root_viewport_scroll_y = scroll_target == &document.body()
                    && (exposed_root->style.overflow_y == overflow_mode::visible
                        || exposed_root->style.scroll_y_enabled);
                const auto next_x = (scroll_target->style.scroll_x_enabled
                        || root_viewport_scroll_x)
                    ? std::clamp(
                        scroll_target->scroll_left + static_cast<float>(input.delta_x),
                        0.0F,
                        maximum_x)
                    : scroll_target->scroll_left;
                const auto next_y = (scroll_target->style.scroll_y_enabled
                        || root_viewport_scroll_y)
                    ? std::clamp(
                        scroll_target->scroll_top + static_cast<float>(input.delta_y),
                        0.0F,
                        maximum_y)
                    : scroll_target->scroll_top;
                if (std::abs(next_x - scroll_target->scroll_left) <= 0.01F
                    && std::abs(next_y - scroll_target->scroll_top) <= 0.01F) {
                    continue;
                }
                scroll_target->scroll_left = next_x;
                scroll_target->scroll_top = next_y;
                document.mark_dirty();
                if (!dispatch_input_event_type(input, "scroll", *scroll_target)) {
                    last_error = "Native scroll dispatch failed: "
                        + describe_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
                break;
            }
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN
            && changed_button == 0
            && !pointer_focus_default_prevented) {
            auto* focus_target = nearest_pointer_focusable(target);
            focus_visible = false;
            if ((focus_target != nullptr || active_element != nullptr)
                && !set_active_element(focus_target, input)) {
                last_error = "Native focus dispatch failed: "
                    + describe_exception(try_catch, local_context);
                return false;
            }
        }
        if (context_menu_target != nullptr
            && !dispatch_input_event_type(input, "contextmenu", *context_menu_target)) {
            last_error = "Native contextmenu dispatch failed: "
                + describe_exception(try_catch, local_context);
            return false;
        }
        bool click_default_prevented = false;
        if (click_target != nullptr) {
            if (!dispatch_input_event_type(
                    input,
                    "click",
                    *click_target,
                    &click_default_prevented)) {
                last_error = "Native click dispatch failed: "
                    + describe_exception(try_catch, local_context);
                return false;
            }
            if (!click_default_prevented && !queue_external_navigation(*click_target)) {
                last_error = "Native external navigation host request could not be queued";
                return false;
            }
        }
        if (auxiliary_click_target != nullptr
            && !dispatch_input_event_type(input, "auxclick", *auxiliary_click_target)) {
            last_error = "Native auxiliary click dispatch failed: "
                + describe_exception(try_catch, local_context);
            return false;
        }
        if (activate_label_control_after_click) {
            // A label owns the visual hit area for many custom switches;
            // their native input is intentionally zero-width. HTML label
            // activation dispatches a second click at the associated control.
            form_control_changed = apply_form_control_click_default(*activated_form_control);
            if (!dispatch_input_event_type(input, "click", *activated_form_control)) {
                last_error = "Native label-control activation failed: "
                    + describe_exception(try_catch, local_context);
                return false;
            }
        }
        if (form_control_changed) {
            if (!dispatch_input_event_type(input, "input", *activated_form_control)
                || !dispatch_input_event_type(input, "change", *activated_form_control)) {
                last_error = "Native form-control dispatch failed: "
                    + describe_exception(try_catch, local_context);
                return false;
            }
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_UP) {
            pointer_capture_target = nullptr;
            pointer_down_target = nullptr;
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_MOVE
            || input.kind == WEBSCENE_INPUT_POINTER_DOWN
            || input.kind == WEBSCENE_INPUT_POINTER_UP) {
            ensure_layout();
            auto* cursor_target = document.hit_test(
                active_root(),
                static_cast<float>(input.x),
                static_cast<float>(input.y));
            current_cursor_kind_value = cursor_target == nullptr
                ? WEBSCENE_CURSOR_DEFAULT
                : cursor_kind_for(*cursor_target);
        }
        return true;
    }
