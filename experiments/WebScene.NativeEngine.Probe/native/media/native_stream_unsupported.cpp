#include "native_stream.h"
#include <stdexcept>
namespace webscene::media {
bool native_stream_supported() noexcept { return false; }
std::unique_ptr<native_stream> open_native_stream(std::string, std::function<void()>) {
    throw std::runtime_error("Native URL media streaming is not available on this platform");
}
}
