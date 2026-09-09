#include "media_decode.h"
#include <stdexcept>
namespace webscene::media {
bool native_video_decode_available() noexcept { return false; }
video_frame decode_video_frame(const encoded_source&, double, decode_limits, std::stop_token) {
    throw std::runtime_error("Native video decoding is not implemented on this platform");
}
}
