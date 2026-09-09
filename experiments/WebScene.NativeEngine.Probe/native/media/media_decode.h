#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace webscene::media {
// Only bytes already admitted by the host resource loader enter a decoder.
// Decoder backends must never fetch an arbitrary application URL themselves.
struct encoded_source {
    std::vector<uint8_t> bytes;
    std::string extension;
};
struct audio_buffer {
    uint32_t channels{}, sample_rate{};
    std::vector<float> samples; // interleaved; immutable once published
    uint64_t frames() const { return channels ? samples.size() / channels : 0; }
};
struct decode_limits {
    uint64_t encoded_bytes = 256ULL * 1024 * 1024;
    uint64_t decoded_audio_bytes = 256ULL * 1024 * 1024;
    uint64_t video_pixels = 64ULL * 1024 * 1024;
};
// A native frame lease, independent of JS wrappers and decoder/session lifetime.
// On macOS native_surface is a retained CVPixelBuffer. No CPU pixels are copied.
enum class surface_kind { cv_pixel_buffer, d3d11_texture, dma_buf, host_pixels };
struct video_frame {
    surface_kind kind{surface_kind::cv_pixel_buffer};
    std::array<double, 6> display_transform{1, 0, 0, 1, 0, 0};
    std::shared_ptr<void> native_surface;
    uint32_t width{}, height{}, pixel_format{};
    double timestamp{}, duration{};
};
class video_decoder {
public:
    virtual ~video_decoder() = default;
    virtual video_frame read(double seconds, std::stop_token = {}) = 0;
    virtual double duration() const noexcept = 0;
    virtual audio_buffer audio(std::stop_token = {}) = 0;
};
std::unique_ptr<video_decoder> open_video(const encoded_source&, decode_limits = {}, std::stop_token = {});
audio_buffer decode_native_audio(std::span<const uint8_t>, decode_limits, std::stop_token, uint32_t target_sample_rate);
audio_buffer decode_audio(std::span<const uint8_t>, decode_limits = {}, std::stop_token = {}, uint32_t target_sample_rate = 0);
video_frame decode_video_frame(const encoded_source&, double seconds, decode_limits = {}, std::stop_token = {});
bool native_video_decode_available() noexcept;
}
