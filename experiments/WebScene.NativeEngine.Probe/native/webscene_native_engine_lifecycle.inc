    explicit webscene_engine(
        uint32_t command_count,
        std::string compilation_cache_directory = {},
        webscene_resource_load_callback resource_load_callback = nullptr,
        void* resource_load_user_data = nullptr,
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
    }

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

    uint8_t animation_frame_demand() const noexcept
    {
        return host_animation_frame_requested_.load(
            std::memory_order_acquire);
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

    bool set_resource_root(const char* value, size_t length)
    {
        if (value == nullptr || length == 0U) return false;
        std::lock_guard lock(configuration_mutex_);
        resource_root_.assign(value, length);
        return true;
    }

    bool load_url(const char* value, size_t length)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(value);
        static_cast<void>(length);
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
        script_work_.emplace_back(url_request{std::string(value, length)});
        signal_worker();
        return true;
#endif
    }
