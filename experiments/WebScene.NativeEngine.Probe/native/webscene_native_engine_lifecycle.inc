    explicit webscene_engine(
        uint32_t command_count,
        std::string compilation_cache_directory = {},
        webscene_resource_load_callback resource_load_callback = nullptr,
        void* resource_load_user_data = nullptr,
        webscene_resource_load_callback_v2 resource_load_callback_v2 = nullptr,
        void* resource_load_v2_user_data = nullptr,
        webscene_resource_load_callback_v3 resource_load_callback_v3 = nullptr,
        void* resource_load_v3_user_data = nullptr,
        webscene_scene_published_callback scene_published_callback = nullptr,
        void* scene_published_user_data = nullptr,
        webscene_text_measure_callback text_measure_callback = nullptr,
        void* text_measure_user_data = nullptr,
        webscene_host_request_available_callback
            host_request_available_callback = nullptr,
        void* host_request_available_user_data = nullptr,
        webscene_interop_callback_available_callback
            interop_callback_available_callback = nullptr,
        void* interop_callback_available_user_data = nullptr,
        webscene_animation_frame_requested_callback
            animation_frame_requested_callback = nullptr,
        void* animation_frame_requested_user_data = nullptr)
        : command_count_(command_count == 0U
              ? 0U
              : (command_count < minimum_command_count
                    ? minimum_command_count
                    : command_count))
        , compilation_cache_directory_(std::move(compilation_cache_directory))
        , resource_load_callback_(resource_load_callback)
        , resource_load_user_data_(resource_load_user_data)
        , resource_load_callback_v2_(resource_load_callback_v2)
        , resource_load_v2_user_data_(resource_load_v2_user_data)
        , resource_load_callback_v3_(resource_load_callback_v3)
        , resource_load_v3_user_data_(resource_load_v3_user_data)
        , scene_published_callback_(scene_published_callback)
        , scene_published_user_data_(scene_published_user_data)
        , host_request_available_callback_(host_request_available_callback)
        , host_request_available_user_data_(host_request_available_user_data)
        , interop_callback_available_callback_(interop_callback_available_callback)
        , interop_callback_available_user_data_(interop_callback_available_user_data)
        , animation_frame_requested_callback_(animation_frame_requested_callback)
        , animation_frame_requested_user_data_(animation_frame_requested_user_data)
        , document_(text_measure_callback, text_measure_user_data)
        , worker_([this](std::stop_token token) { run(token); })
    {
    }

    ~webscene_engine()
    {
        worker_.request_stop();
        signal_worker();
        worker_.join();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        delete inspector_state_.exchange(nullptr, std::memory_order_acq_rel);
#endif
    }

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    engine_inspector_state* ensure_inspector_state()
    {
        auto* existing = inspector_state_.load(std::memory_order_acquire);
        if (existing != nullptr) return existing;
        auto candidate = std::make_unique<engine_inspector_state>();
        auto* expected = static_cast<engine_inspector_state*>(nullptr);
        if (inspector_state_.compare_exchange_strong(
                expected,
                candidate.get(),
                std::memory_order_release,
                std::memory_order_acquire)) {
            return candidate.release();
        }
        return expected;
    }

    void enqueue_inspector_pump()
    {
        auto* state = ensure_inspector_state();
        if (state->pump_enqueued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        {
            std::lock_guard lock(script_mutex_);
            script_work_.emplace_back(inspector_pump_work{});
        }
        signal_worker();
    }
#endif

    bool enqueue(const webscene_input_event& event)
    {
        if (event.kind == WEBSCENE_INPUT_RESIZE) {
            {
                std::lock_guard lock(resize_mutex_);
                if (pending_resize_count_ != 0) {
                    coalesced_resize_inputs_.fetch_add(1, std::memory_order_relaxed);
                }
                pending_resize_ = event;
                ++pending_resize_count_;
                // A later unpaired viewport supersedes the association between
                // an older viewport and its host frame. The queued frame records
                // remain accounted as coalesced when the resize slot is taken.
                pending_resize_has_frame_ = false;
                resize_pending_.store(true, std::memory_order_release);
            }
            enqueued_inputs_.fetch_add(1, std::memory_order_relaxed);
            signal_worker();
            return true;
        }

        if (!inputs_.try_push(event)) {
            dropped_inputs_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        enqueued_inputs_.fetch_add(1, std::memory_order_relaxed);
        signal_worker();
        return true;
    }

    bool enqueue_resize_frame(
        const webscene_input_event& resize_event,
        const webscene_input_event& frame_event)
    {
        if (resize_event.kind != WEBSCENE_INPUT_RESIZE
            || frame_event.kind != WEBSCENE_INPUT_FRAME) {
            return false;
        }
        const auto enqueued_at = std::chrono::steady_clock::now();
        {
            std::lock_guard lock(resize_mutex_);
            if (pending_resize_count_ != 0) {
                coalesced_resize_inputs_.fetch_add(1, std::memory_order_relaxed);
            }
            pending_resize_ = resize_event;
            ++pending_resize_count_;
            pending_resize_frame_ = frame_event;
            ++pending_resize_frame_count_;
            pending_resize_has_frame_ = true;
            pending_resize_frame_enqueued_at_ = enqueued_at;
            resize_pending_.store(true, std::memory_order_release);
        }
        submitted_resize_frame_pairs_.fetch_add(1, std::memory_order_relaxed);
        enqueued_inputs_.fetch_add(2, std::memory_order_relaxed);
        signal_worker();
        return true;
    }

    bool request_low_memory()
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        return false;
#else
        low_memory_requested_.store(true, std::memory_order_release);
        signal_worker();
        return true;
#endif
    }

    bool set_visible(bool visible)
    {
        host_visible_.store(visible, std::memory_order_release);
        visibility_changed_.store(true, std::memory_order_release);
        signal_worker();
        return true;
    }

    bool set_preferred_color_scheme(uint32_t preferred_color_scheme)
    {
        if (preferred_color_scheme != WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT
            && preferred_color_scheme != WEBSCENE_PREFERRED_COLOR_SCHEME_DARK) {
            return false;
        }
        const auto previous = preferred_color_scheme_.exchange(
            preferred_color_scheme,
            std::memory_order_acq_rel);
        if (previous != preferred_color_scheme) {
            preferred_color_scheme_changed_.store(true, std::memory_order_release);
            signal_worker();
        }
        return true;
    }

    uint32_t cursor() const noexcept
    {
        return current_cursor_.load(std::memory_order_acquire);
    }

    static bool advance_observed_timestamp(
        std::atomic<uint64_t>& destination,
        uint64_t timestamp_microseconds) noexcept
    {
        auto observed = destination.load(std::memory_order_relaxed);
        while (timestamp_microseconds > observed) {
            if (destination.compare_exchange_weak(
                    observed,
                    timestamp_microseconds,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    void observe_host_timeline(double timestamp_ms) noexcept
    {
        if (!std::isfinite(timestamp_ms) || timestamp_ms < 0) return;
        const auto timestamp_microseconds = static_cast<uint64_t>(
            timestamp_ms * 1000.0);
        static_cast<void>(advance_observed_timestamp(
            observed_host_timestamp_microseconds_,
            timestamp_microseconds));
    }

    void observe_compositor_frame(double timestamp_ms) noexcept
    {
        if (!std::isfinite(timestamp_ms) || timestamp_ms < 0) return;
        const auto timestamp_microseconds = static_cast<uint64_t>(
            timestamp_ms * 1000.0);
        static_cast<void>(advance_observed_timestamp(
            observed_host_timestamp_microseconds_,
            timestamp_microseconds));
        if (advance_observed_timestamp(
                observed_compositor_timestamp_microseconds_,
                timestamp_microseconds)
            && frame_paced_pointer_pending_.load(std::memory_order_acquire)) {
            // A deferred continuous pointer sample is released at the next
            // actual display boundary even when the page has no RAF demand.
            signal_worker();
        }
    }

    uint8_t animation_frame_demand() const noexcept
    {
        const auto host_demand = host_animation_frame_requested_.load(
            std::memory_order_acquire);
        return static_cast<uint8_t>(
            host_demand
            | (frame_paced_pointer_pending_.load(std::memory_order_acquire)
                    ? uint8_t{4U}
                    : uint8_t{0U}));
    }

    bool execute_script(
        const char* source,
        size_t source_length,
        const char* document_name,
        size_t document_name_length)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(source);
        static_cast<void>(source_length);
        static_cast<void>(document_name);
        static_cast<void>(document_name_length);
        set_last_error("Native engine was built without V8 support");
        return false;
#else
        if (source == nullptr || source_length == 0) {
            set_last_error("Script source is empty");
            return false;
        }
        std::lock_guard lock(script_mutex_);
        const auto pending_scripts = std::count_if(
            script_work_.begin(),
            script_work_.end(),
            [](const auto& request) {
                return std::holds_alternative<script_request>(request);
            });
        if (pending_scripts >= 64) {
            set_last_error("Native script queue is full");
            return false;
        }
        script_work_.emplace_back(script_request{
            std::string(source, source_length),
            document_name == nullptr
                ? std::string("native-engine.js")
                : std::string(document_name, document_name_length)});
        signal_worker();
        return true;
#endif
    }

    uint64_t connect_inspector(
        webscene_inspector_message_callback callback,
        void* user_data,
        bool wait_for_debugger)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        static_cast<void>(callback);
        static_cast<void>(user_data);
        static_cast<void>(wait_for_debugger);
        return 0U;
#else
        auto* runtime = inspector_runtime_.load(std::memory_order_acquire);
        if (runtime == nullptr || callback == nullptr) return 0U;
        const auto session_id = runtime->connect_inspector(
            [callback, user_data](uint64_t id, std::string_view message) {
                callback(user_data, id, message.data(), message.size());
            },
            wait_for_debugger,
            [this] { enqueue_inspector_pump(); });
        return session_id;
#endif
    }

    uint64_t connect_inspector_v2(
        webscene_inspector_message_available_callback_v3 callback,
        void* user_data,
        bool wait_for_debugger)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        static_cast<void>(callback);
        static_cast<void>(user_data);
        static_cast<void>(wait_for_debugger);
        return 0U;
#else
        auto* runtime = inspector_runtime_.load(std::memory_order_acquire);
        if (runtime == nullptr || callback == nullptr) return 0U;
        auto* inspector_state = ensure_inspector_state();
        auto output = std::make_shared<inspector_output_state>();
        output->callback = callback;
        output->user_data = user_data;
        const auto session_id = runtime->connect_inspector(
            [output](uint64_t id, std::string_view message) {
                output->publish(id, message);
            },
            wait_for_debugger,
            [this] { enqueue_inspector_pump(); });
        if (session_id == 0U) return 0U;
        {
            std::lock_guard lock(inspector_state->output_mutex);
            inspector_state->outputs[session_id] = std::move(output);
        }
        return session_id;
#endif
    }

    size_t take_inspector_message(
        uint64_t session_id,
        char* destination,
        size_t destination_capacity)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        static_cast<void>(session_id);
        static_cast<void>(destination);
        static_cast<void>(destination_capacity);
        return 0U;
#else
        auto* inspector_state = inspector_state_.load(std::memory_order_acquire);
        if (inspector_state == nullptr) return 0U;
        std::shared_ptr<inspector_output_state> output;
        {
            std::lock_guard lock(inspector_state->output_mutex);
            const auto found = inspector_state->outputs.find(session_id);
            if (found == inspector_state->outputs.end()) return 0U;
            output = found->second;
        }
        return output->take(destination, destination_capacity);
#endif
    }

    bool dispatch_inspector(
        uint64_t session_id,
        const char* message,
        size_t message_length)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        static_cast<void>(session_id);
        static_cast<void>(message);
        static_cast<void>(message_length);
        return false;
#else
        auto* runtime = inspector_runtime_.load(std::memory_order_acquire);
        static constexpr size_t maximum_inspector_message_bytes =
            16U * 1024U * 1024U;
        if (runtime == nullptr || message == nullptr || message_length == 0U
            || message_length > maximum_inspector_message_bytes) {
            return false;
        }
        if (!runtime->dispatch_inspector_message(
                session_id,
                std::string(message, message_length))) {
            return false;
        }
        enqueue_inspector_pump();
        return true;
#endif
    }

    bool disconnect_inspector(uint64_t session_id)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        static_cast<void>(session_id);
        return false;
