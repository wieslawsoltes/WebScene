#include "webscene_native_engine.h"
#include "webscene_native_dom.h"
#include "webscene_v8_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr uint32_t input_capacity = 8192;
constexpr uint32_t minimum_command_count = 64;
constexpr uint32_t scene_flag_checkpoint = 1U;
constexpr uint32_t scene_flag_dom_replacement = 2U;
constexpr uint32_t scene_flag_component_ready = 4U;
constexpr uint32_t canvas_layer_flag_replace = 1U;
constexpr uint32_t canvas_layer_flag_remove = 2U;

static_assert(sizeof(webscene_input_event) == 48);
static_assert(sizeof(webscene_scene_command) == 52);
static_assert(sizeof(webscene_scene_header) == 56);
static_assert(sizeof(webscene_canvas_layout) == 32);
static_assert(sizeof(webscene_canvas_layer) == 64);
static_assert(sizeof(webscene_canvas_command) == 80);
static_assert(sizeof(webscene_scene_string) == 8);
static_assert(sizeof(webscene_damage_rect) == 16);
static_assert(sizeof(webscene_scene_view) == 136);
static_assert(sizeof(webscene_interop_value_v1) == 24);
static_assert(sizeof(webscene_interop_edge_v1) == 16);
static_assert(sizeof(webscene_interop_request_v1) == 48);
static_assert(sizeof(webscene_interop_request_v2) == 112);
static_assert(sizeof(webscene_interop_result_view_v1) == 96);
static_assert(sizeof(webscene_interop_pool_metrics_v1) == 152);

class input_ring final {
public:
    bool try_push(const webscene_input_event& value)
    {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == read_.load(std::memory_order_acquire)) {
            return false;
        }

        values_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(webscene_input_event& value)
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) {
            return false;
        }

        value = values_[read];
        read_.store(increment(read), std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return read_.load(std::memory_order_acquire)
            == write_.load(std::memory_order_acquire);
    }

private:
    static constexpr uint32_t increment(uint32_t value)
    {
        return (value + 1U) % input_capacity;
    }

    std::array<webscene_input_event, input_capacity> values_{};
    alignas(64) std::atomic<uint32_t> write_{0};
    alignas(64) std::atomic<uint32_t> read_{0};
};

struct canvas_layer_version final {
    uint64_t generation{0};
    uint32_t command_count{0};
    uint32_t string_count{0};
    float x{0};
    float y{0};
    float width{0};
    float height{0};

    bool operator==(const canvas_layer_version&) const = default;
};

struct scene final {
    webscene_scene_header header{};
    std::vector<webscene_scene_command> commands;
    std::vector<webscene_canvas_layer> canvas_layers;
    std::vector<webscene_canvas_command> canvas_commands;
    std::vector<webscene_scene_string> canvas_strings;
    std::vector<char> canvas_string_bytes;
    std::vector<webscene_damage_rect> damage_rects;
    std::unordered_map<uint32_t, canvas_layer_version> full_layer_versions;
    uint64_t dom_hash{0};
    uint64_t published_timestamp_nanoseconds{0};
};

uint64_t retained_scene_bytes(const scene& value)
{
    return sizeof(scene)
        + value.commands.capacity() * sizeof(webscene_scene_command)
        + value.canvas_layers.capacity() * sizeof(webscene_canvas_layer)
        + value.canvas_commands.capacity() * sizeof(webscene_canvas_command)
        + value.canvas_strings.capacity() * sizeof(webscene_scene_string)
        + value.canvas_string_bytes.capacity() * sizeof(char)
        + value.damage_rects.capacity() * sizeof(webscene_damage_rect)
        + value.full_layer_versions.size()
            * (sizeof(uint32_t) + sizeof(canvas_layer_version) + sizeof(void*) * 2U);
}

struct acknowledgement_state final {
    std::mutex mutex;
    uint64_t revision{0};
    uint64_t dom_hash{0};
    float viewport_width{0};
    float viewport_height{0};
    std::unordered_map<uint32_t, canvas_layer_version> layer_versions;
    std::shared_ptr<const scene> value;
    std::deque<std::shared_ptr<const scene>> pending_scenes;
    std::atomic<uint64_t> acknowledged_scenes{0};
    std::atomic<uint64_t> total_acknowledgement_nanoseconds{0};
    std::atomic<uint64_t> last_acknowledgement_nanoseconds{0};
    std::atomic<uint64_t> maximum_acknowledgement_nanoseconds{0};
};

struct script_request final {
    std::string source;
    std::string document_name;
};

struct url_request final {
    std::string url;
};

struct evaluation_completion final {
    std::mutex mutex;
    std::condition_variable ready;
    std::string result;
    std::string error;
    bool completed{false};
    bool succeeded{false};
};

struct evaluation_request final {
    std::string source;
    std::string document_name;
    std::shared_ptr<evaluation_completion> completion;
    uint64_t required_consumed_inputs{0};
};

class interop_result_pool_v1;

std::mutex taken_interop_results_mutex;
std::unordered_set<const webscene_interop_result_view_v1*>
    taken_interop_results;

struct interop_result_lease_v1 final {
    webscene_interop_result_view_v1 view{};
    webscene_native::interop_result_data_v1 data;
    std::shared_ptr<interop_result_pool_v1> owner;

    size_t retained_bytes() const noexcept
    {
        return sizeof(*this)
            + data.values.capacity() * sizeof(webscene_interop_value_v1)
            + data.edges.capacity() * sizeof(webscene_interop_edge_v1)
            + data.utf8_bytes.capacity()
            + data.error.capacity();
    }

