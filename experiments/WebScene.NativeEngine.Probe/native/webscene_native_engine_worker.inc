    void run(std::stop_token token)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        runtime_ = std::make_unique<webscene_native::v8_dom_runtime>(
            document_,
            [this] {
                return webscene_native::v8_dom_runtime::viewport_metrics{
                    static_cast<float>(viewport_width_),
                    static_cast<float>(viewport_height_),
                    device_scale_factor_,
                    preferred_color_scheme_.load(std::memory_order_acquire)};
            },
            compilation_cache_directory_,
            [this](
                uint32_t kind,
                const std::string& url,
                const std::string& entity_tag,
                int64_t last_modified_unix_seconds,
                webscene_native::v8_dom_runtime::resource_response& response) {
                if (resource_load_callback_ == nullptr) return false;
                const auto required = resource_load_callback_(
                    resource_load_user_data_,
                    kind,
                    url.data(),
                    url.size(),
                    entity_tag.data(),
                    entity_tag.size(),
                    last_modified_unix_seconds,
                    nullptr,
                    0U);
                constexpr size_t envelope_header_size =
                    2U + sizeof(uint32_t) + sizeof(int64_t) + sizeof(int64_t);
                if (required < envelope_header_size) return false;
                std::vector<char> buffer(required);
                if (resource_load_callback_(
                        resource_load_user_data_,
                        kind,
                        url.data(),
                        url.size(),
                        entity_tag.data(),
                        entity_tag.size(),
                        last_modified_unix_seconds,
                        buffer.data(),
                        buffer.size()) != required) {
                    return false;
                }
                const auto status = static_cast<uint8_t>(buffer[0]);
                const auto cacheable = static_cast<uint8_t>(buffer[1]) != 0U;
                uint32_t response_tag_length = 0U;
                int64_t response_last_modified = 0;
                int64_t response_fresh_until = 0;
                std::memcpy(&response_tag_length, buffer.data() + 2U, sizeof(response_tag_length));
                std::memcpy(
                    &response_last_modified,
                    buffer.data() + 2U + sizeof(response_tag_length),
                    sizeof(response_last_modified));
                std::memcpy(
                    &response_fresh_until,
                    buffer.data() + 2U + sizeof(response_tag_length) + sizeof(response_last_modified),
                    sizeof(response_fresh_until));
                if (status < 1U || status > 2U
                    || response_tag_length > required - envelope_header_size) return false;
                response.not_modified = status == 2U;
                response.cacheable = cacheable;
                response.last_modified_unix_seconds = response_last_modified;
                response.fresh_until_unix_seconds = response_fresh_until;
                response.entity_tag.assign(
                    buffer.data() + envelope_header_size,
                    response_tag_length);
                response.content.assign(
                    buffer.data() + envelope_header_size + response_tag_length,
                    required - envelope_header_size - response_tag_length);
                return true;
            },
            [this] {
                if (host_request_available_callback_ != nullptr) {
                    host_request_available_callback_(
                        host_request_available_user_data_);
                }
            },
            [this] {
                if (interop_callback_available_callback_ != nullptr) {
                    interop_callback_available_callback_(
                        interop_callback_available_user_data_);
                }
            },
            [this](
                    webscene_native::interop_callback_request_data_v3&&
                        callback) {
                return enqueue_callback_v3(std::move(callback));
            });
        runtime_->set_work_metrics_enabled(
            runtime_work_metrics_enabled_.load(std::memory_order_acquire));
        if (!runtime_->initialize()) {
            set_last_error(runtime_->last_error());
            runtime_.reset();
        } else {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            inspector_runtime_.store(runtime_.get(), std::memory_order_release);
#endif
        }
