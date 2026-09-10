#pragma once
#include "release_channel.h"
#include <v8.h>

namespace webscene::graphics {
// Binding-owned registry, destroyed in the owning isolate scope before isolate
// disposal. It holds weak wrappers only; all release capacity is reserved before
// a wrapper is exposed to JavaScript. No GPU API runs from either GC callback.
class v8_release_registry {
    struct entry {
        v8::Global<v8::Object> wrapper;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{};
    };
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    std::shared_ptr<release_channel> releases_;
    std::vector<std::unique_ptr<entry>> entries_;
    static void second_pass(const v8::WeakCallbackInfo<entry>& info) {
        auto* item=info.GetParameter();
        item->releases->publish(item->ticket);
        item->published=true;
    }
    static void first_pass(const v8::WeakCallbackInfo<entry>& info) {
        info.GetParameter()->wrapper.Reset();
        info.SetSecondPassCallback(second_pass);
    }
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("graphics weak handles require the owning isolate scope");
    }
public:
    v8_release_registry(v8::Isolate* isolate,std::shared_ptr<release_channel> releases,size_t capacity)
        : isolate_(isolate),releases_(std::move(releases)),entries_(capacity) {
        if (!isolate_ || !releases_ || !capacity) throw std::invalid_argument("invalid graphics wrapper registry");
        check_scope();
    }
    ~v8_release_registry() {
        check_scope();
        for (auto& item:entries_) if (item) {
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    // False means capacity is unavailable: do not expose the wrapper. Its native
    // resource remains the binding caller's responsibility until attach succeeds.
    bool attach(v8::Local<v8::Object> wrapper,graphics_command release) {
        check_scope();
        if (wrapper.IsEmpty()) throw std::invalid_argument("graphics wrapper is empty");
        auto found=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) {
            return !item || item->published;
        });
        if (found==entries_.end()) return false;
        auto item=std::make_unique<entry>();
        auto ticket=releases_->reserve(release);
        if (!ticket) return false;
        item->releases=releases_; item->ticket=*ticket;
        item->wrapper.Reset(isolate_,wrapper);
        item->wrapper.SetWeak(item.get(),first_pass,v8::WeakCallbackType::kParameter);
        *found=std::move(item);
        return true;
    }
};
} // namespace webscene::graphics