    void publish(uint64_t operation_id)
    {
        const auto retained = retained_bytes();
        view = webscene_interop_result_view_v1{
            static_cast<uint32_t>(sizeof(webscene_interop_result_view_v1)),
            1U,
            data.status,
            0U,
            operation_id,
            data.values.data(),
            data.edges.data(),
            data.utf8_bytes.data(),
            data.error.data(),
            this,
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

class interop_result_pool_v1 final
    : public std::enable_shared_from_this<interop_result_pool_v1> {
public:
    static constexpr size_t maximum_pooled_bytes = 8U * 1024U * 1024U;
    static constexpr size_t maximum_pooled_result_bytes = 1024U * 1024U;
    static constexpr std::array<size_t, 5> size_classes{
        4U * 1024U,
        16U * 1024U,
        64U * 1024U,
        256U * 1024U,
        1024U * 1024U};

    std::unique_ptr<interop_result_lease_v1> acquire()
    {
        std::unique_ptr<interop_result_lease_v1> result;
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
            result = std::make_unique<interop_result_lease_v1>();
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

    void release(std::unique_ptr<interop_result_lease_v1> result)
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

    void read_metrics(webscene_interop_pool_metrics_v1& metrics) const
    {
        metrics.version = 1U;
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
        std::vector<std::unique_ptr<interop_result_lease_v1>>,
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

struct interop_operation_v1 final {
    size_t pool_index{0};
    uint64_t id{0};
    webscene_interop_completed_callback_v1 completed{nullptr};
    void* user_data{nullptr};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> ready{false};
    std::unique_ptr<interop_result_lease_v1> result;
    bool in_use{false};

    ~interop_operation_v1()
    {
        if (!result) return;
        auto pool = result->owner;
        pool->release(std::move(result));
    }
};

struct interop_request_v1 final {
    std::string source;
    std::string document_name;
    std::shared_ptr<interop_operation_v1> operation;
    uint64_t required_consumed_inputs{0};
};

struct interop_request_v2 final {
    webscene_native::interop_request_data_v2 request;
    std::shared_ptr<interop_operation_v1> operation;
    uint64_t required_consumed_inputs{0};
};

struct interop_cancel_request_v2 final {
    uint64_t operation_id{0};
    std::shared_ptr<interop_operation_v1> operation;
};

using script_work_request =
    std::variant<
        script_request,
        url_request,
        evaluation_request,
        interop_request_v1,
        interop_request_v2,
        interop_cancel_request_v2>;

bool validate_interop_request_v2(
    const webscene_interop_request_v2& request,
    std::string& error)
{
    constexpr size_t maximum_values = 1'000'000U;
    constexpr size_t maximum_edges = 1'000'000U;
    constexpr size_t maximum_utf8_bytes = 64U * 1024U * 1024U;
    if (request.version != 2U
        || request.operation < WEBSCENE_INTEROP_GET_GLOBAL_V2
        || request.operation > WEBSCENE_INTEROP_RELEASE_HANDLE_V2
        || request.result_mode > WEBSCENE_INTEROP_RESULT_VOID_V2
        || (request.flags & ~WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V2) != 0U
        || request.value_count == 0U
        || request.value_count > maximum_values
        || request.edge_count > maximum_edges
        || request.utf8_byte_count > maximum_utf8_bytes
        || request.values == nullptr
        || (request.edge_count != 0U && request.edges == nullptr)
        || (request.utf8_byte_count != 0U && request.utf8_bytes == nullptr)
        || request.arguments_root >= request.value_count
        || request.values[request.arguments_root].kind
            != WEBSCENE_INTEROP_VALUE_ARRAY_V1) {
        error = "Generated native interop request header is invalid";
        return false;
    }
    const auto needs_global =
        request.operation == WEBSCENE_INTEROP_GET_GLOBAL_V2
        || request.operation == WEBSCENE_INTEROP_INVOKE_GLOBAL_V2
        || request.operation == WEBSCENE_INTEROP_CONSTRUCT_V2;
    const auto needs_target =
        request.operation == WEBSCENE_INTEROP_GET_PROPERTY_V2
        || request.operation == WEBSCENE_INTEROP_SET_PROPERTY_V2
        || request.operation == WEBSCENE_INTEROP_INVOKE_MEMBER_V2
        || request.operation == WEBSCENE_INTEROP_RELEASE_HANDLE_V2;
    const auto needs_member =
        request.operation == WEBSCENE_INTEROP_GET_PROPERTY_V2
        || request.operation == WEBSCENE_INTEROP_SET_PROPERTY_V2
        || request.operation == WEBSCENE_INTEROP_INVOKE_MEMBER_V2;
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
        if (value.kind > WEBSCENE_INTEROP_VALUE_HANDLE_V1) {
            error = "Generated native interop request has an invalid value tag";
            return false;
        }
        if (value.kind == WEBSCENE_INTEROP_VALUE_STRING_V1
            && (value.offset > request.utf8_byte_count
                || value.length > request.utf8_byte_count - value.offset)) {
            error = "Generated native interop request has an invalid string range";
            return false;
        }
        if ((value.kind == WEBSCENE_INTEROP_VALUE_ARRAY_V1
                || value.kind == WEBSCENE_INTEROP_VALUE_OBJECT_V1)
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

uint64_t mix_hash(uint64_t hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

void store_maximum(std::atomic<uint64_t>& target, uint64_t value)
{
    auto current = target.load(std::memory_order_relaxed);
    while (current < value
        && !target.compare_exchange_weak(
            current,
            value,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}

bool command_uses_dom_string(const webscene_scene_command& command)
{
    return command.kind >= 3U && command.kind <= 6U;
}

std::string_view command_dom_string(
    const scene& owner,
    const webscene_scene_command& command)
{
    if (!command_uses_dom_string(command)
        || command.flags >= owner.canvas_strings.size()) {
        return {};
    }
    const auto& value = owner.canvas_strings[command.flags];
    if (value.byte_offset > owner.canvas_string_bytes.size()
        || value.byte_length > owner.canvas_string_bytes.size() - value.byte_offset) {
        return {};
    }
    return std::string_view(
        owner.canvas_string_bytes.data() + value.byte_offset,
        value.byte_length);
}

bool same_dom_command_visual(
    const scene& previous_owner,
    const webscene_scene_command& previous,
    const scene& next_owner,
    const webscene_scene_command& next)
{
    if (previous.kind != next.kind
        || previous.node_id != next.node_id
        || previous.x != next.x
        || previous.y != next.y
        || previous.width != next.width
        || previous.height != next.height
        || previous.rgba != next.rgba
        || previous.radius_top_left != next.radius_top_left
        || previous.radius_top_right != next.radius_top_right
        || previous.radius_bottom_right != next.radius_bottom_right
        || previous.radius_bottom_left != next.radius_bottom_left
        || previous.stroke_width != next.stroke_width) {
        return false;
    }
    if (command_uses_dom_string(next)) {
        return command_dom_string(previous_owner, previous)
            == command_dom_string(next_owner, next);
    }
    return previous.flags == next.flags;
}

webscene_damage_rect command_damage_bounds(const webscene_scene_command& command)
{
    auto left = command.x;
    auto top = command.y;
    auto right = command.x + command.width;
    auto bottom = command.y + command.height;
    if (command.kind == 2U) {
        left = std::min(command.x, command.width);
        top = std::min(command.y, command.height);
        right = std::max(command.x, command.width);
        bottom = std::max(command.y, command.height);
    } else if (command.kind >= 4U && command.kind <= 6U
        && std::abs(command.stroke_width) > 0.001F) {
        // A rotated SVG can extend beyond its untransformed rectangular box.
        // Use the circumscribed square so both the old and new pixels are
        // invalidated throughout the transition.
        const auto center_x = command.x + command.width * 0.5F;
        const auto center_y = command.y + command.height * 0.5F;
        const auto radius = std::sqrt(
            command.width * command.width + command.height * command.height) * 0.5F;
        left = center_x - radius;
        top = center_y - radius;
        right = center_x + radius;
        bottom = center_y + radius;
    }
    constexpr auto antialias_padding = 2.0F;
    return {
        left - antialias_padding,
        top - antialias_padding,
        std::max(0.0F, right - left + antialias_padding * 2.0F),
        std::max(0.0F, bottom - top + antialias_padding * 2.0F)};
}

bool append_localized_dom_damage(
    const scene& previous,
    const scene& next,
    float viewport_width,
    float viewport_height,
    std::vector<webscene_damage_rect>& damage)
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto trace = std::getenv("WEBSCENE_PROBE_PROFILE_ANIMATION") != nullptr;
#endif
    if (previous.commands.size() != next.commands.size()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) std::fprintf(
            stderr,
            "[Native Engine Probe] DOM damage fallback: command-count %zu -> %zu\n",
            previous.commands.size(),
            next.commands.size());
#endif
        return false;
    }

    auto changed = false;
    auto left = viewport_width;
    auto top = viewport_height;
    auto right = 0.0F;
    auto bottom = 0.0F;
    const auto include = [&](const webscene_scene_command& command) {
        const auto bounds = command_damage_bounds(command);
        left = std::min(left, bounds.x);
        top = std::min(top, bounds.y);
        right = std::max(right, bounds.x + bounds.width);
        bottom = std::max(bottom, bounds.y + bounds.height);
    };
    for (size_t index = 0; index < next.commands.size(); ++index) {
        const auto& old_command = previous.commands[index];
        const auto& new_command = next.commands[index];
        if (old_command.kind != new_command.kind
            || old_command.node_id != new_command.node_id) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            if (trace) std::fprintf(
                stderr,
                "[Native Engine Probe] DOM damage fallback: command identity at %zu "
                "kind %u/%u node %u/%u\n",
                index,
                old_command.kind,
                new_command.kind,
                old_command.node_id,
                new_command.node_id);
#endif
            return false;
        }
        if (same_dom_command_visual(previous, old_command, next, new_command)) continue;
        changed = true;
        include(old_command);
        include(new_command);
    }
    if (!changed) return false;

    left = std::clamp(left, 0.0F, viewport_width);
    top = std::clamp(top, 0.0F, viewport_height);
    right = std::clamp(right, left, viewport_width);
    bottom = std::clamp(bottom, top, viewport_height);
    const auto width = right - left;
    const auto height = bottom - top;
    const auto viewport_area = std::max(1.0F, viewport_width * viewport_height);
    if (width <= 0 || height <= 0 || width * height > viewport_area * 0.4F) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) std::fprintf(
            stderr,
            "[Native Engine Probe] DOM damage fallback: changed bounds %.1f,%.1f %.1fx%.1f "
            "(%.1f%%)\n",
            left,
            top,
            width,
            height,
            width * height * 100.0F / viewport_area);
#endif
        return false;
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    if (trace) std::fprintf(
        stderr,
        "[Native Engine Probe] localized DOM damage: %.1f,%.1f %.1fx%.1f\n",
        left,
        top,
        width,
        height);
#endif
    damage.push_back({left, top, width, height});
    return true;
}

} // namespace

struct webscene_engine final {
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
        void* host_request_available_user_data = nullptr)
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

    size_t evaluate_json(
        const char* source,
        size_t source_length,
        const char* document_name,
        size_t document_name_length,
        char* destination,
        size_t destination_capacity,
        uint32_t timeout_milliseconds)
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        static_cast<void>(source);
        static_cast<void>(source_length);
        static_cast<void>(document_name);
        static_cast<void>(document_name_length);
        static_cast<void>(destination);
        static_cast<void>(destination_capacity);
        static_cast<void>(timeout_milliseconds);
        set_last_error("Native engine was built without V8 support");
        return 0;
#else
        if (source == nullptr || source_length == 0) {
            set_last_error("Evaluation source is empty");
            return 0;
        }
        auto completion = std::make_shared<evaluation_completion>();
        {
            std::lock_guard lock(script_mutex_);
            const auto pending_evaluations = std::count_if(
                script_work_.begin(),
                script_work_.end(),
                [](const auto& request) {
                    return std::holds_alternative<evaluation_request>(request);
                });
            if (pending_evaluations >= 64) {
                set_last_error("Native evaluation queue is full");
                return 0;
            }
            script_work_.emplace_back(evaluation_request{
                std::string(source, source_length),
                document_name == nullptr
                    ? std::string("managed-chart-api.js")
                    : std::string(document_name, document_name_length),
                completion,
                enqueued_inputs_.load(std::memory_order_acquire)});
        }
        signal_worker();
        std::unique_lock lock(completion->mutex);
        const auto timeout = std::chrono::milliseconds(
            std::clamp(timeout_milliseconds, 1U, 60'000U));
        if (!completion->ready.wait_for(lock, timeout, [&completion] {
                return completion->completed;
            })) {
            set_last_error("Native evaluation timed out");
            return 0;
        }
        if (!completion->succeeded) {
            set_last_error(completion->error);
            return 0;
        }
        const auto required = completion->result.size() + 1U;
        if (destination != nullptr && destination_capacity > 0) {
            const auto copy_count = std::min(completion->result.size(), destination_capacity - 1U);
            std::copy_n(completion->result.data(), copy_count, destination);
            destination[copy_count] = '\0';
        }
        return required;
#endif
    }

    uint64_t begin_invoke_v1(
        const webscene_interop_request_v1* request,
        webscene_interop_completed_callback_v1 completed,
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
            || request->struct_size < sizeof(webscene_interop_request_v1)
            || request->version != 1U
            || request->source == nullptr
            || request->source_length == 0U
            || completed == nullptr) {
            set_last_error("Native interop invocation request is invalid");
            return 0U;
        }
        std::shared_ptr<interop_operation_v1> operation;
        {
            std::scoped_lock lock(script_mutex_, interop_mutex_);
            const auto pending_interop = std::count_if(
                script_work_.begin(),
                script_work_.end(),
                [](const auto& pending) {
                    return std::holds_alternative<interop_request_v1>(pending)
                        || std::holds_alternative<interop_request_v2>(pending);
                });
            if (pending_interop >= 64U) {
                set_last_error("Native interop invocation queue is full");
                return 0U;
            }
            operation = rent_interop_operation_locked(
                completed,
                user_data);
            interop_operations_.emplace(operation->id, operation);
            script_work_.emplace_back(interop_request_v1{
                std::string(request->source, request->source_length),
                request->document_name == nullptr
                    ? std::string("native-interop-v1.js")
                    : std::string(
                        request->document_name,
                        request->document_name_length),
                operation,
                enqueued_inputs_.load(std::memory_order_acquire)});
        }
        signal_worker();
        return operation->id;
#endif
    }

    uint64_t begin_generated_invoke_v2(
        const webscene_interop_request_v2* request,
        webscene_interop_completed_callback_v1 completed,
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
            || request->struct_size < sizeof(webscene_interop_request_v2)
            || completed == nullptr
            || !validate_interop_request_v2(*request, validation_error)) {
            set_last_error(validation_error.empty()
                ? "Generated native interop invocation request is invalid"
                : std::move(validation_error));
            return 0U;
        }
        std::shared_ptr<interop_operation_v1> operation;
        auto copied = copy_interop_request(*request);
        {
            std::scoped_lock lock(script_mutex_, interop_mutex_);
            const auto pending_interop = std::count_if(
                script_work_.begin(),
                script_work_.end(),
                [](const auto& pending) {
                    return std::holds_alternative<interop_request_v1>(pending)
                        || std::holds_alternative<interop_request_v2>(pending);
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
            script_work_.emplace_back(interop_request_v2{
                std::move(copied),
                operation,
                enqueued_inputs_.load(std::memory_order_acquire)});
        }
        signal_worker();
        return operation->id;
#endif
    }

    const webscene_interop_result_view_v1* take_invoke_result_v1(
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
        const auto* view = &result.release()->view;
        {
            std::lock_guard taken_lock(taken_interop_results_mutex);
            taken_interop_results.insert(view);
        }
        return view;
    }

    bool cancel_invoke_v1(uint64_t operation_id)
    {
        {
            std::scoped_lock lock(script_mutex_, interop_mutex_);
            const auto iterator = interop_operations_.find(operation_id);
            if (iterator == interop_operations_.end()) return false;
            auto operation = iterator->second;
            operation->cancelled.store(true, std::memory_order_release);
            interop_operations_.erase(iterator);
            script_work_.emplace_back(
                interop_cancel_request_v2{
                    operation_id,
                    std::move(operation)});
        }
        signal_worker();
        return true;
    }

    bool get_interop_pool_metrics_v1(
        webscene_interop_pool_metrics_v1& metrics) const
    {
        if (metrics.struct_size < sizeof(webscene_interop_pool_metrics_v1)) {
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
        }
        return true;
    }

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

    std::shared_ptr<acknowledgement_state> acknowledgement_state_handle() const
    {
        return acknowledgement_;
    }

    void read_metrics(webscene_engine_metrics& result) const
    {
        result.enqueued_inputs = enqueued_inputs_.load(std::memory_order_relaxed);
        result.dropped_inputs = dropped_inputs_.load(std::memory_order_relaxed);
        result.consumed_inputs = consumed_inputs_.load(std::memory_order_relaxed);
        result.published_scenes = published_scenes_.load(std::memory_order_relaxed);
        result.acquired_scenes = acquired_scenes_.load(std::memory_order_relaxed);
        result.executed_scripts = executed_scripts_.load(std::memory_order_relaxed);
        result.script_errors = script_errors_.load(std::memory_order_relaxed);
        result.dom_nodes = dom_nodes_.load(std::memory_order_relaxed);
        result.layout_passes = layout_passes_.load(std::memory_order_relaxed);
        result.iframe_nodes = iframe_nodes_.load(std::memory_order_relaxed);
        result.iframe_html_bytes = iframe_html_bytes_.load(std::memory_order_relaxed);
        result.frame_scripts_executed = frame_scripts_executed_.load(std::memory_order_relaxed);
        result.frame_script_errors = frame_script_errors_.load(std::memory_order_relaxed);
        result.canvas_nodes = canvas_nodes_.load(std::memory_order_relaxed);
        result.component_ready = component_ready_.load(std::memory_order_relaxed);
        result.compilation_requests = compilation_requests_.load(std::memory_order_relaxed);
        result.compilation_memory_hits = compilation_memory_hits_.load(std::memory_order_relaxed);
        result.compilation_persistent_hits = compilation_persistent_hits_.load(std::memory_order_relaxed);
        result.compilation_persistent_misses = compilation_persistent_misses_.load(std::memory_order_relaxed);
        result.compilation_cache_rejections = compilation_cache_rejections_.load(std::memory_order_relaxed);
        result.compilation_cache_bytes_read = compilation_cache_bytes_read_.load(std::memory_order_relaxed);
        result.compilation_cache_bytes_written = compilation_cache_bytes_written_.load(std::memory_order_relaxed);
        result.compilation_time_nanoseconds = compilation_time_nanoseconds_.load(std::memory_order_relaxed);
        result.input_events_dispatched = input_events_dispatched_.load(std::memory_order_relaxed);
        result.input_callbacks_invoked = input_callbacks_invoked_.load(std::memory_order_relaxed);
        result.busiest_canvas_width_milli = busiest_canvas_width_milli_.load(std::memory_order_relaxed);
        result.busiest_canvas_height_milli = busiest_canvas_height_milli_.load(std::memory_order_relaxed);
        result.coalesced_resize_inputs = coalesced_resize_inputs_.load(std::memory_order_relaxed);
        result.applied_resize_inputs = applied_resize_inputs_.load(std::memory_order_relaxed);
        result.last_resize_dispatch_nanoseconds =
            last_resize_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.last_scene_publication_nanoseconds =
            last_scene_publication_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_outer_listeners_nanoseconds =
            last_resize_outer_listeners_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_frame_listeners_nanoseconds =
            last_resize_frame_listeners_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_layout_nanoseconds =
            last_resize_layout_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_observers_nanoseconds =
            last_resize_observers_nanoseconds_.load(std::memory_order_relaxed);
        result.coalesced_pointer_move_inputs =
            coalesced_pointer_move_inputs_.load(std::memory_order_relaxed);
        result.coalesced_wheel_inputs =
            coalesced_wheel_inputs_.load(std::memory_order_relaxed);
        result.applied_pointer_move_inputs =
            applied_pointer_move_inputs_.load(std::memory_order_relaxed);
        result.applied_wheel_inputs =
            applied_wheel_inputs_.load(std::memory_order_relaxed);
        result.applied_animation_frames =
            applied_animation_frames_.load(std::memory_order_relaxed);
        result.coalesced_animation_frames =
            coalesced_animation_frames_.load(std::memory_order_relaxed);
        result.last_animation_advance_nanoseconds =
            last_animation_advance_nanoseconds_.load(std::memory_order_relaxed);
        result.last_layout_nanoseconds =
            last_layout_nanoseconds_.load(std::memory_order_relaxed);
        result.last_scene_build_nanoseconds =
            last_scene_build_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_scene_publication_nanoseconds =
            maximum_scene_publication_nanoseconds_.load(std::memory_order_relaxed);
    }

    void read_input_dispatch_metrics(webscene_input_dispatch_metrics& result) const
    {
        result.last_dispatch_nanoseconds =
            last_input_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_dispatch_nanoseconds =
            maximum_input_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.last_dispatch_sequence =
            last_input_dispatch_sequence_.load(std::memory_order_relaxed);
        if (result.struct_size >= sizeof(webscene_input_dispatch_metrics)) {
            result.dispatched_inputs =
                dispatched_inputs_.load(std::memory_order_relaxed);
            result.total_dispatch_nanoseconds =
                total_input_dispatch_nanoseconds_.load(
                    std::memory_order_relaxed);
        }
    }

    void read_animation_frame_metrics(
        webscene_animation_frame_metrics& result) const
    {
        result.dispatched_frames =
            dispatched_animation_frames_.load(std::memory_order_relaxed);
        result.total_dispatch_nanoseconds =
            total_animation_frame_dispatch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_dispatch_nanoseconds =
            last_animation_frame_dispatch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.maximum_dispatch_nanoseconds =
            maximum_animation_frame_dispatch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_timestamp_microseconds =
            last_animation_frame_timestamp_microseconds_.load(
                std::memory_order_relaxed);
    }

    void read_scene_flow_metrics(webscene_scene_flow_metrics& result) const
    {
        result.publication_attempts =
            scene_publication_attempts_.load(std::memory_order_relaxed);
        result.blocked_publications =
            blocked_scene_publications_.load(std::memory_order_relaxed);
        result.acknowledged_scenes =
            acknowledgement_->acknowledged_scenes.load(
                std::memory_order_relaxed);
        result.total_acknowledgement_nanoseconds =
            acknowledgement_->total_acknowledgement_nanoseconds.load(
                std::memory_order_relaxed);
        result.last_acknowledgement_nanoseconds =
            acknowledgement_->last_acknowledgement_nanoseconds.load(
                std::memory_order_relaxed);
        result.maximum_acknowledgement_nanoseconds =
            acknowledgement_->maximum_acknowledgement_nanoseconds.load(
                std::memory_order_relaxed);
        std::lock_guard lock(acknowledgement_->mutex);
        result.acknowledged_revision = acknowledgement_->revision;
    }

    void read_resize_frame_metrics(webscene_resize_frame_metrics& result) const
    {
        result.submitted_pairs =
            submitted_resize_frame_pairs_.load(std::memory_order_relaxed);
        result.applied_pairs =
            applied_resize_frame_pairs_.load(std::memory_order_relaxed);
        result.published_pairs =
            published_resize_frame_pairs_.load(std::memory_order_relaxed);
        result.total_queue_nanoseconds =
            total_resize_frame_queue_nanoseconds_.load(std::memory_order_relaxed);
        result.last_queue_nanoseconds =
            last_resize_frame_queue_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_queue_nanoseconds =
            maximum_resize_frame_queue_nanoseconds_.load(std::memory_order_relaxed);
        result.total_dispatch_nanoseconds =
            total_resize_frame_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.last_dispatch_nanoseconds =
            last_resize_frame_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_dispatch_nanoseconds =
            maximum_resize_frame_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.animation_frame_callbacks =
            resize_frame_animation_callbacks_.load(std::memory_order_relaxed);
        result.total_animation_frame_batch_nanoseconds =
            total_resize_frame_animation_batch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_animation_frame_batch_nanoseconds =
            last_resize_frame_animation_batch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.maximum_animation_frame_batch_nanoseconds =
            maximum_resize_frame_animation_batch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.total_to_publication_nanoseconds =
            total_resize_frame_to_publication_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_to_publication_nanoseconds =
            last_resize_frame_to_publication_nanoseconds_.load(
                std::memory_order_relaxed);
        result.maximum_to_publication_nanoseconds =
            maximum_resize_frame_to_publication_nanoseconds_.load(
                std::memory_order_relaxed);
    }

    void read_resource_cache_metrics(webscene_resource_cache_metrics& result) const
    {
        result.requests = resource_cache_requests_.load(std::memory_order_relaxed);
        result.hits = resource_cache_hits_.load(std::memory_order_relaxed);
        result.misses = resource_cache_misses_.load(std::memory_order_relaxed);
        result.rejections = resource_cache_rejections_.load(std::memory_order_relaxed);
        result.bytes_read = resource_cache_bytes_read_.load(std::memory_order_relaxed);
        result.bytes_written = resource_cache_bytes_written_.load(std::memory_order_relaxed);
    }

    void read_process_cache_metrics(webscene_process_cache_metrics& result) const
    {
        result.compilation_memory_hits =
            process_compilation_memory_hits_.load(std::memory_order_relaxed);
        result.compilation_leaders =
            process_compilation_leaders_.load(std::memory_order_relaxed);
        result.compilation_waiters =
            process_compilation_waiters_.load(std::memory_order_relaxed);
        result.compilation_shared_bytes =
            process_compilation_shared_bytes_.load(std::memory_order_relaxed);
        result.resource_memory_hits =
            process_resource_memory_hits_.load(std::memory_order_relaxed);
        result.resource_load_leaders =
            process_resource_load_leaders_.load(std::memory_order_relaxed);
        result.resource_load_waiters =
            process_resource_load_waiters_.load(std::memory_order_relaxed);
        result.resource_shared_bytes =
            process_resource_shared_bytes_.load(std::memory_order_relaxed);
        if (result.struct_size
            >= offsetof(
                webscene_process_cache_metrics,
                shared_isolate_slot)) {
            result.script_source_memory_hits =
                process_script_source_memory_hits_.load(std::memory_order_relaxed);
            result.script_source_shared_bytes =
                process_script_source_shared_bytes_.load(std::memory_order_relaxed);
        }
        if (result.struct_size >= sizeof(webscene_process_cache_metrics)) {
            result.shared_isolate_slot =
                shared_isolate_slot_.load(std::memory_order_relaxed);
            result.shared_isolate_active_contexts =
                shared_isolate_active_contexts_.load(std::memory_order_relaxed);
            result.shared_isolate_peak_contexts =
                shared_isolate_peak_contexts_.load(std::memory_order_relaxed);
        }
    }

    void read_memory_metrics(webscene_engine_memory_metrics& result) const
    {
        result.v8_total_heap_bytes =
            v8_total_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_used_heap_bytes =
            v8_used_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_executable_heap_bytes =
            v8_executable_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_physical_heap_bytes =
            v8_physical_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_external_bytes =
            v8_external_bytes_.load(std::memory_order_relaxed);
        result.v8_malloced_bytes =
            v8_malloced_bytes_.load(std::memory_order_relaxed);
        result.v8_peak_malloced_bytes =
            v8_peak_malloced_bytes_.load(std::memory_order_relaxed);
        result.latest_scene_bytes =
            latest_scene_bytes_.load(std::memory_order_relaxed);
        result.process_compilation_cache_bytes =
            process_compilation_cache_bytes_.load(std::memory_order_relaxed);
        result.process_resource_cache_bytes =
            process_resource_cache_bytes_.load(std::memory_order_relaxed);
        constexpr auto attributed_memory_metrics_size =
            offsetof(webscene_engine_memory_metrics, low_memory_notifications);
        if (result.struct_size >= attributed_memory_metrics_size) {
            result.v8_code_and_metadata_bytes =
                v8_code_and_metadata_bytes_.load(std::memory_order_relaxed);
            result.v8_bytecode_and_metadata_bytes =
                v8_bytecode_and_metadata_bytes_.load(std::memory_order_relaxed);
            result.v8_external_script_source_bytes =
                v8_external_script_source_bytes_.load(std::memory_order_relaxed);
            result.native_dom_node_count =
                native_dom_node_count_.load(std::memory_order_relaxed);
            result.native_dom_node_size_bytes =
                native_dom_node_size_bytes_.load(std::memory_order_relaxed);
            result.native_dom_inline_bytes =
                native_dom_inline_bytes_.load(std::memory_order_relaxed);
            result.native_dom_pseudo_storage_bytes =
                native_dom_pseudo_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_canvas_node_count =
                native_dom_canvas_node_count_.load(std::memory_order_relaxed);
            result.native_dom_canvas_storage_bytes =
                native_dom_canvas_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_animation_count =
                native_dom_animation_count_.load(std::memory_order_relaxed);
            result.native_dom_animation_storage_bytes =
                native_dom_animation_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_custom_property_node_count =
                native_dom_custom_property_node_count_.load(std::memory_order_relaxed);
            result.native_dom_custom_property_entry_count =
                native_dom_custom_property_entry_count_.load(std::memory_order_relaxed);
            result.native_dom_custom_property_storage_bytes =
                native_dom_custom_property_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_background_image_count =
                native_dom_background_image_count_.load(std::memory_order_relaxed);
            result.native_dom_background_image_storage_bytes =
                native_dom_background_image_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_grid_count =
                native_dom_grid_count_.load(std::memory_order_relaxed);
            result.native_dom_grid_storage_bytes =
                native_dom_grid_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_authored_style_node_count =
                native_dom_authored_style_node_count_.load(std::memory_order_relaxed);
            result.native_dom_authored_style_entry_count =
                native_dom_authored_style_entry_count_.load(std::memory_order_relaxed);
            result.native_dom_authored_style_storage_bytes =
                native_dom_authored_style_storage_bytes_.load(std::memory_order_relaxed);
            result.native_css_rule_count =
                native_css_rule_count_.load(std::memory_order_relaxed);
            result.native_css_rule_storage_bytes =
                native_css_rule_storage_bytes_.load(std::memory_order_relaxed);
            result.native_css_index_storage_bytes =
                native_css_index_storage_bytes_.load(std::memory_order_relaxed);
            result.process_shared_css_rule_count =
                process_shared_css_rule_count_.load(std::memory_order_relaxed);
            result.process_shared_css_rule_storage_bytes =
                process_shared_css_rule_storage_bytes_.load(std::memory_order_relaxed);
        }
        constexpr auto low_memory_metrics_size =
            offsetof(webscene_engine_memory_metrics, native_dom_attribute_node_count);
        if (result.struct_size >= low_memory_metrics_size) {
            result.low_memory_notifications =
                low_memory_notifications_.load(std::memory_order_relaxed);
        }
        constexpr auto attribute_metrics_size =
            offsetof(webscene_engine_memory_metrics, native_wrapper_handle_count);
        if (result.struct_size >= attribute_metrics_size) {
            result.native_dom_attribute_node_count =
                native_dom_attribute_node_count_.load(std::memory_order_relaxed);
            result.native_dom_attribute_entry_count =
                native_dom_attribute_entry_count_.load(std::memory_order_relaxed);
            result.native_dom_attribute_storage_bytes =
                native_dom_attribute_storage_bytes_.load(std::memory_order_relaxed);
        }
        constexpr auto native_registry_metrics_size =
            offsetof(webscene_engine_memory_metrics, v8_young_space_used_bytes);
        if (result.struct_size >= native_registry_metrics_size) {
            result.native_wrapper_handle_count =
                native_wrapper_handle_count_.load(std::memory_order_relaxed);
            result.native_wrapper_storage_bytes =
                native_wrapper_storage_bytes_.load(std::memory_order_relaxed);
            result.native_text_measurement_cache_entry_count =
                native_text_measurement_cache_entry_count_.load(std::memory_order_relaxed);
            result.native_text_measurement_cache_storage_bytes =
                native_text_measurement_cache_storage_bytes_.load(std::memory_order_relaxed);
            result.process_compilation_mapped_cache_bytes =
                process_compilation_mapped_cache_bytes_.load(std::memory_order_relaxed);
            result.process_resource_mapped_cache_bytes =
                process_resource_mapped_cache_bytes_.load(std::memory_order_relaxed);
            result.native_dom_textual_style_count =
                native_dom_textual_style_count_.load(std::memory_order_relaxed);
            result.native_dom_textual_style_storage_bytes =
                native_dom_textual_style_storage_bytes_.load(
                    std::memory_order_relaxed);
            result.native_dom_node_pool_reserved_bytes =
                native_dom_node_pool_reserved_bytes_.load(
                    std::memory_order_relaxed);
            result.native_dom_node_pool_peak_bytes =
                native_dom_node_pool_peak_bytes_.load(
                    std::memory_order_relaxed);
            result.native_dom_table_layout_count =
                native_dom_table_layout_count_.load(std::memory_order_relaxed);
            result.native_dom_table_layout_storage_bytes =
                native_dom_table_layout_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_form_control_count =
                native_dom_form_control_count_.load(std::memory_order_relaxed);
            result.native_dom_form_control_storage_bytes =
                native_dom_form_control_storage_bytes_.load(std::memory_order_relaxed);
            result.hidden_low_memory_notifications =
                hidden_low_memory_notifications_.load(std::memory_order_relaxed);
            result.native_event_listener_count =
                native_event_listener_count_.load(std::memory_order_relaxed);
            result.native_event_listener_storage_bytes =
                native_event_listener_storage_bytes_.load(std::memory_order_relaxed);
        }
        constexpr auto v8_space_metrics_size =
            offsetof(webscene_engine_memory_metrics, pending_scene_count);
        if (result.struct_size >= v8_space_metrics_size) {
            result.v8_young_space_used_bytes =
                v8_young_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_young_space_physical_bytes =
                v8_young_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_old_space_used_bytes =
                v8_old_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_old_space_physical_bytes =
                v8_old_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_code_space_used_bytes =
                v8_code_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_code_space_physical_bytes =
                v8_code_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_map_space_used_bytes =
                v8_map_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_map_space_physical_bytes =
                v8_map_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_large_object_space_used_bytes =
                v8_large_object_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_large_object_space_physical_bytes =
                v8_large_object_space_physical_bytes_.load(
                    std::memory_order_relaxed);
            result.v8_read_only_space_used_bytes =
                v8_read_only_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_read_only_space_physical_bytes =
                v8_read_only_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_shared_space_used_bytes =
                v8_shared_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_shared_space_physical_bytes =
                v8_shared_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_trusted_space_used_bytes =
                v8_trusted_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_trusted_space_physical_bytes =
                v8_trusted_space_physical_bytes_.load(std::memory_order_relaxed);
        }
        if (result.struct_size >= sizeof(webscene_engine_memory_metrics)) {
            std::lock_guard lock(acknowledgement_->mutex);
            result.pending_scene_count =
                acknowledgement_->pending_scenes.size();
            result.pending_scene_bytes = 0;
            for (const auto& pending : acknowledgement_->pending_scenes) {
                result.pending_scene_bytes += retained_scene_bytes(*pending);
            }
        }
    }

private:
    webscene_native::interop_request_data_v2 copy_interop_request(
        const webscene_interop_request_v2& request)
    {
        webscene_native::interop_request_data_v2 copied;
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
        webscene_native::interop_request_data_v2&& request)
    {
        constexpr size_t maximum_pooled_request_bytes =
            8U * 1024U * 1024U;
        const auto retained =
            request.global_name.capacity()
            + request.member_name.capacity()
            + request.values.capacity()
                * sizeof(webscene_interop_value_v1)
            + request.edges.capacity()
                * sizeof(webscene_interop_edge_v1)
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

    std::shared_ptr<interop_operation_v1> rent_interop_operation_locked(
        webscene_interop_completed_callback_v1 completed,
        void* user_data)
    {
        std::shared_ptr<interop_operation_v1> operation;
        if (!available_interop_operation_slots_.empty()) {
            const auto index = available_interop_operation_slots_.back();
            available_interop_operation_slots_.pop_back();
            operation = interop_operation_slots_[index];
        } else {
            operation = std::make_shared<interop_operation_v1>();
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
        operation->result.reset();
        operation->in_use = true;
        return operation;
    }

    void return_interop_operation_locked(
        const std::shared_ptr<interop_operation_v1>& operation)
    {
        if (!operation || !operation->in_use) return;
        operation->id = 0U;
        operation->completed = nullptr;
        operation->user_data = nullptr;
        operation->cancelled.store(false, std::memory_order_relaxed);
        operation->ready.store(false, std::memory_order_relaxed);
        operation->result.reset();
        operation->in_use = false;
        available_interop_operation_slots_.push_back(
            operation->pool_index);
    }

    void complete_interop_operation(
        const std::shared_ptr<interop_operation_v1>& operation,
        webscene_native::interop_result_data_v1&& data)
    {
        auto result = interop_result_pool_->acquire();
        result->data = std::move(data);
        result->publish(operation->id);

        webscene_interop_completed_callback_v1 completed = nullptr;
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
        host_animation_frame_requested_.store(
            demand,
            std::memory_order_release);
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
            });
        if (!runtime_->initialize()) {
            set_last_error(runtime_->last_error());
            runtime_.reset();
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
            bool resize_applied = false;
            bool host_frame_applied = false;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (visibility_changed_.exchange(false, std::memory_order_acq_rel)) {
                if (host_visible_.load(std::memory_order_acquire)) {
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
                changed = true;
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
                if (auto* url = std::get_if<url_request>(&request)) {
                    if (runtime_ != nullptr && runtime_->load_url(url->url)) {
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
                        std::get_if<interop_cancel_request_v2>(&request)) {
                    if (runtime_ != nullptr) {
                        runtime_->cancel_interop_v2(
                            cancellation->operation_id);
                    }
                    {
                        std::lock_guard lock(interop_mutex_);
                        return_interop_operation_locked(
                            cancellation->operation);
                    }
                    continue;
                }
                if (auto* interop = std::get_if<interop_request_v2>(&request)) {
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

                    webscene_native::interop_result_data_v1 result;
                    auto state =
                        webscene_native::interop_invoke_state_v2::failed;
                    if (runtime_ == nullptr) {
                        result.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V1;
                        result.error = "V8 runtime is unavailable";
                    } else {
                        const std::weak_ptr<interop_operation_v1> operation =
                            interop->operation;
                        state = runtime_->invoke_interop_v2(
                            interop->request,
                            result,
                            interop->operation->id,
                            [this, operation](
                                    webscene_native::interop_result_data_v1&&
                                        completed_result) {
                                const auto retained = operation.lock();
                                if (!retained) return;
                                complete_interop_operation(
                                    retained,
                                    std::move(completed_result));
                            });
                    }
                    if (state
                        == webscene_native::interop_invoke_state_v2::pending) {
                        return_interop_request(
                            std::move(interop->request));
                        update_compilation_metrics();
                        changed = true;
                        continue;
                    }
                    if (state
                            == webscene_native::interop_invoke_state_v2::failed
                        && result.error.empty()) {
                        result.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V1;
                        result.error = runtime_ == nullptr
                            ? "V8 runtime is unavailable"
                            : runtime_->last_error();
                    }
                    if (state
                        == webscene_native::interop_invoke_state_v2::completed) {
                        update_component_readiness();
                    }
                    complete_interop_operation(
                        interop->operation,
                        std::move(result));
                    return_interop_request(
                        std::move(interop->request));
                    update_compilation_metrics();
                    changed = true;
                    continue;
                }
                if (auto* interop = std::get_if<interop_request_v1>(&request)) {
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
                        && runtime_->evaluate_interop_v1(
                            interop->source,
                            interop->document_name,
                            result->data);
                    if (runtime_ == nullptr) {
                        result->data.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V1;
                        result->data.error = "V8 runtime is unavailable";
                    } else if (!succeeded && result->data.error.empty()) {
                        result->data.status =
                            WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V1;
                        result->data.error = runtime_->last_error();
                    }
                    if (succeeded) update_component_readiness();
                    result->publish(interop->operation->id);

                    webscene_interop_completed_callback_v1 completed = nullptr;
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
                    changed = true;
                    continue;
                }
                auto& evaluation = std::get<evaluation_request>(request);
                if (consumed_inputs_.load(std::memory_order_acquire)
                    < evaluation.required_consumed_inputs) {
                    // A producer can enqueue input after this worker iteration
                    // has drained the input queues but before it swaps script
                    // work. Preserve API order without blocking the worker:
                    // prepend this evaluation and all later work so the next
                    // iteration consumes the input first.
                    std::lock_guard lock(script_mutex_);
                    script_work_.insert(
                        script_work_.begin(),
                        std::make_move_iterator(iterator),
                        std::make_move_iterator(script_work.end()));
                    break;
                }
                std::string result;
                const auto succeeded = runtime_ != nullptr
                    && runtime_->evaluate_json(
                        evaluation.source,
                        evaluation.document_name,
                        result);
                if (succeeded) update_component_readiness();
                {
                    std::lock_guard lock(evaluation.completion->mutex);
                    evaluation.completion->result = std::move(result);
                    evaluation.completion->error = succeeded
                        ? std::string{}
                        : runtime_ == nullptr
                            ? "V8 runtime is unavailable"
                            : runtime_->last_error();
                    evaluation.completion->succeeded = succeeded;
                    evaluation.completion->completed = true;
                }
                evaluation.completion->ready.notify_all();
                update_compilation_metrics();
                changed = true;
            }
#endif

            update_host_animation_frame_demand();
            scene_pending = scene_pending || changed;
            const auto now = std::chrono::steady_clock::now();
            // A resize and its host RAF are one browser rendering opportunity.
            // Do not defer that completed boundary behind a producer-only phase:
            // macOS live resize may not deliver another compositor callback until
            // the following display slot.
            const auto resize_frame_boundary =
                resize_applied && host_frame_applied;
            if (scene_pending
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
            wake_.wait_for(lock, idle_wait, [this, &token, &deferred_input] {
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
            wake_pending_ = false;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
        runtime_.reset();
#endif
    }

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

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    void update_compilation_metrics()
    {
        if (runtime_ == nullptr) return;
        compilation_requests_.store(runtime_->compilation_requests(), std::memory_order_relaxed);
        compilation_memory_hits_.store(runtime_->compilation_memory_hits(), std::memory_order_relaxed);
        compilation_persistent_hits_.store(runtime_->compilation_persistent_hits(), std::memory_order_relaxed);
        compilation_persistent_misses_.store(runtime_->compilation_persistent_misses(), std::memory_order_relaxed);
        compilation_cache_rejections_.store(runtime_->compilation_cache_rejections(), std::memory_order_relaxed);
        compilation_cache_bytes_read_.store(runtime_->compilation_cache_bytes_read(), std::memory_order_relaxed);
        compilation_cache_bytes_written_.store(runtime_->compilation_cache_bytes_written(), std::memory_order_relaxed);
        compilation_time_nanoseconds_.store(runtime_->compilation_time_nanoseconds(), std::memory_order_relaxed);
        process_compilation_memory_hits_.store(
            runtime_->process_compilation_memory_hits(),
            std::memory_order_relaxed);
        process_compilation_leaders_.store(
            runtime_->process_compilation_leaders(),
            std::memory_order_relaxed);
        process_compilation_waiters_.store(
            runtime_->process_compilation_waiters(),
            std::memory_order_relaxed);
        process_compilation_shared_bytes_.store(
            runtime_->process_compilation_shared_bytes(),
            std::memory_order_relaxed);
        process_resource_memory_hits_.store(
            runtime_->process_resource_memory_hits(),
            std::memory_order_relaxed);
        process_resource_load_leaders_.store(
            runtime_->process_resource_load_leaders(),
            std::memory_order_relaxed);
        process_resource_load_waiters_.store(
            runtime_->process_resource_load_waiters(),
            std::memory_order_relaxed);
        process_resource_shared_bytes_.store(
            runtime_->process_resource_shared_bytes(),
            std::memory_order_relaxed);
        process_script_source_memory_hits_.store(
            runtime_->process_script_source_memory_hits(),
            std::memory_order_relaxed);
        process_script_source_shared_bytes_.store(
            runtime_->process_script_source_shared_bytes(),
            std::memory_order_relaxed);
        shared_isolate_slot_.store(
            runtime_->shared_isolate_slot(),
            std::memory_order_relaxed);
        shared_isolate_active_contexts_.store(
            runtime_->shared_isolate_active_contexts(),
            std::memory_order_relaxed);
        shared_isolate_peak_contexts_.store(
            runtime_->shared_isolate_peak_contexts(),
            std::memory_order_relaxed);
        resource_cache_requests_.store(runtime_->resource_cache_requests(), std::memory_order_relaxed);
        resource_cache_hits_.store(runtime_->resource_cache_hits(), std::memory_order_relaxed);
        resource_cache_misses_.store(runtime_->resource_cache_misses(), std::memory_order_relaxed);
        resource_cache_rejections_.store(runtime_->resource_cache_rejections(), std::memory_order_relaxed);
        resource_cache_bytes_read_.store(runtime_->resource_cache_bytes_read(), std::memory_order_relaxed);
        resource_cache_bytes_written_.store(runtime_->resource_cache_bytes_written(), std::memory_order_relaxed);
        input_events_dispatched_.store(runtime_->input_events_dispatched(), std::memory_order_relaxed);
        input_callbacks_invoked_.store(runtime_->input_callbacks_invoked(), std::memory_order_relaxed);

        // Heap code/metadata statistics walk V8's code spaces and are orders of
        // magnitude more expensive than the atomic compilation/resource counters
        // above. This method runs after animation frames and input dispatch, so
        // collecting a full memory census on every turn directly reduces interactive
        // frame cadence (and scales that cost with every active document). Memory
        // metrics are diagnostic snapshots, not scheduling inputs; keep the cached
        // ABI view reasonably fresh without placing the census on the hot path.
        const auto memory_metrics_now = std::chrono::steady_clock::now();
        constexpr auto memory_metrics_refresh_interval =
            std::chrono::seconds(5);
        if (last_memory_metrics_update_
                != std::chrono::steady_clock::time_point{}
            && memory_metrics_now - last_memory_metrics_update_
                < memory_metrics_refresh_interval) {
            return;
        }
        last_memory_metrics_update_ = memory_metrics_now;
        const auto memory = runtime_->read_memory_metrics();
        v8_total_heap_bytes_.store(memory.total_heap_bytes, std::memory_order_relaxed);
        v8_used_heap_bytes_.store(memory.used_heap_bytes, std::memory_order_relaxed);
        v8_executable_heap_bytes_.store(memory.executable_heap_bytes, std::memory_order_relaxed);
        v8_physical_heap_bytes_.store(memory.physical_heap_bytes, std::memory_order_relaxed);
        v8_external_bytes_.store(memory.external_bytes, std::memory_order_relaxed);
        v8_malloced_bytes_.store(memory.malloced_bytes, std::memory_order_relaxed);
        v8_peak_malloced_bytes_.store(memory.peak_malloced_bytes, std::memory_order_relaxed);
        v8_code_and_metadata_bytes_.store(
            memory.code_and_metadata_bytes,
            std::memory_order_relaxed);
        v8_bytecode_and_metadata_bytes_.store(
            memory.bytecode_and_metadata_bytes,
            std::memory_order_relaxed);
        v8_external_script_source_bytes_.store(
            memory.external_script_source_bytes,
            std::memory_order_relaxed);
        v8_young_space_used_bytes_.store(
            memory.young_space_used_bytes,
            std::memory_order_relaxed);
        v8_young_space_physical_bytes_.store(
            memory.young_space_physical_bytes,
            std::memory_order_relaxed);
        v8_old_space_used_bytes_.store(
            memory.old_space_used_bytes,
            std::memory_order_relaxed);
        v8_old_space_physical_bytes_.store(
            memory.old_space_physical_bytes,
            std::memory_order_relaxed);
        v8_code_space_used_bytes_.store(
            memory.code_space_used_bytes,
            std::memory_order_relaxed);
        v8_code_space_physical_bytes_.store(
            memory.code_space_physical_bytes,
            std::memory_order_relaxed);
        v8_map_space_used_bytes_.store(
            memory.map_space_used_bytes,
            std::memory_order_relaxed);
        v8_map_space_physical_bytes_.store(
            memory.map_space_physical_bytes,
            std::memory_order_relaxed);
        v8_large_object_space_used_bytes_.store(
            memory.large_object_space_used_bytes,
            std::memory_order_relaxed);
        v8_large_object_space_physical_bytes_.store(
            memory.large_object_space_physical_bytes,
            std::memory_order_relaxed);
        v8_read_only_space_used_bytes_.store(
            memory.read_only_space_used_bytes,
            std::memory_order_relaxed);
        v8_read_only_space_physical_bytes_.store(
            memory.read_only_space_physical_bytes,
            std::memory_order_relaxed);
        v8_shared_space_used_bytes_.store(
            memory.shared_space_used_bytes,
            std::memory_order_relaxed);
        v8_shared_space_physical_bytes_.store(
            memory.shared_space_physical_bytes,
            std::memory_order_relaxed);
        v8_trusted_space_used_bytes_.store(
            memory.trusted_space_used_bytes,
            std::memory_order_relaxed);
        v8_trusted_space_physical_bytes_.store(
            memory.trusted_space_physical_bytes,
            std::memory_order_relaxed);
        process_compilation_cache_bytes_.store(
            memory.process_compilation_cache_bytes,
            std::memory_order_relaxed);
        process_compilation_mapped_cache_bytes_.store(
            memory.process_compilation_mapped_cache_bytes,
            std::memory_order_relaxed);
        process_resource_cache_bytes_.store(
            memory.process_resource_cache_bytes,
            std::memory_order_relaxed);
        process_resource_mapped_cache_bytes_.store(
            memory.process_resource_mapped_cache_bytes,
            std::memory_order_relaxed);
        native_dom_node_count_.store(
            memory.native_dom_node_count,
            std::memory_order_relaxed);
        native_dom_node_size_bytes_.store(
            memory.native_dom_node_size_bytes,
            std::memory_order_relaxed);
        native_dom_inline_bytes_.store(
            memory.native_dom_inline_bytes,
            std::memory_order_relaxed);
        native_dom_node_pool_reserved_bytes_.store(
            memory.native_dom_node_pool_reserved_bytes,
            std::memory_order_relaxed);
        native_dom_node_pool_peak_bytes_.store(
            memory.native_dom_node_pool_peak_bytes,
            std::memory_order_relaxed);
        native_dom_table_layout_count_.store(
            memory.native_dom_table_layout_count,
            std::memory_order_relaxed);
        native_dom_table_layout_storage_bytes_.store(
            memory.native_dom_table_layout_storage_bytes,
            std::memory_order_relaxed);
        native_dom_form_control_count_.store(
            memory.native_dom_form_control_count,
            std::memory_order_relaxed);
        native_dom_form_control_storage_bytes_.store(
            memory.native_dom_form_control_storage_bytes,
            std::memory_order_relaxed);
        native_event_listener_count_.store(
            memory.native_event_listener_count,
            std::memory_order_relaxed);
        native_event_listener_storage_bytes_.store(
            memory.native_event_listener_storage_bytes,
            std::memory_order_relaxed);
        native_dom_attribute_node_count_.store(
            memory.native_dom_attribute_node_count,
            std::memory_order_relaxed);
        native_dom_attribute_entry_count_.store(
            memory.native_dom_attribute_entry_count,
            std::memory_order_relaxed);
        native_dom_attribute_storage_bytes_.store(
            memory.native_dom_attribute_storage_bytes,
            std::memory_order_relaxed);
        native_wrapper_handle_count_.store(
            memory.native_wrapper_handle_count,
            std::memory_order_relaxed);
        native_wrapper_storage_bytes_.store(
            memory.native_wrapper_storage_bytes,
            std::memory_order_relaxed);
        native_text_measurement_cache_entry_count_.store(
            memory.native_text_measurement_cache_entry_count,
            std::memory_order_relaxed);
        native_text_measurement_cache_storage_bytes_.store(
            memory.native_text_measurement_cache_storage_bytes,
            std::memory_order_relaxed);
        native_dom_pseudo_storage_bytes_.store(
            memory.native_dom_pseudo_storage_bytes,
            std::memory_order_relaxed);
        native_dom_canvas_node_count_.store(
            memory.native_dom_canvas_node_count,
            std::memory_order_relaxed);
        native_dom_canvas_storage_bytes_.store(
            memory.native_dom_canvas_storage_bytes,
            std::memory_order_relaxed);
        native_dom_animation_count_.store(
            memory.native_dom_animation_count,
            std::memory_order_relaxed);
        native_dom_animation_storage_bytes_.store(
            memory.native_dom_animation_storage_bytes,
            std::memory_order_relaxed);
        native_dom_custom_property_node_count_.store(
            memory.native_dom_custom_property_node_count,
            std::memory_order_relaxed);
        native_dom_custom_property_entry_count_.store(
            memory.native_dom_custom_property_entry_count,
            std::memory_order_relaxed);
        native_dom_custom_property_storage_bytes_.store(
            memory.native_dom_custom_property_storage_bytes,
            std::memory_order_relaxed);
        native_dom_background_image_count_.store(
            memory.native_dom_background_image_count,
            std::memory_order_relaxed);
        native_dom_background_image_storage_bytes_.store(
            memory.native_dom_background_image_storage_bytes,
            std::memory_order_relaxed);
        native_dom_grid_count_.store(
            memory.native_dom_grid_count,
            std::memory_order_relaxed);
        native_dom_grid_storage_bytes_.store(
            memory.native_dom_grid_storage_bytes,
            std::memory_order_relaxed);
        native_dom_textual_style_count_.store(
            memory.native_dom_textual_style_count,
            std::memory_order_relaxed);
        native_dom_textual_style_storage_bytes_.store(
            memory.native_dom_textual_style_storage_bytes,
            std::memory_order_relaxed);
        native_dom_authored_style_node_count_.store(
            memory.native_dom_authored_style_node_count,
            std::memory_order_relaxed);
        native_dom_authored_style_entry_count_.store(
            memory.native_dom_authored_style_entry_count,
            std::memory_order_relaxed);
        native_dom_authored_style_storage_bytes_.store(
            memory.native_dom_authored_style_storage_bytes,
            std::memory_order_relaxed);
        native_css_rule_count_.store(
            memory.native_css_rule_count,
            std::memory_order_relaxed);
        native_css_rule_storage_bytes_.store(
            memory.native_css_rule_storage_bytes,
            std::memory_order_relaxed);
        native_css_index_storage_bytes_.store(
            memory.native_css_index_storage_bytes,
            std::memory_order_relaxed);
        process_shared_css_rule_count_.store(
            memory.process_shared_css_rule_count,
            std::memory_order_relaxed);
        process_shared_css_rule_storage_bytes_.store(
            memory.process_shared_css_rule_storage_bytes,
            std::memory_order_relaxed);
    }

    void update_component_readiness()
    {
        if (runtime_ != nullptr
            && component_ready_.load(std::memory_order_relaxed) == 0U
            && runtime_->component_ready()) {
            component_ready_.store(1U, std::memory_order_relaxed);
        }
    }
#endif

    bool publish_scene()
    {
        scene_publication_attempts_.fetch_add(1, std::memory_order_relaxed);
        const auto publication_started = std::chrono::steady_clock::now();
        auto next = std::make_shared<scene>();
        next->commands.reserve(command_count_ + 4U);

        uint64_t hash = 1469598103934665603ULL;
        const auto width = static_cast<float>(viewport_width_);
        const auto height = static_cast<float>(viewport_height_);
        uint64_t base_revision = 0;
        uint64_t acknowledged_dom_hash = 0;
        float acknowledged_width = 0;
        float acknowledged_height = 0;
        std::unordered_map<uint32_t, canvas_layer_version> acknowledged_layers;
        std::shared_ptr<const scene> acknowledged_scene;
        {
            std::lock_guard lock(acknowledgement_->mutex);
            const auto ordered_consumer =
                ordered_scene_consumer_.load(std::memory_order_acquire);
            const auto maximum_pending_scenes = ordered_consumer ? 2U : 1U;
            if (acknowledgement_->pending_scenes.size()
                >= maximum_pending_scenes) {
                if (!ordered_consumer && acknowledgement_->revision == 0U) {
                    // Preserve the legacy latest-only startup behavior: before
                    // a renderer establishes its checkpoint base, a newer full
                    // checkpoint may replace the unacquired initial scene.
                    acknowledgement_->pending_scenes.clear();
                } else {
                    blocked_scene_publications_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return false;
                }
            }
            if (!acknowledgement_->pending_scenes.empty()) {
                const auto& base = acknowledgement_->pending_scenes.back();
                base_revision = base->header.revision;
                acknowledged_dom_hash = base->dom_hash;
                acknowledged_width = base->header.viewport_width;
                acknowledged_height = base->header.viewport_height;
                acknowledged_layers = base->full_layer_versions;
                acknowledged_scene =
                    (base->header.flags & scene_flag_dom_replacement) != 0U
                        ? base : acknowledgement_->value;
            } else {
                base_revision = acknowledgement_->revision;
                acknowledged_dom_hash = acknowledgement_->dom_hash;
                acknowledged_width = acknowledgement_->viewport_width;
                acknowledged_height = acknowledgement_->viewport_height;
                acknowledged_layers = acknowledgement_->layer_versions;
                acknowledged_scene = acknowledgement_->value;
            }
        }
        uint32_t scene_flags = base_revision == 0 ? scene_flag_checkpoint : 0U;
        if (native_scene_active_) {
            if (document_.dirty()) {
                const auto layout_started = std::chrono::steady_clock::now();
                document_.layout(width, height);
                last_layout_nanoseconds_.store(
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - layout_started).count()),
                    std::memory_order_relaxed);
            }
            const auto scene_build_started = std::chrono::steady_clock::now();
            document_.build_scene(
                next->commands,
                next->canvas_strings,
                next->canvas_string_bytes);
            last_scene_build_nanoseconds_.store(
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - scene_build_started).count()),
                std::memory_order_relaxed);
            dom_nodes_.store(document_.node_count(), std::memory_order_relaxed);
            layout_passes_.store(document_.layout_passes(), std::memory_order_relaxed);
            iframe_nodes_.store(document_.count_tag("iframe"), std::memory_order_relaxed);
            iframe_html_bytes_.store(
                document_.sum_attribute_bytes("iframe", "object-html")
                    + document_.sum_attribute_bytes("iframe", "frame-html"),
                std::memory_order_relaxed);
            canvas_nodes_.store(document_.count_tag("canvas"), std::memory_order_relaxed);
            const auto busiest_canvas = document_.busiest_canvas_layout();
            busiest_canvas_width_milli_.store(
                static_cast<uint64_t>(std::max(0.0F, busiest_canvas.width) * 1000.0F),
                std::memory_order_relaxed);
            busiest_canvas_height_milli_.store(
                static_cast<uint64_t>(std::max(0.0F, busiest_canvas.height) * 1000.0F),
                std::memory_order_relaxed);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr) {
                update_component_readiness();
                if (component_ready_.load(std::memory_order_relaxed) != 0U) {
                    scene_flags |= scene_flag_component_ready;
                }
            }
#endif
            {
                auto frame_html = document_.first_attribute("iframe", "frame-html");
                if (frame_html.empty()) {
                    frame_html = document_.first_attribute("iframe", "object-html");
                }
                std::lock_guard lock(iframe_html_mutex_);
                iframe_html_ = std::move(frame_html);
            }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto diagnostics_now = std::chrono::steady_clock::now();
            if (!scene_diagnostics_initialized_
                || diagnostics_now - last_scene_diagnostics_update_
                    >= std::chrono::milliseconds(250)) {
                auto next_diagnostics = document_.describe_busiest_canvas();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
                if (runtime_ != nullptr) {
                    if (runtime_diagnostics_.empty()
                        && component_ready_.load(std::memory_order_relaxed) != 0U) {
                        runtime_diagnostics_ = runtime_->diagnostics();
                    }
                    if (!runtime_diagnostics_.empty()) {
                        next_diagnostics += " | runtime=" + runtime_diagnostics_;
                    }
                    next_diagnostics += " | " + runtime_->event_diagnostics();
                }
#endif
                {
                    std::lock_guard lock(scene_diagnostics_mutex_);
                    scene_diagnostics_ = std::move(next_diagnostics);
                }
                scene_diagnostics_initialized_ = true;
                last_scene_diagnostics_update_ = diagnostics_now;
            }
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
#endif
            {
                std::lock_guard lock(canvas_layout_mutex_);
                document_.build_canvas_layouts(canvas_layouts_);
            }
            std::vector<webscene_canvas_layer> all_layers;
            std::vector<webscene_canvas_command> all_canvas_commands;
            std::vector<webscene_scene_string> all_canvas_strings;
            std::vector<char> all_canvas_string_bytes;
            document_.build_canvas_display_lists(
                all_layers,
                all_canvas_commands,
                all_canvas_strings,
                all_canvas_string_bytes);

            uint64_t dom_hash = 1469598103934665603ULL;
            for (const auto& command : next->commands) {
                dom_hash = mix_hash(dom_hash, static_cast<uint64_t>(command.kind) << 32U | command.flags);
                dom_hash = mix_hash(dom_hash, static_cast<uint64_t>(command.node_id) << 32U | command.rgba);
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.x)) << 32U
                        | std::bit_cast<uint32_t>(command.y));
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.width)) << 32U
                        | std::bit_cast<uint32_t>(command.height));
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.radius_top_left)) << 32U
                        | std::bit_cast<uint32_t>(command.radius_top_right));
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.radius_bottom_right)) << 32U
                        | std::bit_cast<uint32_t>(command.radius_bottom_left));
                // SVG rotation is carried in stroke_width for the immutable
                // scene ABI. Omitting it made the DOM animate internally while
                // every intermediate scene compared equal and was discarded.
                dom_hash = mix_hash(
                    dom_hash,
                    std::bit_cast<uint32_t>(command.stroke_width));
            }
            for (const auto byte : next->canvas_string_bytes) {
                dom_hash = mix_hash(dom_hash, static_cast<uint8_t>(byte));
            }
            next->dom_hash = dom_hash;
            const auto viewport_changed = width != acknowledged_width || height != acknowledged_height;
            if (base_revision == 0 || viewport_changed || dom_hash != acknowledged_dom_hash) {
                scene_flags |= scene_flag_dom_replacement;
                const auto localized = base_revision != 0
                    && !viewport_changed
                    && acknowledged_scene != nullptr
                    && append_localized_dom_damage(
                        *acknowledged_scene,
                        *next,
                        width,
                        height,
                        next->damage_rects);
                if (!localized) {
                    next->damage_rects.push_back(webscene_damage_rect{0, 0, width, height});
                }
            } else {
                next->commands.clear();
                next->canvas_strings.clear();
                next->canvas_string_bytes.clear();
            }

            const auto append_damage = [&](float x, float y, float damage_width, float damage_height) {
                if (damage_width <= 0 || damage_height <= 0) return;
                next->damage_rects.push_back(webscene_damage_rect{x, y, damage_width, damage_height});
            };
            for (auto layer : all_layers) {
                const canvas_layer_version version{
                    layer.generation,
                    layer.command_count,
                    layer.string_count,
                    layer.x,
                    layer.y,
                    layer.width,
                    layer.height};
                next->full_layer_versions[layer.node_id] = version;
                const auto old = acknowledged_layers.find(layer.node_id);
                if (base_revision != 0 && old != acknowledged_layers.end() && old->second == version) {
                    continue;
                }

                if (old != acknowledged_layers.end()) {
                    append_damage(old->second.x, old->second.y, old->second.width, old->second.height);
                }
                append_damage(layer.x, layer.y, layer.width, layer.height);

                const auto source_command_offset = layer.command_offset;
                const auto source_string_offset = layer.string_offset;
                layer.flags = canvas_layer_flag_replace;
                layer.command_offset = static_cast<uint32_t>(next->canvas_commands.size());
                layer.string_offset = static_cast<uint32_t>(next->canvas_strings.size());
                next->canvas_commands.insert(
                    next->canvas_commands.end(),
                    all_canvas_commands.begin() + source_command_offset,
                    all_canvas_commands.begin() + source_command_offset + layer.command_count);
                for (uint32_t index = 0; index < layer.string_count; ++index) {
                    const auto& source_string = all_canvas_strings[source_string_offset + index];
                    const auto byte_offset = static_cast<uint32_t>(next->canvas_string_bytes.size());
                    next->canvas_string_bytes.insert(
                        next->canvas_string_bytes.end(),
                        all_canvas_string_bytes.begin() + source_string.byte_offset,
                        all_canvas_string_bytes.begin() + source_string.byte_offset + source_string.byte_length);
                    next->canvas_strings.push_back(webscene_scene_string{
                        byte_offset,
                        source_string.byte_length});
                }
                next->canvas_layers.push_back(layer);
            }
            for (const auto& [node_id, old] : acknowledged_layers) {
                if (next->full_layer_versions.contains(node_id)) continue;
                append_damage(old.x, old.y, old.width, old.height);
                webscene_canvas_layer removed{};
                removed.node_id = node_id;
                removed.flags = canvas_layer_flag_remove;
                removed.x = old.x;
                removed.y = old.y;
                removed.width = old.width;
                removed.height = old.height;
                removed.generation = old.generation + 1U;
                next->canvas_layers.push_back(removed);
            }

            hash = mix_hash(hash, dom_hash);
            for (const auto& layer : all_layers) {
                hash = mix_hash(hash, layer.generation);
                hash = mix_hash(
                    hash,
                    static_cast<uint64_t>(layer.node_id) << 32U | layer.command_count);
                hash = mix_hash(
                    hash,
                    static_cast<uint64_t>(layer.string_count) << 32U | layer.command_offset);
            }
        } else {
            scene_flags |= scene_flag_dom_replacement;
            next->damage_rects.push_back(webscene_damage_rect{0, 0, width, height});
            const auto horizontal_step = width / 96.0F;
            const auto vertical_step = height / 24.0F;
            for (uint32_t index = 0; index < command_count_; ++index) {
                const auto column = static_cast<float>(index % 96U);
                const auto row = static_cast<float>((index / 96U) % 24U);
                const auto wave = static_cast<float>(std::sin(
                    static_cast<double>(index) * 0.075 + scroll_x_ * 0.001));
                const auto x = column * horizontal_step;
                const auto y = row * vertical_step + wave * 3.0F
                    + static_cast<float>(std::fmod(scroll_y_, 11.0));
                const auto color = (index % 2U == 0U) ? 0x26A69AFFU : 0xEF5350FFU;
                next->commands.push_back(webscene_scene_command{
                    1U,
                    0U,
                    x,
                    y,
                    horizontal_step * 0.72F,
                    vertical_step * 0.65F,
                    color,
                    index + 1U,
                    0,
                    0,
                    0,
                    0,
                    0});
                hash = mix_hash(hash, static_cast<uint64_t>(index) << 32U | color);
            }
            next->dom_hash = hash;
        }

        const auto revision = next_revision_++;
        next->header = webscene_scene_header{
            revision,
            base_revision,
            last_input_sequence_,
            width,
            height,
            static_cast<uint32_t>(next->commands.size()),
            static_cast<uint32_t>(next->canvas_layers.size()),
            static_cast<uint32_t>(next->damage_rects.size()),
            scene_flags,
            mix_hash(
                mix_hash(hash, std::bit_cast<uint32_t>(width)),
                std::bit_cast<uint32_t>(height))};
        const auto latest_scene_bytes = retained_scene_bytes(*next);
        latest_scene_bytes_.store(latest_scene_bytes, std::memory_order_relaxed);
        next->published_timestamp_nanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        auto published_scene = std::shared_ptr<const scene>(std::move(next));
        {
            std::lock_guard lock(acknowledgement_->mutex);
            acknowledgement_->pending_scenes.push_back(published_scene);
        }
        std::atomic_store_explicit(
            &latest_,
            std::move(published_scene),
            std::memory_order_release);
        published_scenes_.fetch_add(1, std::memory_order_relaxed);
        const auto publication_nanoseconds =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - publication_started).count());
        last_scene_publication_nanoseconds_.store(
            publication_nanoseconds,
            std::memory_order_relaxed);
        store_maximum(maximum_scene_publication_nanoseconds_, publication_nanoseconds);
        if (scene_published_callback_ != nullptr) {
            scene_published_callback_(
                scene_published_user_data_,
                revision,
                last_input_sequence_,
                width,
                height);
        }
        return true;
    }

    void set_last_error(std::string value)
    {
        std::lock_guard lock(error_mutex_);
        last_error_ = std::move(value);
    }

    void record_input_dispatch_failure(
        const webscene_input_event& event,
        const std::string& error)
    {
        set_last_error(error);
        auto payload = std::to_string(event.sequence)
            + "\n" + std::to_string(event.kind)
            + "\n" + error;
        std::lock_guard lock(input_dispatch_failure_mutex_);
        input_dispatch_failures_.push_back(std::move(payload));
    }

    uint32_t command_count_;
    std::string compilation_cache_directory_;
    webscene_resource_load_callback resource_load_callback_{nullptr};
    void* resource_load_user_data_{nullptr};
    webscene_scene_published_callback scene_published_callback_{nullptr};
    void* scene_published_user_data_{nullptr};
    webscene_host_request_available_callback
        host_request_available_callback_{nullptr};
    void* host_request_available_user_data_{nullptr};
    mutable std::mutex configuration_mutex_;
    std::string resource_root_;
    input_ring inputs_;
    webscene_input_event pending_resize_{};
    uint64_t pending_resize_count_{0};
    webscene_input_event pending_resize_frame_{};
    uint64_t pending_resize_frame_count_{0};
    bool pending_resize_has_frame_{false};
    std::chrono::steady_clock::time_point pending_resize_frame_enqueued_at_{};
    std::mutex resize_mutex_;
    std::atomic<bool> resize_pending_{false};
    std::atomic<bool> low_memory_requested_{false};
    std::atomic<bool> host_visible_{true};
    std::atomic<bool> visibility_changed_{false};
    std::atomic<uint32_t> preferred_color_scheme_{
        WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT};
    std::atomic<bool> preferred_color_scheme_changed_{false};
    std::atomic<uint8_t> host_animation_frame_requested_{0U};
    webscene_native::native_document document_;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    std::unique_ptr<webscene_native::v8_dom_runtime> runtime_;
