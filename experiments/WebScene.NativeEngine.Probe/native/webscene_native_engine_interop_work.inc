    webscene_native::interop_invoke_request_data_v3 copy_interop_request(
        const webscene_interop_invoke_request_v3& request)
    {
        webscene_native::interop_invoke_request_data_v3 copied;
        {
            std::lock_guard lock(interop_request_pool_mutex_);
            if (!available_interop_requests_.empty()) {
                copied = std::move(available_interop_requests_.back());
                available_interop_requests_.pop_back();
                ++interop_request_pool_hits_;
            } else {
                ++interop_request_pool_misses_;
            }
        }
        copied.operation = request.operation;
        copied.flags = request.flags;
        copied.result_mode = request.result_mode;
        copied.target_handle = request.target_handle;
        copied.arguments_root = request.arguments_root;
        copied.global_name.assign(
            request.global_name == nullptr ? "" : request.global_name,
            request.global_name == nullptr
                ? 0U
                : request.global_name_length);
        copied.member_name.assign(
            request.member_name == nullptr ? "" : request.member_name,
            request.member_name == nullptr
                ? 0U
                : request.member_name_length);
        copied.values.assign(
            request.values,
            request.values + request.value_count);
        if (request.edge_count == 0U) {
            copied.edges.clear();
        } else {
            copied.edges.assign(
                request.edges,
                request.edges + request.edge_count);
        }
        if (request.utf8_byte_count == 0U) {
            copied.utf8_bytes.clear();
        } else {
            copied.utf8_bytes.assign(
                request.utf8_bytes,
                request.utf8_bytes + request.utf8_byte_count);
        }
        return copied;
    }

    void return_interop_request(
        webscene_native::interop_invoke_request_data_v3&& request)
    {
        constexpr size_t maximum_pooled_request_bytes =
            8U * 1024U * 1024U;
        const auto retained =
            request.global_name.capacity()
            + request.member_name.capacity()
            + request.values.capacity()
                * sizeof(webscene_interop_value_v3)
            + request.edges.capacity()
                * sizeof(webscene_interop_edge_v3)
            + request.utf8_bytes.capacity();
        if (retained > maximum_pooled_request_bytes) {
            ++interop_request_oversize_allocations_;
            return;
        }
        request.global_name.clear();
        request.member_name.clear();
        request.values.clear();
        request.edges.clear();
        request.utf8_bytes.clear();
        std::lock_guard lock(interop_request_pool_mutex_);
        if (available_interop_requests_.size() >= 64U) return;
        available_interop_requests_.push_back(std::move(request));
    }

    std::shared_ptr<interop_operation_v3> rent_interop_operation_locked(
        webscene_interop_completed_callback_v3 completed,
        void* user_data)
    {
        std::shared_ptr<interop_operation_v3> operation;
        if (!available_interop_operation_slots_.empty()) {
            const auto index = available_interop_operation_slots_.back();
            available_interop_operation_slots_.pop_back();
            operation = interop_operation_slots_[index];
        } else {
            operation = std::make_shared<interop_operation_v3>();
            operation->pool_index = interop_operation_slots_.size();
            interop_operation_slots_.push_back(operation);
            interop_operation_slot_high_water_ = std::max(
                interop_operation_slot_high_water_,
                interop_operation_slots_.size());
        }
        operation->id = next_interop_operation_id_.fetch_add(
            1U,
            std::memory_order_relaxed);
        operation->completed = completed;
        operation->user_data = user_data;
        operation->cancelled.store(false, std::memory_order_relaxed);
        operation->ready.store(false, std::memory_order_relaxed);
        release_interop_operation_result(*operation);
        operation->in_use = true;
        return operation;
    }

    static void release_interop_operation_result(
        interop_operation_v3& operation)
    {
        if (!operation.result) return;
        auto pool = operation.result->owner;
        pool->release(std::move(operation.result));
    }

    void return_interop_operation_locked(
        const std::shared_ptr<interop_operation_v3>& operation)
    {
        if (!operation || !operation->in_use) return;
        operation->id = 0U;
        operation->completed = nullptr;
        operation->user_data = nullptr;
        operation->cancelled.store(false, std::memory_order_relaxed);
        operation->ready.store(false, std::memory_order_relaxed);
        release_interop_operation_result(*operation);
        operation->in_use = false;
        available_interop_operation_slots_.push_back(
            operation->pool_index);
    }

    void complete_interop_operation(
        const std::shared_ptr<interop_operation_v3>& operation,
        webscene_native::interop_result_data_v3&& data)
    {
        auto result = interop_result_pool_->acquire();
        result->data = std::move(data);
        result->publish(operation->id);

        webscene_interop_completed_callback_v3 completed = nullptr;
        void* completed_user_data = nullptr;
        {
            std::lock_guard lock(interop_mutex_);
            const auto pending = interop_operations_.find(operation->id);
            if (pending != interop_operations_.end()
                && pending->second == operation
                && !operation->cancelled.load(std::memory_order_acquire)) {
                operation->result = std::move(result);
                operation->ready.store(true, std::memory_order_release);
                completed = operation->completed;
                completed_user_data = operation->user_data;
            }
        }
        if (result) {
            auto pool = result->owner;
            pool->release(std::move(result));
        }
        if (completed != nullptr) {
            completed(completed_user_data, operation->id);
        }
    }

    void update_host_animation_frame_demand() noexcept
    {
        auto demand = document_.has_active_animations()
            ? uint8_t{2U}
            : uint8_t{0U};
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        if (runtime_ != nullptr) {
            demand |= runtime_->host_animation_frame_demand();
        }
#endif
        const auto previous = host_animation_frame_requested_.exchange(
            demand,
            std::memory_order_release);
        const auto pointer_frame_pending =
            frame_paced_pointer_pending_.load(std::memory_order_acquire);
        if (previous == 0U
            && demand != 0U
            && !pointer_frame_pending
            && animation_frame_requested_callback_ != nullptr) {
            animation_frame_requested_callback_(
                animation_frame_requested_user_data_);
        }
    }

    void signal_worker() noexcept
    {
        {
            // This is an auto-reset event, not an unlatched condition-variable
            // edge. Mutating the predicate under the wait mutex closes the
            // producer's check-to-sleep race while still coalescing any number
            // of signals into one immediate worker wake.
            std::lock_guard lock(wake_mutex_);
            wake_pending_ = true;
        }
        wake_.notify_one();
    }

    bool take_latest_resize(
        webscene_input_event& result,
        uint64_t& consumed_count,
        webscene_input_event& frame,
        uint64_t& frame_consumed_count,
        bool& has_frame,
        std::chrono::steady_clock::time_point& frame_enqueued_at)
    {
        std::lock_guard lock(resize_mutex_);
        if (pending_resize_count_ == 0) {
            resize_pending_.store(false, std::memory_order_release);
            return false;
        }
        result = pending_resize_;
        consumed_count = pending_resize_count_;
        frame = pending_resize_frame_;
        frame_consumed_count = pending_resize_frame_count_;
        has_frame = pending_resize_has_frame_;
        frame_enqueued_at = pending_resize_frame_enqueued_at_;
        pending_resize_count_ = 0;
        pending_resize_frame_count_ = 0;
        pending_resize_has_frame_ = false;
        resize_pending_.store(false, std::memory_order_release);
        return true;
    }
