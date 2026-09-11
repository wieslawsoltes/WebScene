#include "native_stream.h"
#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Predicate> void until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Native stream timed out");
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    }
}
}
int main(int argc, char** argv) { @autoreleasepool {
    try {
        check(argc == 2, "Provide a local media path or HTTP(S) URL");
        NSString* input = [NSString stringWithUTF8String:argv[1]];
        NSURL* url = [input containsString:@"://"] ? [NSURL URLWithString:input] : [NSURL fileURLWithPath:input];
        auto stream = webscene::media::open_native_stream(url.absoluteString.UTF8String, []{});
        auto read = [&] { auto state = stream->read(); if (!state.error.empty()) throw std::runtime_error(state.error); return state; };
        until([&] { return read().ready; });
        auto first = read();
        check(first.video.width && first.video.height && first.duration > 1, "Invalid first frame/metadata");
        stream->control(1, true, .25, true);
        until([&] { return read().time > .2; });
        until([&] { return read().video.native_surface != nullptr; });
        first = read();
        stream->control(1, false, .25, true);
        until([&] { return !read().playing; });
        const auto paused = read().time;
        const auto wait = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
        until([&] { return std::chrono::steady_clock::now() >= wait; });
        check(std::abs(read().time - paused) < .06, "Paused clock advanced");
        stream->seek(first.duration * .6);
        until([&] { const auto state = read(); return !state.seeking && std::abs(state.time - first.duration * .6) < .1; });
        stream->seek(.05);
        until([&] { const auto state = read(); return !state.seeking && state.time < .15; });
        stream.reset();
        auto* pixel = static_cast<CVPixelBufferRef>(first.video.native_surface.get());
        check(CVPixelBufferGetWidth(pixel) == first.video.width, "Frame lease did not survive close");
        auto failed = webscene::media::open_native_stream("file:///does-not-exist-webscene.mp4", []{});
        until([&] { return !failed->read().error.empty(); });
        failed.reset();
        for (int i = 0; i < 3; ++i) {
            auto cancelled = webscene::media::open_native_stream(url.absoluteString.UTF8String, []{});
            cancelled.reset();
        }
        std::puts("Native URL media: metadata, playback, pause, seek, error and frame lifetime passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}}