#endif
    std::deque<script_work_request> script_work_;
    std::mutex script_mutex_;
    std::shared_ptr<interop_result_pool_v1> interop_result_pool_{
        std::make_shared<interop_result_pool_v1>()};
    mutable std::mutex interop_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<interop_operation_v1>>
        interop_operations_;
    std::vector<std::shared_ptr<interop_operation_v1>>
        interop_operation_slots_;
    std::vector<size_t> available_interop_operation_slots_;
    size_t interop_operation_slot_high_water_{0};
    mutable std::mutex interop_request_pool_mutex_;
    std::vector<webscene_native::interop_request_data_v2>
        available_interop_requests_;
    std::atomic<uint64_t> interop_request_pool_hits_{0};
    std::atomic<uint64_t> interop_request_pool_misses_{0};
    std::atomic<uint64_t> interop_request_oversize_allocations_{0};
    std::atomic<uint64_t> next_interop_operation_id_{1U};
    std::shared_ptr<const scene> latest_{};
    std::atomic<bool> ordered_scene_consumer_{false};
    std::condition_variable wake_;
    std::mutex wake_mutex_;
    bool wake_pending_{false};
    uint64_t next_revision_{1};
    uint64_t last_input_sequence_{0};
    double viewport_width_{1000};
    double viewport_height_{616};
    double device_scale_factor_{1};
    double pointer_x_{500};
    double pointer_y_{308};
    double scroll_x_{0};
    double scroll_y_{0};
    std::atomic<uint64_t> enqueued_inputs_{0};
    std::atomic<uint64_t> dropped_inputs_{0};
    std::atomic<uint64_t> consumed_inputs_{0};
    std::atomic<uint64_t> published_scenes_{0};
    std::atomic<uint64_t> acquired_scenes_{0};
    std::atomic<uint64_t> executed_scripts_{0};
    std::atomic<uint64_t> script_errors_{0};
    std::atomic<uint64_t> dom_nodes_{0};
    std::atomic<uint64_t> layout_passes_{0};
    std::atomic<uint64_t> iframe_nodes_{0};
    std::atomic<uint64_t> iframe_html_bytes_{0};
    std::atomic<uint64_t> frame_scripts_executed_{0};
    std::atomic<uint64_t> frame_script_errors_{0};
    std::atomic<uint64_t> canvas_nodes_{0};
    std::atomic<uint64_t> component_ready_{0};
    std::atomic<uint64_t> low_memory_notifications_{0};
    std::atomic<uint64_t> hidden_low_memory_notifications_{0};
    std::atomic<uint64_t> compilation_requests_{0};
    std::atomic<uint64_t> compilation_memory_hits_{0};
    std::atomic<uint64_t> compilation_persistent_hits_{0};
    std::atomic<uint64_t> compilation_persistent_misses_{0};
    std::atomic<uint64_t> compilation_cache_rejections_{0};
    std::atomic<uint64_t> compilation_cache_bytes_read_{0};
    std::atomic<uint64_t> compilation_cache_bytes_written_{0};
    std::atomic<uint64_t> compilation_time_nanoseconds_{0};
    std::atomic<uint64_t> process_compilation_memory_hits_{0};
    std::atomic<uint64_t> process_compilation_leaders_{0};
    std::atomic<uint64_t> process_compilation_waiters_{0};
    std::atomic<uint64_t> process_compilation_shared_bytes_{0};
    std::atomic<uint64_t> process_resource_memory_hits_{0};
    std::atomic<uint64_t> process_resource_load_leaders_{0};
    std::atomic<uint64_t> process_resource_load_waiters_{0};
    std::atomic<uint64_t> process_resource_shared_bytes_{0};
    std::atomic<uint64_t> process_script_source_memory_hits_{0};
    std::atomic<uint64_t> process_script_source_shared_bytes_{0};
    std::atomic<uint64_t> shared_isolate_slot_{
        std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> shared_isolate_active_contexts_{1};
    std::atomic<uint64_t> shared_isolate_peak_contexts_{1};
    std::atomic<uint64_t> v8_total_heap_bytes_{0};
    std::atomic<uint64_t> v8_used_heap_bytes_{0};
    std::atomic<uint64_t> v8_executable_heap_bytes_{0};
    std::atomic<uint64_t> v8_physical_heap_bytes_{0};
    std::atomic<uint64_t> v8_external_bytes_{0};
    std::atomic<uint64_t> v8_malloced_bytes_{0};
    std::atomic<uint64_t> v8_peak_malloced_bytes_{0};
    std::atomic<uint64_t> v8_code_and_metadata_bytes_{0};
    std::atomic<uint64_t> v8_bytecode_and_metadata_bytes_{0};
    std::atomic<uint64_t> v8_external_script_source_bytes_{0};
    std::atomic<uint64_t> v8_young_space_used_bytes_{0};
    std::atomic<uint64_t> v8_young_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_old_space_used_bytes_{0};
    std::atomic<uint64_t> v8_old_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_code_space_used_bytes_{0};
    std::atomic<uint64_t> v8_code_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_map_space_used_bytes_{0};
    std::atomic<uint64_t> v8_map_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_large_object_space_used_bytes_{0};
    std::atomic<uint64_t> v8_large_object_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_read_only_space_used_bytes_{0};
    std::atomic<uint64_t> v8_read_only_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_shared_space_used_bytes_{0};
    std::atomic<uint64_t> v8_shared_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_trusted_space_used_bytes_{0};
    std::atomic<uint64_t> v8_trusted_space_physical_bytes_{0};
    std::atomic<uint64_t> latest_scene_bytes_{0};
    std::atomic<uint64_t> process_compilation_cache_bytes_{0};
    std::atomic<uint64_t> process_compilation_mapped_cache_bytes_{0};
    std::atomic<uint64_t> process_resource_cache_bytes_{0};
    std::atomic<uint64_t> process_resource_mapped_cache_bytes_{0};
    std::atomic<uint64_t> native_dom_node_count_{0};
    std::atomic<uint64_t> native_dom_node_size_bytes_{0};
    std::atomic<uint64_t> native_dom_inline_bytes_{0};
    std::atomic<uint64_t> native_dom_node_pool_reserved_bytes_{0};
    std::atomic<uint64_t> native_dom_node_pool_peak_bytes_{0};
    std::atomic<uint64_t> native_dom_table_layout_count_{0};
    std::atomic<uint64_t> native_dom_table_layout_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_form_control_count_{0};
    std::atomic<uint64_t> native_dom_form_control_storage_bytes_{0};
    std::atomic<uint64_t> native_event_listener_count_{0};
    std::atomic<uint64_t> native_event_listener_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_attribute_node_count_{0};
    std::atomic<uint64_t> native_dom_attribute_entry_count_{0};
    std::atomic<uint64_t> native_dom_attribute_storage_bytes_{0};
    std::atomic<uint64_t> native_wrapper_handle_count_{0};
    std::atomic<uint64_t> native_wrapper_storage_bytes_{0};
    std::atomic<uint64_t> native_text_measurement_cache_entry_count_{0};
    std::atomic<uint64_t> native_text_measurement_cache_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_pseudo_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_canvas_node_count_{0};
    std::atomic<uint64_t> native_dom_canvas_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_animation_count_{0};
    std::atomic<uint64_t> native_dom_animation_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_custom_property_node_count_{0};
    std::atomic<uint64_t> native_dom_custom_property_entry_count_{0};
    std::atomic<uint64_t> native_dom_custom_property_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_background_image_count_{0};
    std::atomic<uint64_t> native_dom_background_image_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_grid_count_{0};
    std::atomic<uint64_t> native_dom_grid_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_textual_style_count_{0};
    std::atomic<uint64_t> native_dom_textual_style_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_authored_style_node_count_{0};
    std::atomic<uint64_t> native_dom_authored_style_entry_count_{0};
    std::atomic<uint64_t> native_dom_authored_style_storage_bytes_{0};
    std::atomic<uint64_t> native_css_rule_count_{0};
    std::atomic<uint64_t> native_css_rule_storage_bytes_{0};
    std::atomic<uint64_t> native_css_index_storage_bytes_{0};
    std::atomic<uint64_t> process_shared_css_rule_count_{0};
    std::atomic<uint64_t> process_shared_css_rule_storage_bytes_{0};
    std::chrono::steady_clock::time_point last_memory_metrics_update_{};
    std::atomic<uint64_t> resource_cache_requests_{0};
    std::atomic<uint64_t> resource_cache_hits_{0};
    std::atomic<uint64_t> resource_cache_misses_{0};
    std::atomic<uint64_t> resource_cache_rejections_{0};
    std::atomic<uint64_t> resource_cache_bytes_read_{0};
    std::atomic<uint64_t> resource_cache_bytes_written_{0};
    std::atomic<uint64_t> input_events_dispatched_{0};
    std::atomic<uint64_t> input_callbacks_invoked_{0};
    std::atomic<uint64_t> busiest_canvas_width_milli_{0};
    std::atomic<uint64_t> busiest_canvas_height_milli_{0};
    std::atomic<uint64_t> coalesced_resize_inputs_{0};
    std::atomic<uint64_t> applied_resize_inputs_{0};
    std::atomic<uint64_t> last_resize_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_scene_publication_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_outer_listeners_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_listeners_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_layout_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_observers_nanoseconds_{0};
    std::atomic<uint64_t> coalesced_pointer_move_inputs_{0};
    std::atomic<uint64_t> coalesced_wheel_inputs_{0};
    std::atomic<uint64_t> applied_pointer_move_inputs_{0};
    std::atomic<uint64_t> applied_wheel_inputs_{0};
    std::atomic<uint64_t> applied_animation_frames_{0};
    std::atomic<uint64_t> coalesced_animation_frames_{0};
    std::atomic<uint64_t> submitted_resize_frame_pairs_{0};
    std::atomic<uint64_t> applied_resize_frame_pairs_{0};
    std::atomic<uint64_t> published_resize_frame_pairs_{0};
    std::atomic<uint64_t> total_resize_frame_queue_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_queue_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_queue_nanoseconds_{0};
    std::atomic<uint64_t> total_resize_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> resize_frame_animation_callbacks_{0};
    std::atomic<uint64_t> total_resize_frame_animation_batch_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_animation_batch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_animation_batch_nanoseconds_{0};
    std::atomic<uint64_t> total_resize_frame_to_publication_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_to_publication_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_to_publication_nanoseconds_{0};
    std::atomic<uint64_t> last_animation_advance_nanoseconds_{0};
    std::atomic<uint64_t> last_layout_nanoseconds_{0};
    std::atomic<uint64_t> last_scene_build_nanoseconds_{0};
    std::atomic<uint64_t> maximum_scene_publication_nanoseconds_{0};
    std::atomic<uint64_t> scene_publication_attempts_{0};
    std::atomic<uint64_t> blocked_scene_publications_{0};
    std::atomic<uint64_t> last_input_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_input_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_input_dispatch_sequence_{0};
    std::atomic<uint64_t> dispatched_inputs_{0};
    std::atomic<uint64_t> total_input_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> dispatched_animation_frames_{0};
    std::atomic<uint64_t> total_animation_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_animation_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_animation_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_animation_frame_timestamp_microseconds_{0};
    std::atomic<uint32_t> current_cursor_{WEBSCENE_CURSOR_DEFAULT};
    std::atomic<bool> checkpoint_requested_{false};
    mutable std::mutex iframe_html_mutex_;
    std::string iframe_html_;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    mutable std::mutex scene_diagnostics_mutex_;
    std::string scene_diagnostics_;
    bool scene_diagnostics_initialized_{false};
    std::chrono::steady_clock::time_point last_scene_diagnostics_update_{};
    std::string runtime_diagnostics_;
#endif
    mutable std::mutex canvas_layout_mutex_;
    std::vector<webscene_canvas_layout> canvas_layouts_;
    mutable std::mutex error_mutex_;
    std::string last_error_;
    mutable std::mutex host_request_mutex_;
    std::string pending_host_request_;
    mutable std::mutex console_message_mutex_;
    std::string pending_console_message_;
    mutable std::mutex input_dispatch_failure_mutex_;
    std::deque<std::string> input_dispatch_failures_;
    bool native_scene_active_{false};
    std::shared_ptr<acknowledgement_state> acknowledgement_{
        std::make_shared<acknowledgement_state>()};
    // Keep the worker last: every field it can observe is fully initialized before
    // the thread starts, and jthread joins before those fields are destroyed.
    std::jthread worker_;
};