#endif
        publish_scene();
        auto next_scene_publication = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(16);
        bool scene_pending = false;
        std::optional<webscene_input_event> deferred_input;
        struct pending_resize_frame_publication final {
            uint64_t sequence;
            std::chrono::steady_clock::time_point enqueued_at;
        };
        std::optional<pending_resize_frame_publication>
            pending_paired_resize_publication;
        std::optional<std::chrono::steady_clock::time_point>
            hidden_low_memory_deadline;
        uint32_t drag_moves_to_preserve = 0;
        while (!token.stop_requested()) {
            webscene_input_event event{};
            bool changed = checkpoint_requested_.exchange(
                false,
                std::memory_order_acq_rel);
            const auto starting_scene_generation =
                document_.scene_generation();
            const auto starting_component_ready =
                component_ready_.load(std::memory_order_relaxed);
            bool resize_applied = false;
            bool host_frame_applied = false;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (visibility_changed_.exchange(false, std::memory_order_acq_rel)) {
                const auto host_visible = host_visible_.load(std::memory_order_acquire);
                if (runtime_ != nullptr
                    && !runtime_->set_visible(host_visible)) {
                    script_errors_.fetch_add(1, std::memory_order_relaxed);
                    set_last_error(runtime_->last_error());
                }
                if (host_visible) {
                    hidden_low_memory_deadline.reset();
                } else {
                    // Avoid collecting during a transient reparent/tab switch.
                    // The worker remains responsive and visibility restoration
                    // cancels the collection before this deadline.
                    hidden_low_memory_deadline =
                        std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(500);
                }
            }
            if (low_memory_requested_.exchange(false, std::memory_order_acq_rel)
                && runtime_ != nullptr) {
                runtime_->notify_low_memory();
                low_memory_notifications_.fetch_add(1, std::memory_order_relaxed);
                update_compilation_metrics();
            }
            if (hidden_low_memory_deadline.has_value()
                && std::chrono::steady_clock::now()
                    >= *hidden_low_memory_deadline
                && runtime_ != nullptr) {
                runtime_->notify_low_memory();
                low_memory_notifications_.fetch_add(1, std::memory_order_relaxed);
                hidden_low_memory_notifications_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                update_compilation_metrics();
                hidden_low_memory_deadline.reset();
            }
            if (preferred_color_scheme_changed_.exchange(
                    false,
                    std::memory_order_acq_rel)
                && runtime_ != nullptr) {
                if (!runtime_->refresh_media_environment()) {
                    script_errors_.fetch_add(1, std::memory_order_relaxed);
                    set_last_error(runtime_->last_error());
                }
                changed = true;
            }
#endif
            // Treat the latest coalesced viewport as a barrier for pointer work.
            // Avalonia reports pointer coordinates in the newly arranged control,
            // so dispatching queued input against the preceding DOM viewport makes
            // hit testing appear to stop working after a live window resize.
            uint64_t resize_input_count = 0;
            webscene_input_event paired_resize_frame{};
            uint64_t paired_resize_frame_count = 0;
            bool has_paired_resize_frame = false;
            std::chrono::steady_clock::time_point paired_resize_frame_enqueued_at;
            if (take_latest_resize(
                    event,
                    resize_input_count,
                    paired_resize_frame,
                    paired_resize_frame_count,
                    has_paired_resize_frame,
                    paired_resize_frame_enqueued_at)) {
                const auto paired_dispatch_started =
                    std::chrono::steady_clock::now();
                apply(event);
                applied_resize_inputs_.fetch_add(1, std::memory_order_relaxed);
                resize_applied = true;
                changed = true;
                if (has_paired_resize_frame) {
                    // Resize listeners and ResizeObserver delivery may queue RAF.
                    // Release it before this rendering boundary is published.
                    apply(paired_resize_frame);
                    applied_animation_frames_.fetch_add(1, std::memory_order_relaxed);
                    host_frame_applied = true;
                    changed = changed || document_.dirty();
                    if (paired_resize_frame_count > 1) {
                        coalesced_animation_frames_.fetch_add(
                            paired_resize_frame_count - 1,
                            std::memory_order_relaxed);
                    }
                    const auto paired_dispatch_completed =
                        std::chrono::steady_clock::now();
                    const auto queue_nanoseconds = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            paired_dispatch_started
                                - paired_resize_frame_enqueued_at).count());
                    const auto dispatch_nanoseconds = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            paired_dispatch_completed
                                - paired_dispatch_started).count());
                    applied_resize_frame_pairs_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    total_resize_frame_queue_nanoseconds_.fetch_add(
                        queue_nanoseconds,
                        std::memory_order_relaxed);
                    last_resize_frame_queue_nanoseconds_.store(
                        queue_nanoseconds,
                        std::memory_order_relaxed);
                    store_maximum(
                        maximum_resize_frame_queue_nanoseconds_,
                        queue_nanoseconds);
                    total_resize_frame_dispatch_nanoseconds_.fetch_add(
                        dispatch_nanoseconds,
                        std::memory_order_relaxed);
                    last_resize_frame_dispatch_nanoseconds_.store(
                        dispatch_nanoseconds,
                        std::memory_order_relaxed);
                    store_maximum(
                        maximum_resize_frame_dispatch_nanoseconds_,
                        dispatch_nanoseconds);
                    pending_paired_resize_publication =
                        pending_resize_frame_publication{
                            event.sequence,
                            paired_resize_frame_enqueued_at};
                } else if (paired_resize_frame_count != 0) {
                    coalesced_animation_frames_.fetch_add(
                        paired_resize_frame_count,
                        std::memory_order_relaxed);
                }
                consumed_inputs_.fetch_add(
                    resize_input_count + paired_resize_frame_count,
                    std::memory_order_relaxed);
            }

            const auto input_batch_started = std::chrono::steady_clock::now();
            uint32_t applied_input_groups = 0;
            while (applied_input_groups < 8U) {
                // Yield as soon as another live-resize update arrives. The next
                // worker iteration applies it before consuming more pointer input.
                if (resize_pending_.load(std::memory_order_acquire)) {
                    break;
                }
                if (deferred_input.has_value()) {
                    event = *deferred_input;
                    deferred_input.reset();
                } else if (!inputs_.try_pop(event)) {
                    break;
                }

                const auto pressed_pointer_move = event.kind == WEBSCENE_INPUT_POINTER_MOVE
                    && (event.flags & 1U) != 0U;
                const auto threshold_drag_move = pressed_pointer_move
                    && drag_moves_to_preserve != 0U;
                const auto may_coalesce = event.kind == WEBSCENE_INPUT_FRAME
                    || event.kind == WEBSCENE_INPUT_WHEEL
                    || (event.kind == WEBSCENE_INPUT_POINTER_MOVE
                        && (!pressed_pointer_move || drag_moves_to_preserve == 0U));
                if (may_coalesce) {
                    // Continuous device input commonly arrives as alternating
                    // wheel, hover-move, and display-frame records. Coalescing
                    // only adjacent records of the same kind lets that stream
                    // grow without bound whenever a component synchronously
                    // reads layout from its handlers. Consume a bounded
                    // continuous prefix instead: wheel deltas accumulate,
                    // pointer movement is latest-position-wins, and only the
                    // newest display timestamp is relevant. Discrete input
                    // remains an ordering barrier.
                    std::optional<webscene_input_event> frame;
                    std::optional<webscene_input_event> pointer_move;
                    std::optional<webscene_input_event> wheel;
                    std::optional<uint64_t> frame_order;
                    std::optional<uint64_t> pointer_move_order;
                    std::optional<uint64_t> wheel_order;
                    uint64_t next_aggregate_order = 0;
                    uint64_t frame_count = 0;
                    uint64_t pointer_move_count = 0;
                    uint64_t wheel_count = 0;
                    const auto accumulate = [&](
                        const webscene_input_event& candidate) -> bool {
                        const auto order = next_aggregate_order++;
                        if (candidate.kind == WEBSCENE_INPUT_FRAME) {
                            frame = candidate;
                            frame_order = order;
                            ++frame_count;
                            return true;
                        }
                        if (candidate.kind == WEBSCENE_INPUT_POINTER_MOVE) {
                            if (pointer_move.has_value()
                                && pointer_move->flags != candidate.flags) {
                                return false;
                            }
                            pointer_move = candidate;
                            pointer_move_order = order;
                            ++pointer_move_count;
                            return true;
                        }
                        if (candidate.kind == WEBSCENE_INPUT_WHEEL) {
                            const auto direction = [](const webscene_input_event& value) {
                                return value.delta_y > 0 ? 1
                                    : value.delta_y < 0 ? -1
                                    : value.delta_x > 0 ? 1
                                    : value.delta_x < 0 ? -1 : 0;
                            };
                            if (wheel.has_value()
                                && (wheel->flags != candidate.flags
                                    || (direction(*wheel) != 0
                                        && direction(candidate) != 0
                                        && direction(*wheel) != direction(candidate))
                                    || std::abs(wheel->x - candidate.x) > 4.0
                                    || std::abs(wheel->y - candidate.y) > 4.0)) {
                                return false;
                            }
                            if (!wheel.has_value()) {
                                wheel = candidate;
                            } else {
                                wheel->delta_x += candidate.delta_x;
                                wheel->delta_y += candidate.delta_y;
                                wheel->sequence = std::max(
                                    wheel->sequence,
                                    candidate.sequence);
                                wheel->x = candidate.x;
                                wheel->y = candidate.y;
                            }
                            wheel_order = order;
                            ++wheel_count;
                            return true;
                        }
                        return false;
                    };

                    static_cast<void>(accumulate(event));
                    uint64_t consumed_count = 1;
                    webscene_input_event next{};
                    while (consumed_count < 256U && inputs_.try_pop(next)) {
                        if (!accumulate(next)) {
                            deferred_input = next;
                            break;
                        }
                        ++consumed_count;
                    }

                    struct continuous_aggregate final {
                        webscene_input_event event;
                        uint64_t count;
                        uint64_t order;
                    };
                    std::vector<continuous_aggregate> aggregates;
                    if (frame.has_value()) {
                        aggregates.push_back(
                            continuous_aggregate{
                                *frame,
                                frame_count,
                                frame_order.value()});
                    }
                    if (pointer_move.has_value()) {
                        aggregates.push_back(
                            continuous_aggregate{
                                *pointer_move,
                                pointer_move_count,
                                pointer_move_order.value()});
                    }
                    if (wheel.has_value()) {
                        aggregates.push_back(
                            continuous_aggregate{
                                *wheel,
                                wheel_count,
                                wheel_order.value()});
                    }
                    std::sort(
                        aggregates.begin(),
                        aggregates.end(),
                        [](const auto& left, const auto& right) {
                            return left.order < right.order;
                        });
                    for (const auto& item : aggregates) {
                        const auto& aggregate = item.event;
                        const auto aggregate_count = item.count;
                        apply(aggregate);
                        if (aggregate.kind == WEBSCENE_INPUT_POINTER_MOVE) {
                            applied_pointer_move_inputs_.fetch_add(
                                1,
                                std::memory_order_relaxed);
                            if (aggregate_count > 1) {
                                coalesced_pointer_move_inputs_.fetch_add(
                                    aggregate_count - 1,
                                    std::memory_order_relaxed);
                            }
                        } else if (aggregate.kind == WEBSCENE_INPUT_WHEEL) {
                            applied_wheel_inputs_.fetch_add(
                                1,
                                std::memory_order_relaxed);
                            if (aggregate_count > 1) {
                                coalesced_wheel_inputs_.fetch_add(
                                    aggregate_count - 1,
                                std::memory_order_relaxed);
                            }
                        } else if (aggregate.kind == WEBSCENE_INPUT_FRAME) {
                            applied_animation_frames_.fetch_add(
                                1,
                                std::memory_order_relaxed);
                            host_frame_applied = true;
                            if (aggregate_count > 1) {
                                coalesced_animation_frames_.fetch_add(
                                    aggregate_count - 1,
                                    std::memory_order_relaxed);
                            }
                        }
                        changed = changed
                            || aggregate.kind != WEBSCENE_INPUT_FRAME
                            || document_.dirty();
                    }
                    consumed_inputs_.fetch_add(
                        consumed_count,
                        std::memory_order_relaxed);
                    applied_input_groups += static_cast<uint32_t>(aggregates.size());
                    if (std::chrono::steady_clock::now() - input_batch_started
                        >= std::chrono::milliseconds(4)) {
                        break;
                    }
                    continue;
                }

                apply(event);
                if (event.kind == WEBSCENE_INPUT_POINTER_DOWN) {
                    // Gesture recognizers commonly use the first two pressed moves
                    // to cross the drag threshold. Yield between them, then
                    // make any accumulated moves latest-position-wins.
                    drag_moves_to_preserve = 2U;
                } else if (pressed_pointer_move && drag_moves_to_preserve != 0U) {
                    --drag_moves_to_preserve;
                } else if (event.kind == WEBSCENE_INPUT_POINTER_UP) {
                    drag_moves_to_preserve = 0U;
                }
                consumed_inputs_.fetch_add(1, std::memory_order_relaxed);
                if (event.kind == WEBSCENE_INPUT_POINTER_MOVE) {
                    applied_pointer_move_inputs_.fetch_add(1, std::memory_order_relaxed);
                } else if (event.kind == WEBSCENE_INPUT_WHEEL) {
                    applied_wheel_inputs_.fetch_add(1, std::memory_order_relaxed);
                }
                // A frame input only releases queued requestAnimationFrame work.
                // The task pump below marks the scene dirty if a callback ran;
                // idle display frames should not force scene publication.
                changed = changed || event.kind != WEBSCENE_INPUT_FRAME || document_.dirty();
                ++applied_input_groups;
                if (threshold_drag_move
                    || std::chrono::steady_clock::now() - input_batch_started
                    >= std::chrono::milliseconds(4)) {
                    break;
                }
            }

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr && resize_applied && host_frame_applied) {
                // `signal_animation_frame` releases the callbacks belonging to
                // this rendering opportunity; execute that complete RAF batch
                // before publishing. Do not run unrelated resources/timers here,
                // and do not release RAF callbacks scheduled by a callback itself:
                // those retain the sentinel deadline for the next host frame.
                const auto animation_frame_batch_started =
                    std::chrono::steady_clock::now();
                uint64_t animation_frame_callbacks = 0;
                while (runtime_->has_pending_animation_frame_task()) {
                    if (!runtime_->pump_animation_frame_task()) {
                        script_errors_.fetch_add(1, std::memory_order_relaxed);
                        set_last_error(runtime_->last_error());
                        break;
                    }
                    ++animation_frame_callbacks;
                    changed = true;
                }
                const auto animation_frame_batch_nanoseconds =
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now()
                                - animation_frame_batch_started).count());
                resize_frame_animation_callbacks_.fetch_add(
                    animation_frame_callbacks,
                    std::memory_order_relaxed);
                total_resize_frame_animation_batch_nanoseconds_.fetch_add(
                    animation_frame_batch_nanoseconds,
                    std::memory_order_relaxed);
                last_resize_frame_animation_batch_nanoseconds_.store(
                    animation_frame_batch_nanoseconds,
                    std::memory_order_relaxed);
                store_maximum(
                    maximum_resize_frame_animation_batch_nanoseconds_,
                    animation_frame_batch_nanoseconds);
                frame_scripts_executed_.store(
                    runtime_->frame_scripts_executed(),
                    std::memory_order_relaxed);
                frame_script_errors_.store(
                    runtime_->frame_script_errors(),
                    std::memory_order_relaxed);
                update_compilation_metrics();
                update_component_readiness();
            }
            if (runtime_ != nullptr
                && runtime_->has_pending_tasks()
                && !(resize_applied && host_frame_applied)) {
                // Resource discovery during component startup can produce many
                // immediately runnable stylesheet/script tasks. Processing one
                // and then sleeping for 2 ms added hundreds of milliseconds
                // that browsers do not impose. Drain a bounded batch while no
                // input/resize work is waiting; the time/count caps preserve a
                // render opportunity and keep interaction latency bounded.
                // A paired live-resize RAF is already the current rendering
                // opportunity. Arbitrary queued macrotasks run after that scene
                // is published instead of delaying the visual boundary.
                constexpr uint32_t maximum_task_batch = 32U;
                constexpr auto maximum_task_batch_time = std::chrono::milliseconds(4);
                const auto task_batch_started = std::chrono::steady_clock::now();
                uint32_t pumped_tasks = 0;
                auto task_succeeded = true;
                do {
                    task_succeeded = runtime_->pump_task();
                    ++pumped_tasks;
                    if (!task_succeeded) break;
                } while (pumped_tasks < maximum_task_batch
                    && runtime_->has_pending_tasks()
                    && !deferred_input.has_value()
                    && inputs_.empty()
                    && !resize_pending_.load(std::memory_order_acquire)
                    && std::chrono::steady_clock::now() - task_batch_started
                        < maximum_task_batch_time);
                if (!task_succeeded) {
                    script_errors_.fetch_add(1, std::memory_order_relaxed);
                    set_last_error(runtime_->last_error());
                }
                frame_scripts_executed_.store(runtime_->frame_scripts_executed(), std::memory_order_relaxed);
                frame_script_errors_.store(runtime_->frame_script_errors(), std::memory_order_relaxed);
                update_compilation_metrics();
                update_component_readiness();
            }

            if (document_.has_active_animations()) {
                const auto animation_started = std::chrono::steady_clock::now();
                if (document_.advance_animations()) {
                    changed = true;
                }
                last_animation_advance_nanoseconds_.store(
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - animation_started).count()),
                    std::memory_order_relaxed);
            }
            if (runtime_ != nullptr && !runtime_->dispatch_transition_events()) {
                script_errors_.fetch_add(1, std::memory_order_relaxed);
                set_last_error(runtime_->last_error());
            }

            if (runtime_ != nullptr) {
                std::lock_guard lock(configuration_mutex_);
                runtime_->set_resource_root(resource_root_);
            }
            std::deque<script_work_request> script_work;
            {
                std::lock_guard lock(script_mutex_);
                script_work.swap(script_work_);
            }
            for (auto iterator = script_work.begin();
                 iterator != script_work.end();
                 ++iterator) {
                auto& request = *iterator;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                if (std::holds_alternative<inspector_pump_work>(request)) {
                    if (auto* inspector_state = inspector_state_.load(
                            std::memory_order_acquire)) {
                        inspector_state->pump_enqueued.store(
                            false,
                            std::memory_order_release);
                    }
                    if (runtime_ != nullptr) {
                        constexpr uint32_t maximum_inspector_batch = 64U;
                        uint32_t inspector_tasks = 0U;
                        while (inspector_tasks < maximum_inspector_batch
                            && runtime_->has_pending_inspector_tasks()) {
                            if (!runtime_->pump_inspector_task(token)) break;
                            ++inspector_tasks;
                        }
                        if (runtime_->has_pending_inspector_tasks()) {
                            enqueue_inspector_pump();
                        }
                    }
                    continue;
                }
#endif
                if (auto* url = std::get_if<url_request>(&request)) {
                    if (runtime_ != nullptr
                        && runtime_->load_url(
                            url->url,
                            std::move(url->document_start_scripts))) {
                        native_scene_active_ = true;
                        frame_scripts_executed_.store(
                            runtime_->frame_scripts_executed(),
                            std::memory_order_relaxed);
                        frame_script_errors_.store(
                            runtime_->frame_script_errors(),
                            std::memory_order_relaxed);
                        update_compilation_metrics();
                        set_last_error(runtime_->frame_script_errors() == 0
                            ? std::string{}
                            : runtime_->frame_last_error());
                    } else {
                        script_errors_.fetch_add(1, std::memory_order_relaxed);
                        set_last_error(runtime_ == nullptr
                            ? "V8 runtime is unavailable"
                            : runtime_->last_error());
                        update_compilation_metrics();
                    }
                    changed = true;
                    continue;
                }
                if (auto* script = std::get_if<script_request>(&request)) {
                    if (runtime_ != nullptr
                        && runtime_->execute(
                            script->source,
                            script->document_name)) {
                        executed_scripts_.fetch_add(1, std::memory_order_relaxed);
                        native_scene_active_ = true;
                        frame_scripts_executed_.store(
                            runtime_->frame_scripts_executed(),
                            std::memory_order_relaxed);
                        frame_script_errors_.store(
                            runtime_->frame_script_errors(),
                            std::memory_order_relaxed);
                        update_compilation_metrics();
                        update_component_readiness();
                        set_last_error(runtime_->frame_script_errors() == 0
                            ? std::string{}
                            : runtime_->frame_last_error());
                    } else {
                        script_errors_.fetch_add(1, std::memory_order_relaxed);
                        set_last_error(runtime_ == nullptr
                            ? "V8 runtime is unavailable"
                            : runtime_->last_error());
                        update_compilation_metrics();
                    }
                    changed = true;
                    continue;
                }
                if (auto* cancellation =
                        std::get_if<interop_cancel_work_v3>(&request)) {
                    if (runtime_ != nullptr) {
                        runtime_->cancel_interop_v3(
                            cancellation->operation_id);
                    }
                    {
                        std::lock_guard lock(interop_mutex_);
                        return_interop_operation_locked(
                            cancellation->operation);
                    }
                    continue;
                }
                if (auto* completion =
                        std::get_if<
                            interop_callback_completion_work_v3>(&request)) {
                    auto record = std::move(completion->completion);
                    if (runtime_ != nullptr
                        && !runtime_->complete_callback_v3(
                            *record)) {
                        set_last_error(runtime_->last_error());
                    }
                    interop_callback_completion_pool_.release(
                        std::move(record));
                    continue;
                }
                if (auto* cancellation =
                        std::get_if<
                            interop_callback_cancel_work_v3>(&request)) {
                    if (runtime_ != nullptr) {
                        runtime_->cancel_callback_v3(
                            cancellation->call_id);
                    }
                    continue;
                }
                if (auto* interop = std::get_if<interop_invoke_work_v3>(&request)) {
                    if (interop->operation->cancelled.load(
                            std::memory_order_acquire)) {
                        return_interop_request(
                            std::move(interop->request));
                        continue;
                    }
                    if (consumed_inputs_.load(std::memory_order_acquire)
                        < interop->required_consumed_inputs) {
                        std::lock_guard lock(script_mutex_);
                        script_work_.insert(
                            script_work_.begin(),
                            std::make_move_iterator(iterator),
                            std::make_move_iterator(script_work.end()));
                        break;
                    }

                    webscene_native::interop_result_data_v3 result;
                    auto state =
                        webscene_native::interop_invoke_state_v3::failed;
                    if (runtime_ == nullptr) {
                        result.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
                        result.error = "V8 runtime is unavailable";
                    } else {
                        const std::weak_ptr<interop_operation_v3> operation =
                            interop->operation;
                        state = runtime_->invoke_interop_v3(
                            interop->request,
                            result,
                            interop->operation->id,
                            [this, operation](
                                    webscene_native::interop_result_data_v3&&
                                        completed_result) {
                                const auto retained = operation.lock();
                                if (!retained) return;
                                complete_interop_operation(
                                    retained,
                                    std::move(completed_result));
                            });
                    }
                    if (state
                        == webscene_native::interop_invoke_state_v3::pending) {
                        return_interop_request(
                            std::move(interop->request));
                        update_compilation_metrics();
                        continue;
                    }
                    if (state
                            == webscene_native::interop_invoke_state_v3::failed
                        && result.error.empty()) {
                        result.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
                        result.error = runtime_ == nullptr
                            ? "V8 runtime is unavailable"
                            : runtime_->last_error();
                    }
                    if (state
                        == webscene_native::interop_invoke_state_v3::completed) {
                        update_component_readiness();
                    }
                    complete_interop_operation(
                        interop->operation,
                        std::move(result));
                    return_interop_request(
                        std::move(interop->request));
                    update_compilation_metrics();
                    continue;
                }
                if (auto* interop = std::get_if<interop_evaluate_work_v3>(&request)) {
                    if (interop->operation->cancelled.load(
                            std::memory_order_acquire)) {
                        continue;
                    }
                    if (consumed_inputs_.load(std::memory_order_acquire)
                        < interop->required_consumed_inputs) {
                        std::lock_guard lock(script_mutex_);
                        script_work_.insert(
                            script_work_.begin(),
                            std::make_move_iterator(iterator),
                            std::make_move_iterator(script_work.end()));
                        break;
                    }

                    auto result = interop_result_pool_->acquire();
                    const auto succeeded = runtime_ != nullptr
                        && runtime_->evaluate_interop_v3(
                            interop->source,
                            interop->document_name,
                            result->data);
                    if (runtime_ == nullptr) {
                        result->data.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
                        result->data.error = "V8 runtime is unavailable";
                    } else if (!succeeded && result->data.error.empty()) {
                        result->data.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
                        result->data.error = runtime_->last_error();
                    }
                    if (succeeded) update_component_readiness();
                    result->publish(interop->operation->id);

                    webscene_interop_completed_callback_v3 completed = nullptr;
                    void* completed_user_data = nullptr;
                    {
                        std::lock_guard lock(interop_mutex_);
                        const auto pending = interop_operations_.find(
                            interop->operation->id);
                        if (pending != interop_operations_.end()
                            && pending->second == interop->operation
                            && !interop->operation->cancelled.load(
                                std::memory_order_acquire)) {
                            interop->operation->result = std::move(result);
                            interop->operation->ready.store(
                                true,
                                std::memory_order_release);
                            completed = interop->operation->completed;
                            completed_user_data = interop->operation->user_data;
                        }
                    }
                    if (result) {
                        auto pool = result->owner;
                        pool->release(std::move(result));
                    }
                    if (completed != nullptr) {
                        completed(
                            completed_user_data,
                            interop->operation->id);
                    }
                    update_compilation_metrics();
                    continue;
                }
                continue;
            }
