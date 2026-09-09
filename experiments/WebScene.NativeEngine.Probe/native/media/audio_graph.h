#pragma once
#include "audio_capture.h"
#include "media_decode.h"
#include <atomic>
#include <memory>
#include <span>
namespace webscene::media {
struct playback_control {
    std::atomic<uint64_t> sequence{};
    std::atomic<double> position{}, epoch{}, rate{1}, volume{1};
    std::atomic<bool> playing{}, muted{};
    void set(double time, double speed, bool play, double gain, bool mute);
    double time() const noexcept;
};
class audio_graph {
  public:
    enum class kind { destination, gain, analyser, source, stream };
    explicit audio_graph(bool device_output = true, uint32_t sample_rate = 48000);
    ~audio_graph();
    audio_graph(const audio_graph &) = delete;
    uint32_t create(kind);
    void connect(uint32_t source, uint32_t destination);
    void disconnect(uint32_t source);
    void set_gain(uint32_t, float value, double start, double time_constant);
    void set_source(uint32_t, std::shared_ptr<const audio_buffer>, std::shared_ptr<playback_control>);
    void resume();
    void suspend();
    void close();
    double time() const noexcept;
    uint32_t sample_rate() const noexcept;
    void analyser(uint32_t, std::span<float>) const;
    std::shared_ptr<audio_track> capture(uint32_t);
    // Same quantum renderer used by the device callback and numeric tests.
    // No allocation, locks, JS callbacks or disk I/O on this path.
    void render(float *stereo, uint32_t frames) noexcept;
    void render_at(float *stereo, uint32_t frames, double steady_time) noexcept;

  private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
};
} // namespace webscene::media