struct webscene_scene_lease final {
    std::shared_ptr<const scene> value;
    std::shared_ptr<acknowledgement_state> acknowledgement;
    webscene_scene_view view{};

    webscene_scene_lease(
        std::shared_ptr<const scene> scene_value,
        std::shared_ptr<acknowledgement_state> acknowledgement_value)
        : value(std::move(scene_value))
        , acknowledgement(std::move(acknowledgement_value))
    {
        view = webscene_scene_view{
            static_cast<uint32_t>(sizeof(webscene_scene_view)),
            2U,
            value->header,
            value->commands.data(),
            value->canvas_layers.data(),
            value->canvas_commands.data(),
            value->canvas_strings.data(),
            value->canvas_string_bytes.data(),
            value->damage_rects.data(),
            this,
            static_cast<uint32_t>(value->canvas_commands.size()),
            static_cast<uint32_t>(value->canvas_strings.size()),
            static_cast<uint32_t>(value->canvas_string_bytes.size()),
            0U};
    }

    bool acknowledge()
    {
        std::lock_guard lock(acknowledgement->mutex);
        const auto is_checkpoint = (value->header.flags & scene_flag_checkpoint) != 0U;
        if (value->header.revision <= acknowledgement->revision
            || (!is_checkpoint && value->header.base_revision != acknowledgement->revision)) {
            return value->header.revision == acknowledgement->revision;
        }
        if (acknowledgement->pending_scenes.empty()
            || acknowledgement->pending_scenes.front()->header.revision
                != value->header.revision) {
            return false;
        }
        acknowledgement->revision = value->header.revision;
        acknowledgement->dom_hash = value->dom_hash;
        acknowledgement->viewport_width = value->header.viewport_width;
        acknowledgement->viewport_height = value->header.viewport_height;
        acknowledgement->layer_versions = value->full_layer_versions;
        // Diff scenes with unchanged DOM intentionally carry no DOM commands.
        // Preserve the last replacement snapshot as the comparison base for a
        // later transform/style-only DOM change.
        if ((value->header.flags & scene_flag_dom_replacement) != 0U) {
            acknowledgement->value = value;
        }
        if (value->published_timestamp_nanoseconds != 0U) {
            const auto now_nanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            const auto elapsed = now_nanoseconds
                >= value->published_timestamp_nanoseconds
                ? now_nanoseconds - value->published_timestamp_nanoseconds
                : 0U;
            acknowledgement->acknowledged_scenes.fetch_add(
                1,
                std::memory_order_relaxed);
            acknowledgement->total_acknowledgement_nanoseconds.fetch_add(
                elapsed,
                std::memory_order_relaxed);
            acknowledgement->last_acknowledgement_nanoseconds.store(
                elapsed,
                std::memory_order_relaxed);
            store_maximum(
                acknowledgement->maximum_acknowledgement_nanoseconds,
                elapsed);
        }
        acknowledgement->pending_scenes.pop_front();
        return true;
    }
};

