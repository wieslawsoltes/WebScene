#pragma once
#include "media_decode.h"
#include <functional>

namespace webscene::media {
// Opt-in native application sources bypass the byte/blob admission path. The
// platform player owns incremental file/network I/O and streamed audio output;
// decoded video still uses WebScene's retained image compositor.
class native_stream {
public:
    struct snapshot {
        uint64_t version{};
        double time{}, duration{};
        bool ready{}, seeking{}, playing{}, ended{}, buffering{};
        std::string error;
        video_frame video;
    };
    virtual ~native_stream() = default;
    virtual void control(double rate, bool playing, double volume, bool muted) = 0;
    virtual void seek(double seconds) = 0;
    virtual snapshot read() = 0;
};
std::unique_ptr<native_stream> open_native_stream(std::string url, std::function<void()> notify);
bool native_stream_supported() noexcept;
}
