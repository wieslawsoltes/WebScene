    void apply(const webscene_input_event& event)
    {
        frame_trace_.mark(event.kind == WEBSCENE_INPUT_FRAME ? "frame-start" : "input-start", event.sequence);
        last_input_sequence_ = std::max(last_input_sequence_, event.sequence);
        const auto measures_input_dispatch =
            event.kind != WEBSCENE_INPUT_RESIZE
            && event.kind != WEBSCENE_INPUT_FRAME;
        const auto input_dispatch_started = measures_input_dispatch
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        // The host can advance its clock after the worker's loop-entry sample
        // but before this input is dequeued. Start input-triggered transitions
        // on that clock, rather than immediately expiring them on the next loop.
        // Observing the timeline does not release RAF or publish another frame.
        if (measures_input_dispatch) {
            const auto host_timestamp = observed_host_timestamp_microseconds_.load(
                std::memory_order_acquire);
            if (host_timestamp != 0U) {
                document_.signal_animation_frame(static_cast<double>(host_timestamp) / 1000.0);
            }
        }
        switch (event.kind) {
        case WEBSCENE_INPUT_POINTER_MOVE:
        case WEBSCENE_INPUT_POINTER_DOWN:
        case WEBSCENE_INPUT_POINTER_UP:
        case WEBSCENE_INPUT_POINTER_LEAVE:
            pointer_x_ = event.x;
            pointer_y_ = event.y;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            // A paced pointer's cursor is resolved against the publication
            // layout after RAF. Forcing layout here duplicates that work.
#if defined(_WIN32)
            if (runtime_ != nullptr && !runtime_->dispatch_input(event,
                    event.kind == WEBSCENE_INPUT_POINTER_MOVE
                    && observed_compositor_timestamp_microseconds_.load(std::memory_order_acquire) != 0)) {
#else
            if (runtime_ != nullptr && !runtime_->dispatch_input(event)) {
#endif
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
        frame_trace_.mark(event.kind == WEBSCENE_INPUT_FRAME ? "frame-end" : "input-end", event.sequence);
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
