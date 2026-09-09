#include "media_session.h"
#include <cmath>
#include <stdexcept>
namespace webscene::media {
media_session::media_session(std::function<void()> notify)
    : notify_(std::move(notify)), worker_([this](std::stop_token stop) { run(stop); }) {}
media_session::~media_session() { close(); }
void media_session::close() {
    std::stop_source active;
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
        active = active_stop_;
    }
    active.request_stop();
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void media_session::load(std::shared_ptr<const encoded_source> source, bool video) {
    if (!source || source->bytes.empty() || source->bytes.size() > decode_limits{}.encoded_bytes)
        throw std::invalid_argument("Invalid admitted media source");
    std::stop_source old;
    {
        std::lock_guard lock(mutex_);
        if (closing_)
            throw std::runtime_error("Media session closed");
        old = active_stop_;
        active_stop_ = std::stop_source{};
        source_ = std::move(source);
        video_ = video;
        ++generation_;
        ++request_;
        requested_time_ = 0;
        published_ = {};
        published_.generation = generation_;
    }
    old.request_stop();
    wake_.notify_one();
}
void media_session::seek(double time) {
    if (!std::isfinite(time) || time < 0)
        throw std::invalid_argument("Invalid media seek");
    {
        std::lock_guard lock(mutex_);
        if (closing_)
            throw std::runtime_error("Media session closed");
        requested_time_ = time;
        ++request_;
        published_.seeking = true;
    }
    wake_.notify_one();
}
media_session::snapshot media_session::read() {
    std::lock_guard lock(mutex_);
    return published_;
}
void media_session::run(std::stop_token stop) {
    uint64_t loaded = 0, processed = 0;
    std::unique_ptr<video_decoder> decoder;
    std::shared_ptr<const audio_buffer> pcm;
    while (!stop.stop_requested()) {
        std::shared_ptr<const encoded_source> source;
        uint64_t generation, request;
        double time;
        bool video;
        std::stop_token cancel;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return closing_ || request_ != processed; });
            if (closing_)
                return;
            source = source_;
            generation = generation_;
            request = request_;
            time = requested_time_;
            video = video_;
            cancel = active_stop_.get_token();
        }
        processed = request;
        snapshot next;
        next.generation = generation;
        try {
            if (!source)
                throw std::runtime_error("No media source");
            if (loaded != generation) {
                decoder.reset();
                pcm.reset();
                if (video) {
                    decoder = open_video(*source, {}, cancel);
                    auto audio = decoder->audio(cancel);
                    if (audio.frames())
                        pcm = std::make_shared<const audio_buffer>(std::move(audio));
                } else
                    pcm = std::make_shared<const audio_buffer>(decode_audio(source->bytes, {}, cancel));
                loaded = generation;
            }
            next.audio = pcm;
            if (video) {
                next.video = decoder->read(time, cancel);
                next.duration = decoder->duration();
            } else {
                next.audio = pcm;
                next.duration = double(pcm->frames()) / pcm->sample_rate;
            }
            next.ready = true;
        } catch (const std::exception &e) {
            next.error = e.what();
        }
        bool deliver = false;
        {
            std::lock_guard lock(mutex_);
            if (!closing_ && generation == generation_) {
                next.seeking = request != request_;
                next.version = published_.version + 1;
                published_ = std::move(next);
                deliver = true;
            }
        }
        if (deliver && notify_)
            notify_();
    }
}
} // namespace webscene::media
