#pragma once
#include "media_decode.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <deque>
#include <mutex>
#include <thread>

namespace webscene::media {
// Consume only frames whose presentation interval has begun. Returns consumed
// count (more than one means obsolete decoded frames were dropped).
inline size_t select_video_frame(std::deque<video_frame>& queue, double time, video_frame& current) {
    size_t consumed = 0;
    while (!queue.empty() && queue.front().timestamp <= time + 1e-7) {
        current = std::move(queue.front());
        queue.pop_front();
        ++consumed;
    }
    return consumed;
}
// Persistent source/decoder with bounded decode-ahead. The host presentation
// opportunity selects a complete frame; decoder completion never advances it.
class media_session {
  public:
    struct snapshot {
        uint64_t generation{}, version{}, selected{}, dropped{}, repeated{};
        double duration{};
        size_t buffered_frames{};
        bool ready{}, seeking{};
        std::string error;
        std::shared_ptr<const audio_buffer> audio;
        video_frame video;
    };

  private:
    std::mutex mutex_;
    std::condition_variable wake_;
    std::shared_ptr<const encoded_source> source_;
    snapshot published_;
    uint64_t generation_{}, request_{};
    double requested_time_{};
    bool video_{}, closing_{}, exhausted_{};
    std::deque<video_frame> queued_;
    static constexpr size_t queue_capacity = 4;
    std::stop_source active_stop_;
    std::function<void()> notify_;
    std::jthread worker_;
    void run(std::stop_token);

  public:
    explicit media_session(std::function<void()> notify = {});
    ~media_session();
    void load(std::shared_ptr<const encoded_source>, bool video);
    void seek(double);
    snapshot read();
    snapshot present(double media_time);
    void close();
};
} // namespace webscene::media