#endif

            update_host_animation_frame_demand();
            changed = changed
                || document_.scene_generation() != starting_scene_generation
                || component_ready_.load(std::memory_order_relaxed)
                    != starting_component_ready;
            scene_pending = scene_pending || changed;
            const auto now = std::chrono::steady_clock::now();
            // A resize and its host RAF are one browser rendering opportunity.
            // Do not defer that completed boundary behind a producer-only phase:
            // macOS live resize may not deliver another compositor callback until
            // the following display slot.
            const auto resize_frame_boundary =
                resize_applied && host_frame_applied;
            if (scene_pending
                && host_visible_.load(std::memory_order_acquire)
                && (resize_frame_boundary || now >= next_scene_publication)) {
                if (publish_scene()) {
                    scene_pending = false;
                    if (pending_paired_resize_publication.has_value()
                        && last_input_sequence_
                            >= pending_paired_resize_publication->sequence) {
                        const auto elapsed = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now()
                                    - pending_paired_resize_publication
                                        ->enqueued_at).count());
                        published_resize_frame_pairs_.fetch_add(
                            1,
                            std::memory_order_relaxed);
                        total_resize_frame_to_publication_nanoseconds_.fetch_add(
                            elapsed,
                            std::memory_order_relaxed);
                        last_resize_frame_to_publication_nanoseconds_.store(
                            elapsed,
                            std::memory_order_relaxed);
                        store_maximum(
                            maximum_resize_frame_to_publication_nanoseconds_,
                            elapsed);
                        pending_paired_resize_publication.reset();
                    }
                    // Keep the producer on a stable 16 ms cadence. Scheduling
                    // from the completion time adds dispatch/layout cost to
                    // every interval and drifts a nominal 60 Hz resize stream
                    // toward 30-45 Hz even when each frame is comfortably
                    // inside budget.
                    constexpr auto scene_interval = std::chrono::milliseconds(16);
                    if (next_scene_publication <= now) {
                        const auto overdue = now - next_scene_publication;
                        next_scene_publication += scene_interval
                            * (overdue / scene_interval + 1);
                    } else {
                        next_scene_publication += scene_interval;
                    }
                    if (resize_frame_boundary) {
                        next_scene_publication = now + scene_interval;
                    }
                    continue;
                }
            }

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            // The loop above has already bounded task work and checked input,
            // resize, animation, and publication. If another task is runnable,
            // start the next fair batch immediately instead of adding a fixed
            // 2 ms delay between local bundle/resource tasks.
            if (runtime_ != nullptr && runtime_->has_pending_tasks()) {
                continue;
            }