extern "C" {

uint32_t webscene_engine_get_abi_version(void)
{
    return 2U;
}

uint32_t webscene_engine_get_build_features(void)
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    return WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION;
#else
    return 0U;
#endif
}

uint8_t webscene_engine_prewarm(void)
{
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    try {
        webscene_native::prewarm_v8_process();
        return 1U;
    } catch (...) {
        return 0U;
    }
#else
    return 1U;
#endif
}

webscene_engine* webscene_engine_create(uint32_t simulated_chart_command_count)
{
    try {
        return new webscene_engine(simulated_chart_command_count);
    } catch (...) {
        return nullptr;
    }
}

webscene_engine* webscene_engine_create_with_options(const webscene_engine_options* options)
{
    constexpr auto legacy_options_size = offsetof(webscene_engine_options, resource_load_callback);
    if (options == nullptr || options->struct_size < legacy_options_size) {
        return nullptr;
    }
    try {
        std::string cache_directory;
        if (options->compilation_cache_directory != nullptr
            && options->compilation_cache_directory_length > 0U) {
            cache_directory.assign(
                options->compilation_cache_directory,
                options->compilation_cache_directory_length);
        }
        constexpr auto resource_callback_options_size =
            offsetof(webscene_engine_options, scene_published_callback);
        const auto has_resource_callback =
            options->struct_size >= resource_callback_options_size;
        constexpr auto scene_callback_options_size =
            offsetof(webscene_engine_options, text_measure_callback);
        const auto has_scene_published_callback =
            options->struct_size >= scene_callback_options_size;
        constexpr auto text_measure_options_size =
            offsetof(webscene_engine_options, host_request_available_callback);
        const auto has_text_measure_callback =
            options->struct_size >= text_measure_options_size;
        const auto has_host_request_available_callback =
            options->struct_size >= sizeof(webscene_engine_options);
        return new webscene_engine(
            options->simulated_chart_command_count,
            std::move(cache_directory),
            has_resource_callback ? options->resource_load_callback : nullptr,
            has_resource_callback ? options->resource_load_user_data : nullptr,
            has_scene_published_callback ? options->scene_published_callback : nullptr,
            has_scene_published_callback ? options->scene_published_user_data : nullptr,
            has_text_measure_callback ? options->text_measure_callback : nullptr,
            has_text_measure_callback ? options->text_measure_user_data : nullptr,
            has_host_request_available_callback
                ? options->host_request_available_callback
                : nullptr,
            has_host_request_available_callback
                ? options->host_request_available_user_data
                : nullptr);
    } catch (...) {
        return nullptr;
    }
}

