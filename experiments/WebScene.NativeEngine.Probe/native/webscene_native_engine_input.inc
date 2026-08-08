    void apply(const webscene_input_event& event)
    {
        last_input_sequence_ = std::max(last_input_sequence_, event.sequence);
        const auto measures_input_dispatch =
            event.kind != WEBSCENE_INPUT_RESIZE
            && event.kind != WEBSCENE_INPUT_FRAME;
        const auto input_dispatch_started = measures_input_dispatch
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        switch (event.kind) {
        case WEBSCENE_INPUT_POINTER_MOVE:
        case WEBSCENE_INPUT_POINTER_DOWN:
        case WEBSCENE_INPUT_POINTER_UP:
            pointer_x_ = event.x;
            pointer_y_ = event.y;
            if (dom_inspect_mode_.load(std::memory_order_acquire)) {
                if (document_.dirty()) {
                    document_.layout(
                        static_cast<float>(viewport_width_),
                        static_cast<float>(viewport_height_));
                }
                auto* inspected = document_.hit_test(
                    document_.body(),
                    static_cast<float>(event.x),
                    static_cast<float>(event.y));
                const auto inspected_id = inspected == nullptr ? 0U : inspected->id;
                const auto highlight_changed = dom_highlighted_node_id_.exchange(
                    inspected_id,
                    std::memory_order_acq_rel) != inspected_id;
                auto selection_changed = false;
                if (event.kind == WEBSCENE_INPUT_POINTER_DOWN
                    && inspected_id != 0U) {
                    dom_selected_node_id_.store(
                        inspected_id,
                        std::memory_order_release);
                    dom_selection_sequence_.fetch_add(
                        1U,
                        std::memory_order_acq_rel);
                    selection_changed = true;
                }
                if (highlight_changed || selection_changed) {
                    checkpoint_requested_.store(true, std::memory_order_release);
                }
                current_cursor_.store(
                    WEBSCENE_CURSOR_CROSSHAIR,
                    std::memory_order_release);
                break;
            }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr && !runtime_->dispatch_input(event)) {
                script_errors_.fetch_add(1, std::memory_order_relaxed);
                record_input_dispatch_failure(event, runtime_->last_error());
            }
            if (runtime_ != nullptr) {
                current_cursor_.store(
                    runtime_->current_cursor_kind(),
                    std::memory_order_release);
            }
            update_compilation_metrics();
#endif
            break;
        case WEBSCENE_INPUT_WHEEL:
            scroll_x_ += event.delta_x;
            scroll_y_ += event.delta_y;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr && !runtime_->dispatch_input(event)) {
                script_errors_.fetch_add(1, std::memory_order_relaxed);
                record_input_dispatch_failure(event, runtime_->last_error());
            }
            update_compilation_metrics();
#endif
            break;
        case WEBSCENE_INPUT_KEY_DOWN:
        case WEBSCENE_INPUT_KEY_UP:
        case WEBSCENE_INPUT_TEXT:
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr && !runtime_->dispatch_input(event)) {
                script_errors_.fetch_add(1, std::memory_order_relaxed);
                record_input_dispatch_failure(event, runtime_->last_error());
            }
            update_compilation_metrics();
#endif
            break;
        case WEBSCENE_INPUT_RESIZE:
            viewport_width_ = event.x > 1.0 ? event.x : 1.0;
            viewport_height_ = event.y > 1.0 ? event.y : 1.0;
            device_scale_factor_ = std::isfinite(event.delta_x) && event.delta_x > 0.0
                ? event.delta_x
                : 1.0;
            document_.mark_dirty();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            {
                const auto started = std::chrono::steady_clock::now();
                if (runtime_ != nullptr && !runtime_->dispatch_resize()) {
                    script_errors_.fetch_add(1, std::memory_order_relaxed);
                    set_last_error(runtime_->last_error());
                }
                if (runtime_ != nullptr) {
                    last_resize_outer_listeners_nanoseconds_.store(
                        runtime_->last_resize_outer_listeners_nanoseconds(),
                        std::memory_order_relaxed);
                    last_resize_frame_listeners_nanoseconds_.store(
                        runtime_->last_resize_frame_listeners_nanoseconds(),
                        std::memory_order_relaxed);
                    last_resize_layout_nanoseconds_.store(
                        runtime_->last_resize_layout_nanoseconds(),
                        std::memory_order_relaxed);
                    last_resize_observers_nanoseconds_.store(
                        runtime_->last_resize_observers_nanoseconds(),
                        std::memory_order_relaxed);
                }
                last_resize_dispatch_nanoseconds_.store(
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started).count()),
                    std::memory_order_relaxed);
            }
#endif
            break;
        case WEBSCENE_INPUT_FRAME:
            {
                const auto started = std::chrono::steady_clock::now();
                document_.signal_animation_frame(event.x);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
                if (runtime_ != nullptr) {
                    runtime_->signal_animation_frame(event.x);
                    update_compilation_metrics();
                }
#endif
                const auto elapsed = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started).count());
                dispatched_animation_frames_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                total_animation_frame_dispatch_nanoseconds_.fetch_add(
                    elapsed,
                    std::memory_order_relaxed);
                last_animation_frame_dispatch_nanoseconds_.store(
                    elapsed,
                    std::memory_order_relaxed);
                store_maximum(
                    maximum_animation_frame_dispatch_nanoseconds_,
                    elapsed);
                const auto timestamp_microseconds = event.x > 0
                    && std::isfinite(event.x)
                    ? static_cast<uint64_t>(event.x * 1000.0)
                    : 0U;
                last_animation_frame_timestamp_microseconds_.store(
                    timestamp_microseconds,
                    std::memory_order_relaxed);
            }
            break;
        default:
            break;
        }
        if (measures_input_dispatch) {
            const auto elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - input_dispatch_started).count());
            last_input_dispatch_nanoseconds_.store(
                elapsed,
                std::memory_order_relaxed);
            last_input_dispatch_sequence_.store(
                event.sequence,
                std::memory_order_release);
            dispatched_inputs_.fetch_add(1, std::memory_order_relaxed);
            total_input_dispatch_nanoseconds_.fetch_add(
                elapsed,
                std::memory_order_relaxed);
            store_maximum(maximum_input_dispatch_nanoseconds_, elapsed);
        }
    }
