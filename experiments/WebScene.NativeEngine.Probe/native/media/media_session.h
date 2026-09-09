#pragma once
#include "media_decode.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace webscene::media {
// Persistent source/decoder with generation-based cancellation and a one-frame
// mailbox. Owner asks for a media time; decoder never drives presentation itself.
class media_session {
  public:
    struct snapshot {
        uint64_t generation{}, version{};
        double duration{};
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
    bool video_{}, closing_{};
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
    void close();
};
} // namespace webscene::media
