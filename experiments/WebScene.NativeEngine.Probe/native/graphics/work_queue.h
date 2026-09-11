#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace webscene::graphics {
enum class enqueue_result { accepted, full, closed, upload_too_large };
struct queue_metrics {
    size_t depth{};
    size_t high_water{};
    uint64_t accepted{};
    uint64_t upload_bytes{};
};

// Fixed-capacity native command/record storage. Command must contain only native
// values or separately owned handles, never borrowed V8 pointers. One slot stays
// occupied throughout consumption, so reentrant producers cannot overwrite an
// upload while a native API is still reading it. The consumer copies/retains data
// further if its backend API outlives the consume scope.
template<class Command> class work_queue {
    static_assert(std::is_trivially_copyable_v<Command>);
    struct slot { Command command{}; size_t bytes{}; uint64_t serial{}; };
    mutable std::mutex mutex_;
    std::vector<slot> slots_;
    std::vector<std::byte> uploads_;
    size_t upload_limit_;
    size_t head_{};
    bool consuming_{};
    bool closed_{};
    queue_metrics metrics_;
public:
    work_queue(size_t capacity, size_t upload_limit) : upload_limit_(upload_limit)
    {
        if (!capacity || (upload_limit && capacity > std::numeric_limits<size_t>::max() / upload_limit))
            throw std::invalid_argument("invalid graphics queue capacity");
        slots_.resize(capacity);
        uploads_.resize(capacity * upload_limit);
    }
    // full is backpressure: the caller retains/retries the command. No accepted
    // side effect is dropped or coalesced. Copy happens before this call returns.
    enqueue_result try_push(Command command, std::span<const std::byte> upload = {})
    {
        std::lock_guard lock(mutex_);
        if (closed_) return enqueue_result::closed;
        if (upload.size() > upload_limit_) return enqueue_result::upload_too_large;
        if (metrics_.depth == slots_.size()) return enqueue_result::full;
        if (metrics_.accepted == std::numeric_limits<uint64_t>::max()
            || upload.size() > std::numeric_limits<uint64_t>::max() - metrics_.upload_bytes)
            throw std::overflow_error("graphics queue counters exhausted");
        const auto index = (head_ + metrics_.depth) % slots_.size();
        auto& item = slots_[index];
        item.command = command;
        item.bytes = upload.size();
        if (!upload.empty()) std::copy(upload.begin(), upload.end(), uploads_.begin() + index * upload_limit_);
        item.serial = ++metrics_.accepted;
        metrics_.upload_bytes += upload.size();
        ++metrics_.depth;
        metrics_.high_water = std::max(metrics_.high_water, metrics_.depth);
        return enqueue_result::accepted;
    }
    template<class Consumer> bool consume_one(Consumer consume)
    {
        size_t index;
        {
            std::lock_guard lock(mutex_);
            if (consuming_) throw std::logic_error("graphics queue requires one non-reentrant consumer");
            if (!metrics_.depth) return false;
            consuming_ = true;
            index = head_;
        }
        auto release = [&] {
            std::lock_guard lock(mutex_);
            head_ = (head_ + 1) % slots_.size();
            --metrics_.depth;
            consuming_ = false;
        };
        const auto& item = slots_[index];
        const auto upload = item.bytes
            ? std::span<const std::byte>(uploads_.data() + index * upload_limit_, item.bytes)
            : std::span<const std::byte>{};
        try { consume(item.command, upload, item.serial); }
        catch (...) { release(); throw; }
        release();
        return true;
    }
    // Stops admission; accepted work remains drainable during shutdown.
    void close() { std::lock_guard lock(mutex_); closed_ = true; }
    queue_metrics metrics() const { std::lock_guard lock(mutex_); return metrics_; }
};
} // namespace webscene::graphics