void webscene_engine_destroy(webscene_engine* engine)
{
    delete engine;
}

uint8_t webscene_engine_set_resource_root(
    webscene_engine* engine,
    const char* resource_root,
    size_t resource_root_length)
{
    return engine != nullptr
        && engine->set_resource_root(resource_root, resource_root_length)
        ? 1U
        : 0U;
}

uint8_t webscene_engine_load_url(
    webscene_engine* engine,
    const char* url,
    size_t url_length)
{
    return engine != nullptr && engine->load_url(url, url_length) ? 1U : 0U;
}

uint8_t webscene_engine_enqueue(webscene_engine* engine, const webscene_input_event* event)
{
    return engine != nullptr && event != nullptr && engine->enqueue(*event) ? 1U : 0U;
}

uint8_t webscene_engine_enqueue_resize_frame(
    webscene_engine* engine,
    const webscene_input_event* resize_event,
    const webscene_input_event* frame_event)
{
    return engine != nullptr
        && resize_event != nullptr
        && frame_event != nullptr
        && engine->enqueue_resize_frame(*resize_event, *frame_event)
        ? 1U
        : 0U;
}

uint32_t webscene_engine_get_cursor(const webscene_engine* engine)
{
    return engine == nullptr ? WEBSCENE_CURSOR_DEFAULT : engine->cursor();
}

