    uint64_t begin_evaluate_v3(
        const webscene_interop_evaluate_request_v3* request,
        webscene_interop_completed_callback_v3 completed,
        void* user_data)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(request);
        static_cast<void>(completed);
        static_cast<void>(user_data);
        set_last_error("Native engine was built without V8 support");
        return 0U;
#else
        if (request == nullptr
            || request->struct_size < sizeof(webscene_interop_evaluate_request_v3)
            || request->version != 3U
            || request->source == nullptr
            || request->source_length == 0U
            || completed == nullptr) {
            set_last_error("Native interop invocation request is invalid");
            return 0U;
        }
        std::shared_ptr<interop_operation_v3> operation;
        {
            std::scoped_lock lock(script_mutex_, interop_mutex_);
            const auto pending_interop = std::count_if(
                script_work_.begin(),
                script_work_.end(),
                [](const auto& pending) {
                    return std::holds_alternative<interop_evaluate_work_v3>(pending)
                        || std::holds_alternative<interop_invoke_work_v3>(pending);
                });
            if (pending_interop >= 64U) {
                set_last_error("Native interop invocation queue is full");
                return 0U;
            }
            operation = rent_interop_operation_locked(
                completed,
                user_data);
            interop_operations_.emplace(operation->id, operation);
            script_work_.emplace_back(interop_evaluate_work_v3{
                std::string(request->source, request->source_length),
                request->document_name == nullptr
                    ? std::string("native-interop-v3.js")
                    : std::string(
                        request->document_name,
                        request->document_name_length),
                operation,
                enqueued_inputs_.load(std::memory_order_acquire)});
        }
        if (runtime_work_metrics_enabled_.load(std::memory_order_relaxed)) {
            arbitrary_evaluation_calls_.fetch_add(1, std::memory_order_relaxed);
            arbitrary_evaluation_source_bytes_.fetch_add(
                request->source_length,
                std::memory_order_relaxed);
        }
        signal_worker();
        return operation->id;
#endif
    }

    uint64_t begin_invoke_v3(
        const webscene_interop_invoke_request_v3* request,
        webscene_interop_completed_callback_v3 completed,
        void* user_data)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(request);
        static_cast<void>(completed);
        static_cast<void>(user_data);
        set_last_error("Native engine was built without V8 support");
        return 0U;
#else
        std::string validation_error;
        if (request == nullptr
            || request->struct_size < sizeof(webscene_interop_invoke_request_v3)
            || completed == nullptr
            || !validate_interop_invoke_request_v3(*request, validation_error)) {
            set_last_error(validation_error.empty()
                ? "Generated native interop invocation request is invalid"
                : std::move(validation_error));
            return 0U;
        }
        std::shared_ptr<interop_operation_v3> operation;
        auto copied = copy_interop_request(*request);
        {
            std::scoped_lock lock(script_mutex_, interop_mutex_);
            const auto pending_interop = std::count_if(
                script_work_.begin(),
                script_work_.end(),
                [](const auto& pending) {
                    return std::holds_alternative<interop_evaluate_work_v3>(pending)
                        || std::holds_alternative<interop_invoke_work_v3>(pending);
                });
            if (pending_interop >= 64U) {
                return_interop_request(std::move(copied));
                set_last_error("Native interop invocation queue is full");
                return 0U;
            }
            operation = rent_interop_operation_locked(
                completed,
                user_data);
            interop_operations_.emplace(operation->id, operation);
            script_work_.emplace_back(interop_invoke_work_v3{
                std::move(copied),
                operation,
                enqueued_inputs_.load(std::memory_order_acquire)});
        }
        if (runtime_work_metrics_enabled_.load(std::memory_order_relaxed)) {
            generated_invoke_calls_.fetch_add(1, std::memory_order_relaxed);
            generated_request_bytes_.fetch_add(
                request->global_name_length
                    + request->member_name_length
                    + request->value_count * sizeof(webscene_interop_value_v3)
                    + request->edge_count * sizeof(webscene_interop_edge_v3)
                    + request->utf8_byte_count,
                std::memory_order_relaxed);
        }
        signal_worker();
        return operation->id;
#endif
    }

    const webscene_interop_result_view_v3* take_invoke_result_v3(
        uint64_t operation_id)
    {
        std::lock_guard lock(interop_mutex_);
        const auto iterator = interop_operations_.find(operation_id);
        if (iterator == interop_operations_.end()
            || !iterator->second->ready.load(std::memory_order_acquire)
            || !iterator->second->result) {
            return nullptr;
        }
        auto operation = iterator->second;
        auto result = std::move(operation->result);
        interop_operations_.erase(iterator);
        return_interop_operation_locked(operation);
        const auto lease_id = allocate_interop_result_lease_id();
        result->view.lease_id = lease_id;
        const auto* view = &result->view;
        {
            std::lock_guard taken_lock(taken_interop_results_mutex);
            const auto [unused, inserted] =
                taken_interop_results.emplace(
                    view,
                    std::pair{lease_id, result.get()});
            if (!inserted) {
                auto pool = result->owner;
                pool->release(std::move(result));
                set_last_error(
                    "Native interop result lease address is already active");
                return nullptr;
            }
        }
        static_cast<void>(result.release());
        return view;
    }

    bool cancel_invoke_v3(uint64_t operation_id)
    {
        {
            std::scoped_lock lock(script_mutex_, interop_mutex_);
            const auto iterator = interop_operations_.find(operation_id);
            if (iterator == interop_operations_.end()) return false;
            auto operation = iterator->second;
            operation->cancelled.store(true, std::memory_order_release);
            interop_operations_.erase(iterator);
            script_work_.emplace_back(
                interop_cancel_work_v3{
                    operation_id,
                    std::move(operation)});
        }
        signal_worker();
        return true;
    }

    uint64_t enqueue_callback_v3(
        webscene_native::interop_callback_request_data_v3&& request)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(request);
        return 0U;
