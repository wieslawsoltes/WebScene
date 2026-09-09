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

    void set_producer_gpu_wait_consumer(bool enabled) {
        producer_gpu_wait_consumer_.store(enabled,std::memory_order_release);
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
        acknowledgement_->reset_for_checkpoint();
        std::atomic_store_explicit(
            &latest_,
            std::shared_ptr<const scene>{},
            std::memory_order_release);
        checkpoint_requested_.store(true, std::memory_order_release);
        signal_worker();
        return true;
    }

    bool submit_canvas_checkpoint(uint32_t node_id, uint64_t generation,
        uint32_t command_count, const char* payload, size_t length) {
        if (!node_id || !command_count || !payload || !length || length > 32U*1024U*1024U) return false;
        { std::lock_guard lock(canvas_checkpoint_mutex_);
          if (canvas_checkpoint_) return false;
          canvas_checkpoint_ = canvas_checkpoint_request{node_id,command_count,generation,std::string(payload,length)};
        }
        signal_worker(); return true;
    }

    bool apply_canvas_checkpoint() {
        std::optional<canvas_checkpoint_request> request;
        { std::lock_guard lock(canvas_checkpoint_mutex_); request.swap(canvas_checkpoint_); }
        if (!request) return false;
        auto* node = document_.find_by_native_id(request->node_id);
        if (!node || node->tag != "canvas") return false;
        auto& canvas = node->mutable_canvas();
        if (canvas.generation != request->generation || request->command_count > canvas.commands.size()) return false;
        // A source canvas command captures generation/index metadata. Do not
        // rebase a dependent canvas until that protocol supports checkpoints.
        if (!canvas.canvas_dependencies.empty() || document_.has_canvas_references(node->id)) return false;
        std::vector<webscene_canvas_command> commands;
        commands.reserve(1 + canvas.commands.size() - request->command_count);
        webscene_canvas_command checkpoint{};
        checkpoint.kind = WEBSCENE_CANVAS_COMMAND_RASTER_CHECKPOINT;
        commands.push_back(checkpoint);
        commands.insert(commands.end(),canvas.commands.begin()+request->command_count,canvas.commands.end());
        std::vector<std::string> strings;
        strings.push_back(std::move(request->payload));
        std::unordered_map<std::string,uint32_t> indices;
        indices.emplace(strings.front(),0);
        for(size_t index=1;index<commands.size();++index) {
            auto& command=commands[index];
            switch(command.kind) {
            case 25: case 26: case 28: case 29: case 31:
            case 40: case 41: case 43: case 44: case 48: case 49: case 50:
            case 52: case 53: case 54:
                if(command.resource_id>=canvas.strings.size()) return false;
                { const auto& value=canvas.strings[command.resource_id];
                  auto [found,inserted]=indices.emplace(value,static_cast<uint32_t>(strings.size()));
                  if(inserted)strings.push_back(value);
                  command.resource_id=found->second;
                }
                break;
            case 27: case WEBSCENE_CANVAS_COMMAND_RASTER_CHECKPOINT: return false;
            }
        }
        canvas.commands.swap(commands);canvas.strings.swap(strings);canvas.string_indices.swap(indices);
        ++canvas.generation;
        document_.mark_scene_changed();
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
