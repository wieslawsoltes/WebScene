    size_t copy_last_error(char* destination, size_t capacity) const
    {
        std::lock_guard lock(error_mutex_);
        const auto required = last_error_.size() + 1U;
        if (destination != nullptr && capacity > 0) {
            const auto copy_count = std::min(last_error_.size(), capacity - 1U);
            std::copy_n(last_error_.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
    }

    size_t take_host_request(char* destination, size_t capacity)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(destination);
        static_cast<void>(capacity);
        return 0U;
#else
        std::lock_guard lock(host_request_mutex_);
        if (pending_host_request_.empty()
            && (runtime_ == nullptr
                || !runtime_->try_take_host_request(pending_host_request_))) {
            return 0U;
        }
        const auto required = pending_host_request_.size() + 1U;
        if (destination == nullptr || capacity < required) return required;
        std::copy(pending_host_request_.begin(), pending_host_request_.end(), destination);
        destination[pending_host_request_.size()] = '\0';
        pending_host_request_.clear();
        return required;
#endif
    }

    size_t take_console_message(char* destination, size_t capacity)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(destination);
        static_cast<void>(capacity);
        return 0U;
#else
        std::lock_guard lock(console_message_mutex_);
        if (pending_console_message_.empty()
            && (runtime_ == nullptr
                || !runtime_->try_take_console_message(pending_console_message_))) {
            return 0U;
        }
        const auto required = pending_console_message_.size() + 1U;
        if (destination == nullptr || capacity < required) return required;
        std::copy(pending_console_message_.begin(), pending_console_message_.end(), destination);
        destination[pending_console_message_.size()] = '\0';
        pending_console_message_.clear();
        return required;
#endif
    }

    size_t take_input_dispatch_failure(char* destination, size_t capacity)
    {
        std::lock_guard lock(input_dispatch_failure_mutex_);
        if (input_dispatch_failures_.empty()) return 0U;
        const auto& failure = input_dispatch_failures_.front();
        const auto required = failure.size() + 1U;
        if (destination == nullptr || capacity < required) return required;
        std::copy(failure.begin(), failure.end(), destination);
        destination[failure.size()] = '\0';
        input_dispatch_failures_.pop_front();
        return required;
    }

    size_t copy_first_iframe_html(char* destination, size_t capacity) const
    {
        std::lock_guard lock(iframe_html_mutex_);
        const auto required = iframe_html_.size() + 1U;
        if (destination != nullptr && capacity > 0) {
            const auto copy_count = std::min(iframe_html_.size(), capacity - 1U);
            std::copy_n(iframe_html_.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
    }

    size_t copy_scene_diagnostics(char* destination, size_t capacity) const
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        constexpr std::string_view report =
            "certification telemetry disabled at compile time";
        const auto required = report.size() + 1U;
        if (destination != nullptr && capacity > 0) {
            const auto copy_count = std::min(report.size(), capacity - 1U);
            std::copy_n(report.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
#else
        std::lock_guard lock(scene_diagnostics_mutex_);
        const auto required = scene_diagnostics_.size() + 1U;
        if (destination != nullptr && capacity > 0) {
            const auto copy_count = std::min(scene_diagnostics_.size(), capacity - 1U);
            std::copy_n(scene_diagnostics_.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
#endif
    }

    size_t copy_feature_use(char* destination, size_t capacity) const
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const std::string report = R"({"schema":"webscene-native-feature-use-v2","complete":false,"telemetryEnabled":false,"incompleteCategories":["telemetry-disabled"],"observations":[],"compositionDiscovery":{"schema":"webscene-native-composition-use-v1","complete":false,"incompleteCategories":["telemetry-disabled"],"detectors":[],"observations":[]}})";
#elif !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        const std::string report = R"({"schema":"webscene-native-feature-use-v2","complete":false,"incompleteCategories":["v8-runtime"],"observations":[],"compositionDiscovery":{"schema":"webscene-native-composition-use-v1","complete":false,"incompleteCategories":["runtime-not-ready"],"detectors":[],"observations":[]}})";
#else
        const auto report = runtime_ == nullptr
            ? std::string(R"({"schema":"webscene-native-feature-use-v2","complete":false,"incompleteCategories":["runtime-not-ready"],"observations":[],"compositionDiscovery":{"schema":"webscene-native-composition-use-v1","complete":false,"incompleteCategories":["runtime-not-ready"],"detectors":[],"observations":[]}})")
            : runtime_->feature_use_json();
#endif
        const auto required = report.size() + 1U;
        if (destination != nullptr && capacity > 0) {
            const auto copy_count = std::min(report.size(), capacity - 1U);
            std::copy_n(report.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
    }

    size_t copy_event_listener_inventory(char* destination, size_t capacity) const
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const std::string report = R"({"schema":"webscene-event-listener-inventory-v1","complete":false,"telemetryEnabled":false,"targets":[]})";
#elif !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        const std::string report = R"({"schema":"webscene-event-listener-inventory-v1","complete":false,"targets":[]})";
#else
        const auto report = runtime_ == nullptr
            ? std::string(R"({"schema":"webscene-event-listener-inventory-v1","complete":false,"targets":[]})")
            : runtime_->event_listener_inventory_json();
#endif
        const auto required = report.size() + 1U;
        if (destination != nullptr && capacity > 0) {
            const auto copy_count = std::min(report.size(), capacity - 1U);
            std::copy_n(report.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
    }

    size_t copy_canvas_layouts(
        webscene_canvas_layout* destination,
        size_t capacity) const
    {
        std::lock_guard lock(canvas_layout_mutex_);
        const auto required = canvas_layouts_.size();
        if (destination != nullptr && capacity > 0) {
            std::copy_n(
                canvas_layouts_.data(),
                std::min(required, capacity),
                destination);
        }
        return required;
    }

    std::shared_ptr<const scene> acquire_latest()
    {
        auto result = std::atomic_load_explicit(&latest_, std::memory_order_acquire);
        if (result) {
            acquired_scenes_.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    std::shared_ptr<const scene> acquire_next()
    {
        ordered_scene_consumer_.store(true, std::memory_order_release);
        std::lock_guard lock(acknowledgement_->mutex);
        if (acknowledgement_->pending_scenes.empty()) {
            return {};
        }
        acquired_scenes_.fetch_add(1, std::memory_order_relaxed);
        return acknowledgement_->pending_scenes.front();
    }

    bool request_scene_checkpoint()
    {
        {
            std::lock_guard lock(acknowledgement_->mutex);
            acknowledgement_->revision = 0;
            acknowledgement_->dom_hash = 0;
            acknowledgement_->viewport_width = 0;
            acknowledgement_->viewport_height = 0;
            acknowledgement_->layer_versions.clear();
            acknowledgement_->value.reset();
            acknowledgement_->pending_scenes.clear();
        }
        std::atomic_store_explicit(
            &latest_,
            std::shared_ptr<const scene>{},
            std::memory_order_release);
        checkpoint_requested_.store(true, std::memory_order_release);
        signal_worker();
        return true;
    }

    bool release_canvas_export(uint32_t node_id)
    {
        if (node_id == 0U) return false;
        pending_canvas_export_release_id_.store(node_id, std::memory_order_release);
        signal_worker();
        return true;
    }

    std::shared_ptr<acknowledgement_state> acknowledgement_state_handle() const
    {
        return acknowledgement_;
    }