#else
        auto arguments = interop_result_pool_->acquire();
        arguments->data = std::move(request.arguments);
        const auto call_id = next_interop_callback_id_.fetch_add(
            1U,
            std::memory_order_relaxed);
        auto callback = interop_callback_pool_->acquire();
        callback->arguments = std::move(arguments);
        callback->publish(
            call_id,
            request.target_id,
            request.method_id,
            request.return_kind);

        bool notify = false;
        {
            std::lock_guard lock(interop_callback_mutex_);
            constexpr size_t maximum_queued_callbacks = 4096U;
            if (queued_interop_callbacks_.size()
                >= maximum_queued_callbacks) {
                auto pool = callback->owner;
                pool->release(std::move(callback));
                set_last_error("Native reverse interop callback queue is full");
                return 0U;
            }
            notify = queued_interop_callbacks_.empty();
            if (request.return_kind
                == WEBSCENE_INTEROP_CALLBACK_PROMISE_V3) {
                pending_interop_callback_promises_.insert(call_id);
            }
            queued_interop_callbacks_.push_back(std::move(callback));
            interop_callback_queue_high_water_ = std::max(
                interop_callback_queue_high_water_,
                queued_interop_callbacks_.size());
        }
        if (runtime_work_metrics_enabled_.load(std::memory_order_relaxed)) {
            generated_callback_calls_.fetch_add(1, std::memory_order_relaxed);
        }
        if (notify && interop_callback_available_callback_ != nullptr) {
            interop_callback_available_callback_(
                interop_callback_available_user_data_);
        }
        return call_id;
#endif
    }

    const webscene_interop_callback_view_v3* take_callback_v3()
    {
        std::unique_ptr<interop_callback_lease_v3> callback;
        {
            std::lock_guard lock(interop_callback_mutex_);
            if (queued_interop_callbacks_.empty()) return nullptr;
            callback = std::move(queued_interop_callbacks_.front());
            queued_interop_callbacks_.pop_front();
        }
        const auto lease_id = allocate_interop_callback_lease_id();
        callback->view.lease_id = lease_id;
        const auto* view = &callback->view;
        {
            std::lock_guard lock(taken_interop_callbacks_mutex);
            const auto [unused, inserted] =
                taken_interop_callbacks.emplace(
                    view,
                    std::pair{lease_id, callback.get()});
            if (!inserted) {
                auto pool = callback->owner;
                pool->release(std::move(callback));
                set_last_error(
                    "Native callback lease address is already active");
                return nullptr;
            }
        }
        static_cast<void>(callback.release());
        return view;
    }

    bool complete_callback_v3(
        const webscene_interop_callback_completion_v3* completion)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(completion);
        set_last_error("Native engine was built without V8 support");
        return false;