#else
        auto* runtime = inspector_runtime_.load(std::memory_order_acquire);
        if (runtime == nullptr
            || !runtime->disconnect_inspector(session_id)) return false;
        if (auto* inspector_state = inspector_state_.load(
                std::memory_order_acquire)) {
            std::lock_guard lock(inspector_state->output_mutex);
            inspector_state->outputs.erase(session_id);
        }
        enqueue_inspector_pump();
        return true;
#endif
    }

    bool inspector_available() const noexcept
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        return false;
#else
        const auto* runtime = inspector_runtime_.load(std::memory_order_acquire);
        return runtime != nullptr && runtime->inspector_available();
#endif
    }

    bool inspector_state_created() const noexcept
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        return false;
#else
        return inspector_state_.load(std::memory_order_acquire) != nullptr;
#endif
    }

    bool set_resource_root(const char* value, size_t length)
    {
        if (value == nullptr || length == 0U) return false;
        std::lock_guard lock(configuration_mutex_);
        resource_root_.assign(value, length);
        return true;
    }

    bool load_url(const char* value, size_t length)
    {
        return enqueue_url(value, length, {});
    }

    bool load_url(
        const char* value,
        size_t length,
        const webscene_navigation_options* options)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(value);
        static_cast<void>(length);
        static_cast<void>(options);
        set_last_error("Native engine was built without V8 support");
        return false;
