#pragma once
#include "media_decode.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace webscene::media {
// Bounded owner-independent decode worker. Callers deliver ready results into
// their existing runtime completion mailbox, never call JS from this thread.
class decode_service {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void(std::stop_token)>> pending_;
    bool closing_{};
    std::function<void()> notify_;
    std::jthread worker_;
    template<class Result, class Work> std::future<Result> submit(Work work) {
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        std::lock_guard lock(mutex_);
        if (closing_ || pending_.size() >= 8) throw std::runtime_error("Media decode queue unavailable");
        pending_.push_back([this, promise, work = std::move(work)](std::stop_token stop) mutable {
            try {
                if (stop.stop_requested()) throw std::runtime_error("Media service closed");
                promise->set_value(work(stop));
            } catch (...) { promise->set_exception(std::current_exception()); }
            if(notify_)notify_();
        });
        wake_.notify_one();
        return future;
    }
public:
    explicit decode_service(std::function<void()> notify = {}) : notify_(std::move(notify)), worker_([this](std::stop_token stop) {
        for (;;) {
            std::function<void(std::stop_token)> work;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return closing_ || !pending_.empty(); });
                if (pending_.empty() && closing_) return;
                work = std::move(pending_.front()); pending_.pop_front();
            }
            work(stop);
        }
    }) {}
    ~decode_service() { close(); }
    decode_service(const decode_service&) = delete;
    decode_service& operator=(const decode_service&) = delete;
    // Owner-thread lifecycle operation; closing resolves every queued future.
    void close() {
        { std::lock_guard lock(mutex_); closing_ = true; }
        worker_.request_stop(); wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    std::future<audio_buffer> audio(std::shared_ptr<const encoded_source> source, decode_limits limits = {}, uint32_t target_sample_rate = 0) {
        if (!source) throw std::invalid_argument("Missing media source");
        return submit<audio_buffer>([source = std::move(source), limits, target_sample_rate](std::stop_token stop) {
            return decode_audio(source->bytes, limits, stop, target_sample_rate);
        });
    }
    std::future<video_frame> video(std::shared_ptr<const encoded_source> source, double seconds, decode_limits limits = {}) {
        if (!source) throw std::invalid_argument("Missing media source");
        return submit<video_frame>([source = std::move(source), seconds, limits](std::stop_token stop) {
            return decode_video_frame(*source, seconds, limits, stop);
        });
    }
};
}
