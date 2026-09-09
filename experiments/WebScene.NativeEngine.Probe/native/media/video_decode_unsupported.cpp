#include "media_decode.h"
#include <stdexcept>
namespace webscene::media {
bool native_video_decode_available() noexcept { return false; }
std::unique_ptr<video_decoder> open_video(const encoded_source&, decode_limits, std::stop_token) {
    throw std::runtime_error("Native video decoding is not implemented on this platform");
}
video_frame decode_video_frame(const encoded_source&, double, decode_limits, std::stop_token) {
    throw std::runtime_error("Native video decoding is not implemented on this platform");
}
}

namespace webscene::media {
audio_buffer decode_native_audio(std::span<const uint8_t>,decode_limits,std::stop_token,uint32_t) { throw std::runtime_error("Unsupported or invalid audio data"); }
}
