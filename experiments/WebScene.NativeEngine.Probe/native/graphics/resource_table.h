#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace webscene::graphics {

// Tokens identify owners across engines as well as within one resource table.
inline uint64_t new_owner_token()
{
    static std::atomic<uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("graphics owner identity exhausted");
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            return value;
    }
}

struct resource_owner {
    uint64_t engine;
    uint64_t device;
    uint64_t context;
    bool operator==(const resource_owner&) const = default;
};

template<class T> struct resource_handle {
    uint64_t table{};
    uint64_t generation{};
    uint32_t slot{};
};

// Confined to its graphics execution thread. T's destructor therefore also runs
// on that thread. Finalizers and driver callbacks must enqueue release/complete
// requests, never call this table directly. A pointer returned by get() is only
// borrowed for the current synchronous execution scope.
template<class T> class resource_table {
    struct entry {
        std::unique_ptr<T> value;
        resource_owner owner{};
        uint64_t generation{1};
        uint64_t last_submission{};
        bool destroyed{};
    };
    const uint64_t identity_ = new_owner_token();
    const std::thread::id thread_ = std::this_thread::get_id();
    const size_t capacity_;
    const resource_owner owner_;
    std::vector<entry> entries_;
    uint64_t completed_{};

    void check_thread() const
    {
        if (std::this_thread::get_id() != thread_)
            throw std::logic_error("graphics resource accessed from wrong thread");
    }
    entry& resolve(resource_handle<T> handle, resource_owner owner)
    {
        check_thread();
        if (handle.table != identity_ || handle.slot >= entries_.size())
            throw std::invalid_argument("foreign graphics resource handle");
        auto& item = entries_[handle.slot];
        if (!item.value || item.destroyed || item.generation != handle.generation
            || item.owner != owner)
            throw std::invalid_argument("stale or wrong-owner graphics resource handle");
        return item;
    }
    void release_ready()
    {
        for (auto& item : entries_) {
            if (item.value && item.destroyed && item.last_submission <= completed_)
                item.value.reset();
        }
    }
public:
    explicit resource_table(size_t capacity, resource_owner owner) : capacity_(capacity), owner_(owner)
    {
        if (!owner.engine || !owner.device)
            throw std::invalid_argument("graphics table requires a device owner");
        if (capacity > std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("graphics table capacity too large");
        entries_.reserve(capacity);
    }
    resource_table(const resource_table&) = delete;
    resource_table& operator=(const resource_table&) = delete;
    ~resource_table()
    {
        // Teardown must drain the GPU before destroying this execution scope.
        // Silent release here would turn a shutdown bug into GPU use-after-free.
        if (std::this_thread::get_id() != thread_) std::terminate();
        for (const auto& item : entries_)
            if (item.value && item.last_submission > completed_) std::terminate();
    }
    resource_handle<T> insert(resource_owner owner, std::unique_ptr<T> value)
    {
        check_thread();
        if (!value || owner != owner_)
            throw std::invalid_argument("graphics resource requires an owner and value");
        for (uint32_t i = 0; i < entries_.size(); ++i) {
            auto& item = entries_[i];
            if (!item.value && item.generation < std::numeric_limits<uint64_t>::max()) {
                ++item.generation;
                item.owner = owner;
                item.destroyed = false;
                item.last_submission = 0;
                item.value = std::move(value);
                return {identity_, item.generation, i};
            }
        }
        if (entries_.size() == capacity_) throw std::length_error("graphics resource limit reached");
        entries_.push_back({std::move(value), owner});
        return {identity_, 1, static_cast<uint32_t>(entries_.size() - 1)};
    }
    T& get(resource_handle<T> handle, resource_owner owner) { return *resolve(handle, owner).value; }
    void mark_used(resource_handle<T> handle, resource_owner owner, uint64_t submission)
    {
        auto& item = resolve(handle, owner);
        if (submission <= completed_ || submission < item.last_submission)
            throw std::invalid_argument("graphics submission serial is not monotonic");
        item.last_submission = submission;
    }
    void destroy(resource_handle<T> handle, resource_owner owner)
    {
        resolve(handle, owner).destroyed = true;
        release_ready();
    }
    void destroy_owner(resource_owner owner)
    {
        check_thread();
        for (auto& item : entries_)
            if (item.value && item.owner == owner) item.destroyed = true;
        release_ready();
    }
    // Each table belongs to exactly one device/context submission timeline.
    // completion is a contiguous completed prefix for that queue only.
    void complete(uint64_t submission)
    {
        check_thread();
        if (submission < completed_) throw std::invalid_argument("graphics completion moved backwards");
        completed_ = submission;
        release_ready();
    }
    size_t resident_count() const
    {
        check_thread();
        size_t count = 0;
        for (const auto& item : entries_) count += item.value != nullptr;
        return count;
    }
    size_t deferred_count() const
    {
        check_thread();
        size_t count = 0;
        for (const auto& item : entries_) count += item.value && item.destroyed;
        return count;
    }
};
} // namespace webscene::graphics
