class interop_result_pool_v3;
struct interop_result_lease_v3;
class interop_callback_pool_v3;
struct interop_callback_lease_v3;

std::mutex taken_interop_results_mutex;
std::unordered_map<
    const webscene_interop_result_view_v3*,
    std::pair<uint64_t, interop_result_lease_v3*>>
    taken_interop_results;
std::atomic<uint64_t> next_interop_result_lease_id{1U};
std::mutex taken_interop_callbacks_mutex;
std::unordered_map<
    const webscene_interop_callback_view_v3*,
    std::pair<uint64_t, interop_callback_lease_v3*>>
    taken_interop_callbacks;
std::atomic<uint64_t> next_interop_callback_lease_id{1U};

uint64_t allocate_interop_result_lease_id()
{
    auto lease_id = next_interop_result_lease_id.fetch_add(
        1U,
        std::memory_order_relaxed);
    if (lease_id == 0U) {
        lease_id = next_interop_result_lease_id.fetch_add(
            1U,
            std::memory_order_relaxed);
    }
    return lease_id;
}

uint64_t allocate_interop_callback_lease_id()
{
    auto lease_id = next_interop_callback_lease_id.fetch_add(
        1U,
        std::memory_order_relaxed);
    if (lease_id == 0U) {
        lease_id = next_interop_callback_lease_id.fetch_add(
            1U,
            std::memory_order_relaxed);
    }
    return lease_id;
}

struct interop_result_lease_v3 final {
    webscene_interop_result_view_v3 view{};
    webscene_native::interop_result_data_v3 data;
    std::shared_ptr<interop_result_pool_v3> owner;

    size_t retained_bytes() const noexcept
    {
        return sizeof(*this)
            + data.values.capacity() * sizeof(webscene_interop_value_v3)
            + data.edges.capacity() * sizeof(webscene_interop_edge_v3)
            + data.utf8_bytes.capacity()
            + data.error.capacity();
    }

    void publish(uint64_t operation_id)
    {
        const auto retained = retained_bytes();
        view = webscene_interop_result_view_v3{
            static_cast<uint32_t>(sizeof(webscene_interop_result_view_v3)),
            3U,
            data.status,
            0U,
            operation_id,
            data.values.data(),
            data.edges.data(),
            data.utf8_bytes.data(),
            data.error.data(),
            0U,
            static_cast<uint32_t>(data.values.size()),
            static_cast<uint32_t>(data.edges.size()),
            static_cast<uint32_t>(data.utf8_bytes.size()),
            static_cast<uint32_t>(data.error.size()),
            data.root_value_index,
            retained > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(retained),
            0U,
            0U};
    }
};

