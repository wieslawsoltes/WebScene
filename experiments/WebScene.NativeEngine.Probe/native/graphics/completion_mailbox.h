#pragma once
#include "resource_table.h"
#include <mutex>
#include <optional>
#include <chrono>
#include <algorithm>

namespace webscene::graphics {
enum class completion_status { success, failed, cancelled, device_lost };
struct completion_record {
    uint64_t operation{};
    resource_owner owner{};
    completion_status status{};
};
struct completion_ticket { uint64_t mailbox{}; uint64_t generation{}; size_t slot{}; };
// Wake implementations may only signal an engine task queue, never enter V8.
// Shared ownership lets late driver callbacks finish without touching a dead engine.
struct completion_wake {
    virtual ~completion_wake() = default;
    virtual void signal() noexcept = 0;
};
struct completion_metrics {
    size_t pending{},ready{},high_water{};
    uint64_t admitted{},delivered{},rejected_publications{},saturated_reservations{};
    uint64_t latency_samples{},total_latency_ns{},max_latency_ns{};
};
class completion_mailbox {
    enum class state { free, pending, ready };
    struct slot {
        state phase{state::free};
        uint64_t generation{};
        completion_record record{};
        std::chrono::steady_clock::time_point admitted_at{};
    };
    const uint64_t identity_ = new_owner_token();
    const std::thread::id engine_thread_ = std::this_thread::get_id();
    mutable std::mutex mutex_;
    std::vector<slot> slots_;
    std::vector<size_t> ready_;
    size_t head_{};
    size_t count_{};
    size_t pending_{};
    bool closed_{};
    const bool measure_latency_;
    completion_metrics metrics_{};
    std::shared_ptr<completion_wake> wake_;
    void check_engine() const {
        if (std::this_thread::get_id() != engine_thread_)
            throw std::logic_error("graphics completion delivery requires engine thread");
    }
    void make_ready(size_t index) {
        auto& item = slots_[index];
        if (item.phase != state::ready) {
            if (item.phase == state::pending) --pending_;
            ready_[(head_ + count_) % ready_.size()] = index;
            ++count_;
            item.phase = state::ready;
        }
    }
public:
    completion_mailbox(size_t capacity, std::shared_ptr<completion_wake> wake, bool measure_latency=false)
        : slots_(capacity), ready_(capacity), measure_latency_(measure_latency), wake_(std::move(wake)) {
        if (!capacity) throw std::invalid_argument("completion capacity must be positive");
    }
    // Reserve BEFORE issuing an asynchronous backend operation. Every admitted
    // operation owns a completion slot, so driver callbacks can never overflow.
    std::optional<completion_ticket> reserve(uint64_t operation, resource_owner owner) {
        check_engine();
        std::lock_guard lock(mutex_);
        if (closed_) return {};
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& item = slots_[i];
            if (item.phase == state::free && item.generation != UINT64_MAX) {
                ++item.generation;
                item.phase = state::pending;
                ++pending_;
                ++metrics_.admitted;
                metrics_.high_water=std::max(metrics_.high_water,pending_+count_);
                if (measure_latency_) item.admitted_at=std::chrono::steady_clock::now();
                item.record = {operation, owner, completion_status::success};
                return completion_ticket{identity_, item.generation, i};
            }
        }
        ++metrics_.saturated_reservations;
        return {};
    }
    bool publish(completion_ticket ticket, completion_status status) {
        std::shared_ptr<completion_wake> wake;
        {
            std::lock_guard lock(mutex_);
            if (closed_ || ticket.mailbox != identity_ || ticket.slot >= slots_.size()) {
                ++metrics_.rejected_publications; return false;
            }
            auto& item = slots_[ticket.slot];
            if (item.generation != ticket.generation || item.phase != state::pending) {
                ++metrics_.rejected_publications; return false;
            }
            item.record.status = status;
            make_ready(ticket.slot);
            wake = wake_;
        }
        if (wake) wake->signal();
        return true;
    }
    void cancel_owner(resource_owner owner, completion_status status = completion_status::cancelled) {
        check_engine();
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& item = slots_[i];
            if (item.phase != state::free && item.record.owner == owner) {
                item.record.status = status;
                make_ready(i);
            }
        }
    }
    void close() {
        check_engine();
        std::lock_guard lock(mutex_);
        closed_ = true;
        wake_.reset();
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].phase != state::free) {
                slots_[i].record.status = completion_status::cancelled;
                make_ready(i);
            }
    }
    completion_metrics metrics() const {
        std::lock_guard lock(mutex_);
        auto result=metrics_;
        result.pending=pending_; result.ready=count_;
        return result;
    }
    bool has_pending() const { std::lock_guard lock(mutex_); return pending_ != 0; }
    bool has_ready() const { std::lock_guard lock(mutex_); return count_ != 0; }
    template<class Deliver> bool drain_one(Deliver deliver) {
        check_engine();
        completion_record record;
        {
            std::lock_guard lock(mutex_);
            if (!count_) return false;
            auto& item = slots_[ready_[head_]];
            record = item.record;
            ++metrics_.delivered;
            if (measure_latency_) {
                const auto elapsed=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-item.admitted_at).count());
                ++metrics_.latency_samples;
                metrics_.total_latency_ns+=elapsed;
                metrics_.max_latency_ns=std::max(metrics_.max_latency_ns,elapsed);
            }
            item.phase = state::free;
            head_ = (head_ + 1) % ready_.size();
            --count_;
        }
        deliver(record); // Only this engine-thread scope may resolve JS promises.
        return true;
    }
};
} // namespace webscene::graphics