uint8_t webscene_engine_requires_animation_frame(
    const webscene_engine* engine)
{
    return engine == nullptr ? 0U : engine->animation_frame_demand();
}

uint8_t webscene_engine_execute_script(
    webscene_engine* engine,
    const char* source,
    size_t source_length,
    const char* document_name,
    size_t document_name_length)
{
    return engine != nullptr
        && engine->execute_script(source, source_length, document_name, document_name_length)
        ? 1U
        : 0U;
}

size_t webscene_engine_evaluate_json(
    webscene_engine* engine,
    const char* source,
    size_t source_length,
    const char* document_name,
    size_t document_name_length,
    char* destination,
    size_t destination_capacity,
    uint32_t timeout_milliseconds)
{
    return engine == nullptr
        ? 0U
        : engine->evaluate_json(
            source,
            source_length,
            document_name,
            document_name_length,
            destination,
            destination_capacity,
            timeout_milliseconds);
}

uint64_t webscene_engine_begin_invoke_v1(
    webscene_engine* engine,
    const webscene_interop_request_v1* request,
    webscene_interop_completed_callback_v1 completed,
    void* user_data)
{
    return engine == nullptr
        ? 0U
        : engine->begin_invoke_v1(request, completed, user_data);
}

uint64_t webscene_engine_begin_generated_invoke_v2(
    webscene_engine* engine,
    const webscene_interop_request_v2* request,
    webscene_interop_completed_callback_v1 completed,
    void* user_data)
{
    return engine == nullptr
        ? 0U
        : engine->begin_generated_invoke_v2(
            request,
            completed,
            user_data);
}