class interop_result_pool_v3 final
    : public std::enable_shared_from_this<interop_result_pool_v3> {
public:
    static constexpr size_t maximum_pooled_bytes = 8U * 1024U * 1024U;
    static constexpr size_t maximum_pooled_result_bytes = 1024U * 1024U;
    static constexpr std::array<size_t, 5> size_classes{
        4U * 1024U,
        16U * 1024U,
        64U * 1024U,
        256U * 1024U,
        1024U * 1024U};

    std::unique_ptr<interop_result_lease_v3> acquire()
    {
        std::unique_ptr<interop_result_lease_v3> result;
        {
            std::lock_guard lock(mutex_);
            for (size_t index = 0;
                 index < available_by_size_class_.size();
                 ++index) {
                auto& available = available_by_size_class_[index];
                if (available.empty()) continue;
                result = std::move(available.back());
                available.pop_back();
                const auto retained = result->retained_bytes();
                pooled_bytes_ = retained > pooled_bytes_
                    ? 0U
                    : pooled_bytes_ - retained;
                pooled_bytes_by_size_class_[index] =
                    retained > pooled_bytes_by_size_class_[index]
                        ? 0U
                        : pooled_bytes_by_size_class_[index] - retained;
                pool_hits_.fetch_add(1U, std::memory_order_relaxed);
                break;
            }
        }
        if (!result) {
            result = std::make_unique<interop_result_lease_v3>();
            pool_misses_.fetch_add(1U, std::memory_order_relaxed);
        }
        result->data.clear();
        result->owner = shared_from_this();
        const auto outstanding = outstanding_results_.fetch_add(
            1U,
            std::memory_order_relaxed) + 1U;
        auto high_water = high_water_outstanding_results_.load(
            std::memory_order_relaxed);
        while (outstanding > high_water
            && !high_water_outstanding_results_.compare_exchange_weak(
                high_water,
                outstanding,
                std::memory_order_relaxed)) {
        }
        return result;
    }

    void release(std::unique_ptr<interop_result_lease_v3> result)
    {
        outstanding_results_.fetch_sub(1U, std::memory_order_relaxed);
        const auto retained = result->retained_bytes();
        result->owner.reset();
        if (retained > maximum_pooled_result_bytes) {
            oversize_allocations_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const auto size_class = static_cast<size_t>(std::distance(
            size_classes.begin(),
            std::lower_bound(
                size_classes.begin(),
                size_classes.end(),
                retained)));
        if (size_class >= size_classes.size()) {
            oversize_allocations_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        std::lock_guard lock(mutex_);
        if (retained > maximum_pooled_bytes - pooled_bytes_) return;
        result->data.clear();
        pooled_bytes_ += retained;
        pooled_bytes_by_size_class_[size_class] += retained;
        available_by_size_class_[size_class].push_back(std::move(result));
    }

    void read_metrics(webscene_interop_pool_metrics_v3& metrics) const
    {
        metrics.version = 3U;
        metrics.outstanding_results =
            outstanding_results_.load(std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex_);
            metrics.pooled_bytes = pooled_bytes_;
            metrics.pooled_result_bytes_4k =
                pooled_bytes_by_size_class_[0];
            metrics.pooled_result_bytes_16k =
                pooled_bytes_by_size_class_[1];
            metrics.pooled_result_bytes_64k =
                pooled_bytes_by_size_class_[2];
            metrics.pooled_result_bytes_256k =
                pooled_bytes_by_size_class_[3];
            metrics.pooled_result_bytes_1m =
                pooled_bytes_by_size_class_[4];
        }
        metrics.pool_hits = pool_hits_.load(std::memory_order_relaxed);
        metrics.pool_misses = pool_misses_.load(std::memory_order_relaxed);
        metrics.oversize_allocations =
            oversize_allocations_.load(std::memory_order_relaxed);
        metrics.high_water_outstanding_results =
            high_water_outstanding_results_.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex mutex_;
    std::array<
        std::vector<std::unique_ptr<interop_result_lease_v3>>,
        size_classes.size()> available_by_size_class_;
    std::array<size_t, size_classes.size()>
        pooled_bytes_by_size_class_{};
    size_t pooled_bytes_{0};
    std::atomic<uint64_t> outstanding_results_{0};
    std::atomic<uint64_t> pool_hits_{0};
    std::atomic<uint64_t> pool_misses_{0};
    std::atomic<uint64_t> oversize_allocations_{0};
    std::atomic<uint64_t> high_water_outstanding_results_{0};
};

struct interop_callback_lease_v3 final {
    webscene_interop_callback_view_v3 view{};
    std::unique_ptr<interop_result_lease_v3> arguments;
    std::shared_ptr<interop_callback_pool_v3> owner;

    ~interop_callback_lease_v3()
    {
        if (!arguments) return;
        auto result_pool = arguments->owner;
        result_pool->release(std::move(arguments));
    }

    void publish(
        uint64_t call_id,
        uint64_t target_id,
        uint32_t method_id,
        uint32_t return_kind)
    {
        const auto retained = arguments == nullptr
            ? sizeof(*this)
            : sizeof(*this) + arguments->retained_bytes();
        const auto& data = arguments->data;
        view = webscene_interop_callback_view_v3{
            static_cast<uint32_t>(sizeof(webscene_interop_callback_view_v3)),
            3U,
            call_id,
            target_id,
            method_id,
            return_kind,
            data.values.data(),
            data.edges.data(),
            data.utf8_bytes.data(),
            0U,
            static_cast<uint32_t>(data.values.size()),
            static_cast<uint32_t>(data.edges.size()),
            static_cast<uint32_t>(data.utf8_bytes.size()),
            data.root_value_index,
            retained > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(retained),
            0U};
    }
};

class interop_callback_pool_v3 final
    : public std::enable_shared_from_this<interop_callback_pool_v3> {
public:
    std::unique_ptr<interop_callback_lease_v3> acquire()
    {
        std::unique_ptr<interop_callback_lease_v3> result;
        {
            std::lock_guard lock(mutex_);
            if (!available_.empty()) {
                result = std::move(available_.back());
                available_.pop_back();
            }
        }
        if (!result) {
            result = std::make_unique<interop_callback_lease_v3>();
        }
        result->view = {};
        result->arguments.reset();
        result->owner = shared_from_this();
        return result;
    }

    void release(std::unique_ptr<interop_callback_lease_v3> callback)
    {
        if (callback->arguments) {
            auto result_pool = callback->arguments->owner;
            result_pool->release(std::move(callback->arguments));
        }
        callback->view = {};
        callback->owner.reset();
        std::lock_guard lock(mutex_);
        if (available_.size() < 256U) {
            available_.push_back(std::move(callback));
        }
    }

private:
    std::mutex mutex_;
    std::vector<std::unique_ptr<interop_callback_lease_v3>> available_;
};

struct interop_operation_v3 final {
    size_t pool_index{0};
    uint64_t id{0};
    webscene_interop_completed_callback_v3 completed{nullptr};
    void* user_data{nullptr};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> ready{false};
    std::unique_ptr<interop_result_lease_v3> result;
    bool in_use{false};

    ~interop_operation_v3()
    {
        if (!result) return;
        auto pool = result->owner;
        pool->release(std::move(result));
    }
};

struct interop_evaluate_work_v3 final {
    std::string source;
    std::string document_name;
    std::shared_ptr<interop_operation_v3> operation;
    uint64_t required_consumed_inputs{0};
};

struct interop_invoke_work_v3 final {
    webscene_native::interop_invoke_request_data_v3 request;
    std::shared_ptr<interop_operation_v3> operation;
    uint64_t required_consumed_inputs{0};
};

struct interop_cancel_work_v3 final {
    uint64_t operation_id{0};
    std::shared_ptr<interop_operation_v3> operation;
};

struct interop_callback_completion_work_v3 final {
    std::unique_ptr<
        webscene_native::interop_callback_completion_data_v3> completion;
};

struct interop_callback_cancel_work_v3 final {
    uint64_t call_id{0};
};

class interop_callback_completion_pool_v3 final {
public:
    std::unique_ptr<
        webscene_native::interop_callback_completion_data_v3> acquire()
    {
        std::unique_ptr<
            webscene_native::interop_callback_completion_data_v3> result;
        {
            std::lock_guard lock(mutex_);
            if (!available_.empty()) {
                result = std::move(available_.back());
                available_.pop_back();
                pooled_bytes_ -= retained_bytes(*result);
            }
        }
        if (!result) {
            result = std::make_unique<
                webscene_native::interop_callback_completion_data_v3>();
        }
        reset(*result);
        return result;
    }

    void release(
        std::unique_ptr<
            webscene_native::interop_callback_completion_data_v3> completion)
    {
        if (!completion) return;
        reset(*completion);
        const auto bytes = retained_bytes(*completion);
        constexpr size_t maximum_record_capacity = 1U * 1024U * 1024U;
        constexpr size_t maximum_pool_capacity = 8U * 1024U * 1024U;
        if (bytes > maximum_record_capacity) return;
        std::lock_guard lock(mutex_);
        if (available_.size() < 256U
            && bytes <= maximum_pool_capacity - pooled_bytes_) {
            pooled_bytes_ += bytes;
            available_.push_back(std::move(completion));
        }
    }

private:
    static void reset(
        webscene_native::interop_callback_completion_data_v3& completion)
    {
        completion.call_id = 0U;
        completion.succeeded = false;
        completion.root_value_index = 0U;
        completion.values.clear();
        completion.edges.clear();
        completion.utf8_bytes.clear();
        completion.error.clear();
    }

    static size_t retained_bytes(
        const webscene_native::interop_callback_completion_data_v3& completion)
    {
        return completion.values.capacity()
                * sizeof(webscene_interop_value_v3)
            + completion.edges.capacity()
                * sizeof(webscene_interop_edge_v3)
            + completion.utf8_bytes.capacity()
            + completion.error.capacity();
    }

    std::mutex mutex_;
    std::vector<std::unique_ptr<
        webscene_native::interop_callback_completion_data_v3>> available_;
    size_t pooled_bytes_{0};
};

using script_work_request =
    std::variant<
        script_request,
        url_request,
        interop_evaluate_work_v3,
        interop_invoke_work_v3,
        interop_cancel_work_v3,
        interop_callback_completion_work_v3,
        interop_callback_cancel_work_v3>;

bool validate_interop_invoke_request_v3(
    const webscene_interop_invoke_request_v3& request,
    std::string& error)
{
    constexpr size_t maximum_values = 1'000'000U;
    constexpr size_t maximum_edges = 1'000'000U;
    constexpr size_t maximum_utf8_bytes = 64U * 1024U * 1024U;
    constexpr size_t maximum_name_bytes = 1024U * 1024U;
    if (request.struct_size < sizeof(webscene_interop_invoke_request_v3)
        || request.version != 3U
        || request.operation < WEBSCENE_INTEROP_GET_GLOBAL_V3
        || request.operation > WEBSCENE_INTEROP_INVOKE_FUNCTION_V3
        || request.result_mode > WEBSCENE_INTEROP_RESULT_VOID_V3
        || (request.flags & ~WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3) != 0U
        || request.value_count == 0U
        || request.value_count > maximum_values
        || request.edge_count > maximum_edges
        || request.utf8_byte_count > maximum_utf8_bytes
        || request.global_name_length > maximum_name_bytes
        || request.member_name_length > maximum_name_bytes
        || request.values == nullptr
        || (request.edge_count != 0U && request.edges == nullptr)
        || (request.utf8_byte_count != 0U && request.utf8_bytes == nullptr)
        || (request.global_name_length != 0U
            && request.global_name == nullptr)
        || (request.member_name_length != 0U
            && request.member_name == nullptr)
        || request.arguments_root >= request.value_count
        || request.values[request.arguments_root].kind
            != WEBSCENE_INTEROP_VALUE_ARRAY_V3) {
        error = "Generated native interop request header is invalid";
        return false;
    }
    const auto needs_global =
        request.operation == WEBSCENE_INTEROP_GET_GLOBAL_V3
        || request.operation == WEBSCENE_INTEROP_INVOKE_GLOBAL_V3
        || request.operation == WEBSCENE_INTEROP_CONSTRUCT_V3;
    const auto needs_target =
        request.operation == WEBSCENE_INTEROP_GET_PROPERTY_V3
        || request.operation == WEBSCENE_INTEROP_SET_PROPERTY_V3
        || request.operation == WEBSCENE_INTEROP_INVOKE_MEMBER_V3
        || request.operation == WEBSCENE_INTEROP_RELEASE_HANDLE_V3
        || request.operation == WEBSCENE_INTEROP_CREATE_CALLBACK_TARGET_V3
        || request.operation == WEBSCENE_INTEROP_CREATE_CALLBACK_FUNCTION_V3
        || request.operation == WEBSCENE_INTEROP_CREATE_SYNCHRONOUS_FACTORY_V3
        || request.operation == WEBSCENE_INTEROP_INVOKE_FUNCTION_V3;
    const auto needs_member =
        request.operation == WEBSCENE_INTEROP_GET_PROPERTY_V3
        || request.operation == WEBSCENE_INTEROP_SET_PROPERTY_V3
        || request.operation == WEBSCENE_INTEROP_INVOKE_MEMBER_V3;
    if ((needs_global
            && (request.global_name == nullptr
                || request.global_name_length == 0U))
        || (needs_target && request.target_handle == 0U)
        || (needs_member
            && (request.member_name == nullptr
                || request.member_name_length == 0U))) {
        error = "Generated native interop call-site metadata is incomplete";
        return false;
    }
    for (size_t index = 0; index < request.value_count; ++index) {
        const auto& value = request.values[index];
        if (value.kind > WEBSCENE_INTEROP_VALUE_HANDLE_V3) {
            error = "Generated native interop request has an invalid value tag";
            return false;
        }
        if (value.kind == WEBSCENE_INTEROP_VALUE_STRING_V3
            && (value.offset > request.utf8_byte_count
                || value.length > request.utf8_byte_count - value.offset)) {
            error = "Generated native interop request has an invalid string range";
            return false;
        }
        if ((value.kind == WEBSCENE_INTEROP_VALUE_ARRAY_V3
                || value.kind == WEBSCENE_INTEROP_VALUE_OBJECT_V3)
            && (value.offset > request.edge_count
                || value.length > request.edge_count - value.offset)) {
            error = "Generated native interop request has an invalid edge range";
            return false;
        }
    }
    for (size_t index = 0; index < request.edge_count; ++index) {
        const auto& edge = request.edges[index];
        if (edge.value_index >= request.value_count
            || edge.name_offset > request.utf8_byte_count
            || edge.name_length
                > request.utf8_byte_count - edge.name_offset) {
            error = "Generated native interop request has an invalid edge";
            return false;
        }
    }
    return true;
}
