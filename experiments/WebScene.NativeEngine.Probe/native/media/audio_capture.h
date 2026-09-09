#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
namespace webscene::media {
// Single realtime producer, independent recorder readers. Bounded stereo PCM
// ring with absolute timestamps. A lagging reader reports an overrun explicitly;
// it never blocks the audio callback or silently presents a discontinuity.
class audio_capture {
    static constexpr uint64_t capacity = 16384;
    struct frame {
        std::atomic<uint64_t> stamp{UINT64_MAX};
        std::atomic<float> left{}, right{};
    };
    std::array<frame, capacity> data_{};
    std::atomic<uint64_t> end_{};
    std::atomic<bool> ended_{};
    uint32_t rate_;

  public:
    struct result {
        uint64_t first_frame{}, frames{}, dropped{};
        bool ended{};
    };
    explicit audio_capture(uint32_t rate) : rate_(rate) {}
    uint32_t sample_rate() const noexcept { return rate_; }
    uint64_t end_frame() const noexcept { return end_.load(std::memory_order_acquire); }
    bool ended() const noexcept { return ended_.load(std::memory_order_acquire); }
    void end() noexcept { ended_.store(true, std::memory_order_release); }
    void write(std::span<const float> stereo) noexcept {
        auto end = end_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < stereo.size() / 2; ++i) {
            auto &f = data_[(end + i) % capacity];
            f.stamp.store(UINT64_MAX, std::memory_order_seq_cst);
            f.left.store(stereo[i * 2], std::memory_order_seq_cst);
            f.right.store(stereo[i * 2 + 1], std::memory_order_seq_cst);
            f.stamp.store(end + i, std::memory_order_seq_cst);
        }
        end_.store(end + stereo.size() / 2, std::memory_order_release);
    }
    result read(uint64_t &cursor, std::span<float> stereo) const noexcept {
        result out;
        auto end = end_frame(), begin = end > capacity ? end - capacity : 0;
        if (cursor < begin) {
            out.dropped = begin - cursor;
            cursor = begin;
        }
        out.first_frame = cursor;
        out.ended = ended();
        auto count = std::min<uint64_t>(stereo.size() / 2, end - cursor);
        for (; out.frames < count; ++out.frames) {
            auto index = cursor + out.frames;
            const auto &f = data_[index % capacity];
            auto before = f.stamp.load(std::memory_order_seq_cst);
            auto left = f.left.load(std::memory_order_seq_cst),
                 right = f.right.load(std::memory_order_seq_cst);
            if (before != index || f.stamp.load(std::memory_order_seq_cst) != index)
                break;
            stereo[out.frames * 2] = left;
            stereo[out.frames * 2 + 1] = right;
        }
        cursor += out.frames;
        return out;
    }
};
// Stopping/cloning affects one consumer only; monitor and sibling tracks keep
// their graph connection. Recorder keeps this lease after JS wrapper collection.
class audio_track {
    std::shared_ptr<audio_capture> source_;
    uint64_t cursor_;
    std::atomic<bool> stopped_{};

  public:
    std::atomic<bool> enabled{true};
    explicit audio_track(std::shared_ptr<audio_capture> source)
        : source_(std::move(source)), cursor_(source_->end_frame()) {}
    std::shared_ptr<audio_track> clone() const {
        auto result = std::make_shared<audio_track>(source_);
        if (ended())
            result->stop();
        result->enabled = enabled.load();
        return result;
    }
    void stop() noexcept { stopped_ = true; }
    bool ended() const noexcept { return stopped_.load() || source_->ended(); }
    uint32_t sample_rate() const noexcept { return source_->sample_rate(); }
    audio_capture::result read(std::span<float> stereo) noexcept {
        if (stopped_)
            return {cursor_, 0, 0, true};
        auto result = source_->read(cursor_, stereo);
        if (!enabled)
            std::fill_n(stereo.begin(), result.frames * 2, 0.f);
        return result;
    }
};
} // namespace webscene::media