#else
        constexpr size_t maximum_values = 1'000'000U;
        constexpr size_t maximum_edges = 1'000'000U;
        constexpr size_t maximum_utf8_bytes = 64U * 1024U * 1024U;
        if (completion == nullptr
            || completion->struct_size
                < sizeof(webscene_interop_callback_completion_v3)
            || completion->version != 3U
            || completion->call_id == 0U
            || completion->succeeded > 1U
            || completion->value_count > maximum_values
            || completion->edge_count > maximum_edges
            || completion->utf8_byte_count > maximum_utf8_bytes
            || (completion->succeeded != 0U
                && (completion->value_count == 0U
                    || completion->values == nullptr
                    || completion->root_value_index
                        >= completion->value_count))
            || (completion->edge_count != 0U
                && completion->edges == nullptr)
            || (completion->utf8_byte_count != 0U
                && completion->utf8_bytes == nullptr)
            || (completion->succeeded == 0U
                && (completion->error_bytes == nullptr
                    || completion->error_byte_count == 0U))) {
            set_last_error("Native callback completion header is invalid");
            return false;
        }
        for (size_t index = 0; index < completion->value_count; ++index) {
            const auto& value = completion->values[index];
            if (value.kind > WEBSCENE_INTEROP_VALUE_HANDLE_V3
                || (value.kind == WEBSCENE_INTEROP_VALUE_STRING_V3
                    && (value.offset > completion->utf8_byte_count
                        || value.length
                            > completion->utf8_byte_count - value.offset))
                || ((value.kind == WEBSCENE_INTEROP_VALUE_ARRAY_V3
                        || value.kind == WEBSCENE_INTEROP_VALUE_OBJECT_V3)
                    && (value.offset > completion->edge_count
                        || value.length
                            > completion->edge_count - value.offset))) {
                set_last_error("Native callback completion arena is invalid");
                return false;
            }
        }
        for (size_t index = 0; index < completion->edge_count; ++index) {
            const auto& edge = completion->edges[index];
            if (edge.value_index >= completion->value_count
                || edge.name_offset > completion->utf8_byte_count
                || edge.name_length
                    > completion->utf8_byte_count - edge.name_offset) {
                set_last_error("Native callback completion edge is invalid");
                return false;
            }
        }

        auto copied = interop_callback_completion_pool_.acquire();
        copied->call_id = completion->call_id;
        copied->succeeded = completion->succeeded != 0U;
        copied->root_value_index = completion->root_value_index;
        if (completion->value_count != 0U) {
            copied->values.assign(
                completion->values,
                completion->values + completion->value_count);
        }
        if (completion->edge_count != 0U) {
            copied->edges.assign(
                completion->edges,
                completion->edges + completion->edge_count);
        }
        if (completion->utf8_byte_count != 0U) {
            copied->utf8_bytes.assign(
                completion->utf8_bytes,
                completion->utf8_bytes + completion->utf8_byte_count);
        }
        if (completion->error_byte_count != 0U) {
            copied->error.assign(
                completion->error_bytes,
                completion->error_byte_count);
        }
        {
            std::scoped_lock lock(
                script_mutex_,
                interop_callback_mutex_);
            if (!pending_interop_callback_promises_.contains(
                    completion->call_id)) {
                interop_callback_completion_pool_.release(
                    std::move(copied));
                set_last_error(
                    "Native callback completion references a stale call");
                return false;
            }
            constexpr size_t maximum_pending_completions = 4096U;
            const auto pending = static_cast<size_t>(std::count_if(
                script_work_.begin(),
                script_work_.end(),
                [](const auto& work) {
                    return std::holds_alternative<
                        interop_callback_completion_work_v3>(work);
                }));
            if (pending >= maximum_pending_completions) {
                interop_callback_completion_pool_.release(
                    std::move(copied));
                set_last_error("Native callback completion queue is full");
                return false;
            }
            pending_interop_callback_promises_.erase(
                completion->call_id);
            script_work_.emplace_back(
                interop_callback_completion_work_v3{
                    std::move(copied)});
        }
        signal_worker();
        return true;
#endif
    }

    bool cancel_callback_v3(uint64_t call_id)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(call_id);
        return false;
#else
        if (call_id == 0U) return false;
        {
            std::scoped_lock lock(
                script_mutex_,
                interop_callback_mutex_);
            if (pending_interop_callback_promises_.erase(call_id) == 0U) {
                return false;
            }
            script_work_.emplace_back(
                interop_callback_cancel_work_v3{call_id});
        }
        signal_worker();
        return true;
#endif
    }

    bool get_interop_pool_metrics_v3(
        webscene_interop_pool_metrics_v3& metrics) const
    {
        if (metrics.struct_size < sizeof(webscene_interop_pool_metrics_v3)) {
            return false;
        }
        interop_result_pool_->read_metrics(metrics);
        {
            std::lock_guard lock(interop_request_pool_mutex_);
            metrics.pooled_request_records =
                available_interop_requests_.size();
        }
        metrics.request_pool_hits =
            interop_request_pool_hits_.load(std::memory_order_relaxed);
        metrics.request_pool_misses =
            interop_request_pool_misses_.load(std::memory_order_relaxed);
        metrics.request_oversize_allocations =
            interop_request_oversize_allocations_.load(
                std::memory_order_relaxed);
        {
            std::lock_guard lock(interop_mutex_);
            metrics.active_operation_slots = interop_operations_.size();
            metrics.available_operation_slots =
                available_interop_operation_slots_.size();
            metrics.operation_slot_high_water =
                interop_operation_slot_high_water_;
            metrics.operation_result_leases = static_cast<uint64_t>(
                std::count_if(
                    interop_operation_slots_.begin(),
                    interop_operation_slots_.end(),
                    [](const auto& operation) {
                        return operation != nullptr
                            && operation->result != nullptr;
                    }));
        }
        {
            std::lock_guard lock(taken_interop_results_mutex);
            metrics.taken_result_leases =
                taken_interop_results.size();
        }
        {
            std::lock_guard lock(interop_callback_mutex_);
            metrics.queued_callbacks = queued_interop_callbacks_.size();
            metrics.pending_callback_promises =
                pending_interop_callback_promises_.size();
            metrics.callback_queue_high_water =
                interop_callback_queue_high_water_;
        }
        {
            std::lock_guard lock(taken_interop_callbacks_mutex);
            metrics.taken_callback_leases =
                taken_interop_callbacks.size();
        }
        return true;
    }