#endif

            // Producers notify wake_ for immediate dispatch. Retain one display
            // interval as a safety watchdog for V8/platform task sources that
            // do not yet expose a notification edge.
            auto idle_wait = std::chrono::milliseconds(16);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr) {
                idle_wait = runtime_->recommended_idle_wait(idle_wait);
            }
#endif
            std::unique_lock lock(wake_mutex_);
            const auto collect_runtime_work_metrics =
                runtime_work_metrics_enabled_.load(std::memory_order_relaxed);
            if (collect_runtime_work_metrics) {
                worker_waits_.fetch_add(1, std::memory_order_relaxed);
            }
            const auto signalled = wake_.wait_for(
                lock,
                idle_wait,
                [this, &token, &deferred_input] {
                // Every script-queue producer latches wake_pending_ after it
                // enqueues work. Do not inspect script_work_ while holding the
                // wake mutex: producers can signal while holding script_mutex_,
                // and taking the locks in the opposite order deadlocks shared
                // isolate startup.
                return wake_pending_
                    || token.stop_requested()
                    || deferred_input.has_value()
                    || !inputs_.empty()
                    || resize_pending_.load(std::memory_order_acquire)
                    || low_memory_requested_.load(std::memory_order_acquire)
                    || visibility_changed_.load(std::memory_order_acquire)
                    || preferred_color_scheme_changed_.load(
                        std::memory_order_acquire);
                });
            if (collect_runtime_work_metrics && signalled) {
                worker_signalled_wakes_.fetch_add(1, std::memory_order_relaxed);
            } else if (collect_runtime_work_metrics) {
                worker_timeout_wakes_.fetch_add(1, std::memory_order_relaxed);
            }
            wake_pending_ = false;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        inspector_runtime_.store(nullptr, std::memory_order_release);
#endif
        runtime_.reset();
#endif
    }