const webscene_interop_result_view_v1*
webscene_engine_take_invoke_result_v1(
    webscene_engine* engine,
    uint64_t operation_id)
{
    return engine == nullptr
        ? nullptr
        : engine->take_invoke_result_v1(operation_id);
}

uint8_t webscene_engine_cancel_invoke_v1(
    webscene_engine* engine,
    uint64_t operation_id)
{
    return engine != nullptr && engine->cancel_invoke_v1(operation_id)
        ? 1U
        : 0U;
}

void webscene_interop_result_release_v1(
    const webscene_interop_result_view_v1* result)
{
    if (result == nullptr) return;
    {
        std::lock_guard lock(taken_interop_results_mutex);
        if (taken_interop_results.erase(result) == 0U) {
            return;
        }
    }
    auto lease = std::unique_ptr<interop_result_lease_v1>(
        static_cast<interop_result_lease_v1*>(
            const_cast<void*>(result->lease_token)));
    auto pool = lease->owner;
    pool->release(std::move(lease));
}

uint8_t webscene_engine_get_interop_pool_metrics_v1(
    const webscene_engine* engine,
    webscene_interop_pool_metrics_v1* metrics)
{
    return engine != nullptr
        && metrics != nullptr
        && engine->get_interop_pool_metrics_v1(*metrics)
        ? 1U
        : 0U;
}

size_t webscene_engine_take_host_request(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_host_request(destination, destination_capacity);
}

size_t webscene_engine_take_console_message(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_console_message(destination, destination_capacity);
}

size_t webscene_engine_take_input_dispatch_failure(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_input_dispatch_failure(destination, destination_capacity);
}

size_t webscene_engine_copy_last_error(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr ? 0U : engine->copy_last_error(destination, destination_capacity);
}

size_t webscene_engine_copy_first_iframe_html(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_first_iframe_html(destination, destination_capacity);
}

size_t webscene_engine_copy_scene_diagnostics(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_scene_diagnostics(destination, destination_capacity);
}

size_t webscene_engine_copy_feature_use(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_feature_use(destination, destination_capacity);
}

size_t webscene_engine_copy_event_listener_inventory(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_event_listener_inventory(destination, destination_capacity);
}

size_t webscene_engine_copy_canvas_layouts(
    const webscene_engine* engine,
    webscene_canvas_layout* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_canvas_layouts(destination, destination_capacity);
}

uint8_t webscene_engine_request_scene_checkpoint(webscene_engine* engine)
{
    return engine != nullptr && engine->request_scene_checkpoint() ? 1U : 0U;
}

uint8_t webscene_engine_request_low_memory(webscene_engine* engine)
{
    return engine != nullptr && engine->request_low_memory() ? 1U : 0U;
}

uint8_t webscene_engine_set_visible(webscene_engine* engine, uint8_t visible)
{
    return engine != nullptr && engine->set_visible(visible != 0) ? 1U : 0U;
}

uint8_t webscene_engine_set_preferred_color_scheme(
    webscene_engine* engine,
    uint32_t preferred_color_scheme)
{
    return engine != nullptr
        && engine->set_preferred_color_scheme(preferred_color_scheme)
        ? 1U
        : 0U;
}

const webscene_scene_view* webscene_engine_acquire_latest_scene(webscene_engine* engine)
{
    if (engine == nullptr) {
        return nullptr;
    }

    auto scene_value = engine->acquire_latest();
    if (!scene_value) {
        return nullptr;
    }

    try {
        auto* lease = new webscene_scene_lease(
            std::move(scene_value),
            engine->acknowledgement_state_handle());
        return &lease->view;
    } catch (...) {
        return nullptr;
    }
}

const webscene_scene_view* webscene_engine_acquire_next_scene(webscene_engine* engine)
{
    if (engine == nullptr) {
        return nullptr;
    }

    auto scene_value = engine->acquire_next();
    if (!scene_value) {
        return nullptr;
    }

    try {
        auto* lease = new webscene_scene_lease(
            std::move(scene_value),
            engine->acknowledgement_state_handle());
        return &lease->view;
    } catch (...) {
        return nullptr;
    }
}

void webscene_scene_release(const webscene_scene_view* scene_view)
{
    if (scene_view == nullptr) return;
    delete static_cast<webscene_scene_lease*>(const_cast<void*>(scene_view->lease_token));
}

uint8_t webscene_scene_acknowledge(const webscene_scene_view* scene_view)
{
    if (scene_view == nullptr || scene_view->lease_token == nullptr) return 0U;
    auto* lease = static_cast<webscene_scene_lease*>(const_cast<void*>(scene_view->lease_token));
    return lease->acknowledge() ? 1U : 0U;
}

uint8_t webscene_scene_get_header(
    const webscene_scene_view* scene_view,
    webscene_scene_header* header)
{
    if (scene_view == nullptr || header == nullptr) {
        return 0U;
    }

    *header = scene_view->header;
    return 1U;
}

const webscene_scene_command* webscene_scene_get_commands(
    const webscene_scene_view* scene_view,
    uint32_t* count)
{
    if (count != nullptr) *count = scene_view == nullptr ? 0U : scene_view->header.command_count;
    return scene_view == nullptr ? nullptr : scene_view->commands;
}

void webscene_engine_get_metrics(
    const webscene_engine* engine,
    webscene_engine_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr) {
        return;
    }
    engine->read_metrics(*metrics);
}

uint8_t webscene_engine_get_input_dispatch_metrics(
    const webscene_engine* engine,
    webscene_input_dispatch_metrics* metrics)
{
    constexpr auto original_struct_size =
        offsetof(webscene_input_dispatch_metrics, dispatched_inputs);
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < original_struct_size) {
        return 0U;
    }
    engine->read_input_dispatch_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_animation_frame_metrics(
    const webscene_engine* engine,
    webscene_animation_frame_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_animation_frame_metrics)) {
        return 0U;
    }
    engine->read_animation_frame_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_scene_flow_metrics(
    const webscene_engine* engine,
    webscene_scene_flow_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_scene_flow_metrics)) {
        return 0U;
    }
    engine->read_scene_flow_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_resize_frame_metrics(
    const webscene_engine* engine,
    webscene_resize_frame_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_resize_frame_metrics)) {
        return 0U;
    }
    engine->read_resize_frame_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_resource_cache_metrics(
    const webscene_engine* engine,
    webscene_resource_cache_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_resource_cache_metrics)) {
        return 0U;
    }
    engine->read_resource_cache_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_process_cache_metrics(
    const webscene_engine* engine,
    webscene_process_cache_metrics* metrics)
{
    constexpr auto original_struct_size =
        offsetof(webscene_process_cache_metrics, script_source_memory_hits);
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < original_struct_size) {
        return 0U;
    }
    engine->read_process_cache_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_memory_metrics(
    const webscene_engine* engine,
    webscene_engine_memory_metrics* metrics)
{
    constexpr auto original_struct_size =
        offsetof(webscene_engine_memory_metrics, v8_code_and_metadata_bytes);
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < original_struct_size) {
        return 0U;
    }
    engine->read_memory_metrics(*metrics);
    return 1U;
}

} // extern "C"
