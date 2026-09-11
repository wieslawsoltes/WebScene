#pragma once
#include "command_channel.h"

namespace webscene::graphics {
struct release_ticket { uint64_t channel{},generation{}; size_t slot{}; };
// Reserve one slot BEFORE exposing a wrapper to GC. Publication never allocates
// or competes for command-queue capacity. The engine releases after the command
// prefix accepted when the finalizer ran; later commands cannot starve release.
class release_channel {
    enum class state { free,reserved,ready };
    struct slot { state phase{}; uint64_t generation{},after{}; graphics_command command{}; };
    const uint64_t identity_=new_owner_token();
    const std::thread::id thread_=std::this_thread::get_id();
    mutable std::mutex mutex_;
    std::vector<slot> slots_;
    std::shared_ptr<command_channel> commands_;
    std::shared_ptr<completion_wake> wake_;
    bool closed_{};
    size_t occupied_{};
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_) throw std::logic_error("release reservation requires engine thread");
    }
public:
    release_channel(size_t capacity,std::shared_ptr<command_channel> commands,std::shared_ptr<completion_wake> wake)
        : slots_(capacity),commands_(std::move(commands)),wake_(std::move(wake)) {
        if (!capacity || !commands_) throw std::invalid_argument("release channel requires bounded storage and command ordering");
    }
    std::optional<release_ticket> reserve(graphics_command command) {
        check_thread();
        if (!command.execute) throw std::invalid_argument("release dispatcher is required");
        std::lock_guard lock(mutex_);
        if (closed_) return {};
        for (size_t i=0;i<slots_.size();++i) {
            auto& item=slots_[i];
            if (item.phase==state::free && item.generation!=UINT64_MAX) {
                ++item.generation; item.phase=state::reserved; item.command=command; ++occupied_;
                return release_ticket{identity_,item.generation,i};
            }
        }
        return {};
    }
    bool publish(release_ticket ticket) {
        // Callers must retain resources for every queued use before finalization.
        const auto after=commands_->metrics().accepted;
        {
            std::lock_guard lock(mutex_);
            if (closed_ || ticket.channel!=identity_ || ticket.slot>=slots_.size()) return false;
            auto& item=slots_[ticket.slot];
            if (item.generation!=ticket.generation || item.phase!=state::reserved) return false;
            item.after=after; item.phase=state::ready;
        }
        if (wake_) wake_->signal();
        return true;
    }
    size_t occupied() const { std::lock_guard lock(mutex_); return occupied_; }
private:
    friend class graphics_service;
    bool has_ready(uint64_t completed) const {
        std::lock_guard lock(mutex_);
        return std::any_of(slots_.begin(),slots_.end(),[&](const auto& item) {
            return item.phase==state::ready && item.after<=completed;
        });
    }
    template<class Execute> bool consume_one(uint64_t completed,Execute execute) {
        check_thread();
        graphics_command command;
        {
            std::lock_guard lock(mutex_);
            auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& item) {
                return item.phase==state::ready && item.after<=completed;
            });
            if (found==slots_.end()) return false;
            command=found->command; found->phase=state::free; --occupied_;
        }
        execute(command);
        return true;
    }
    void close() {
        check_thread();
        std::lock_guard lock(mutex_);
        closed_=true;
        // Unpublished registrations are owned by engine-wide resource teardown.
        for (auto& item:slots_) if (item.phase==state::reserved) { item.phase=state::free; --occupied_; }
    }
};
} // namespace webscene::graphics