#else
        if (options == nullptr
            || options->struct_size < sizeof(webscene_navigation_options)) {
            set_last_error("Native navigation options are missing or too small");
            return false;
        }
        constexpr uint32_t maximum_document_scripts = 256U;
        constexpr size_t maximum_document_script_bytes = 16U * 1024U * 1024U;
        if (options->document_script_count > maximum_document_scripts) {
            set_last_error("Native navigation has too many document-start scripts");
            return false;
        }
        if (options->document_script_count != 0U
            && options->document_scripts == nullptr) {
            set_last_error("Native navigation document-start descriptors are missing");
            return false;
        }

        std::vector<webscene_native::document_start_script> scripts;
        scripts.reserve(options->document_script_count);
        size_t total_bytes = 0U;
        for (uint32_t index = 0U;
             index < options->document_script_count;
             ++index) {
            const auto& descriptor = options->document_scripts[index];
            if (descriptor.struct_size < sizeof(webscene_document_script)) {
                set_last_error(
                    "Native navigation document-start descriptor is too small");
                return false;
            }
            if ((descriptor.flags & ~WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES) != 0U) {
                set_last_error(
                    "Native navigation document-start descriptor has unknown flags");
                return false;
            }
            if (descriptor.source == nullptr || descriptor.source_length == 0U) {
                set_last_error("Document-start script source is empty");
                return false;
            }
            if (descriptor.name == nullptr || descriptor.name_length == 0U) {
                set_last_error("Document-start script name is empty");
                return false;
            }
            if (descriptor.source_length > maximum_document_script_bytes
                || descriptor.name_length > maximum_document_script_bytes
                || total_bytes > maximum_document_script_bytes
                    - descriptor.source_length
                || total_bytes + descriptor.source_length
                    > maximum_document_script_bytes - descriptor.name_length) {
                set_last_error("Native navigation document-start scripts are too large");
                return false;
            }
            total_bytes += descriptor.source_length + descriptor.name_length;
            scripts.push_back(webscene_native::document_start_script{
                std::string(descriptor.source, descriptor.source_length),
                std::string(descriptor.name, descriptor.name_length),
                (descriptor.flags & WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES) != 0U});
        }
        return enqueue_url(value, length, std::move(scripts));
#endif
    }

    bool enqueue_url(
        const char* value,
        size_t length,
        std::vector<webscene_native::document_start_script> scripts)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(value);
        static_cast<void>(length);
        static_cast<void>(scripts);
        set_last_error("Native engine was built without V8 support");
        return false;
#else
        if (value == nullptr || length == 0U) {
            set_last_error("Document URL is empty");
            return false;
        }
        std::lock_guard lock(script_mutex_);
        const auto pending_urls = std::count_if(
            script_work_.begin(),
            script_work_.end(),
            [](const auto& request) {
                return std::holds_alternative<url_request>(request);
            });
        if (pending_urls >= 8U) {
            set_last_error("Native document queue is full");
            return false;
        }
        script_work_.emplace_back(url_request{
            std::string(value, length),
            std::move(scripts)});
        signal_worker();
        return true;
#endif
    }
