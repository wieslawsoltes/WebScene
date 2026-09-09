#include "media_decode.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <cmath>
#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <unistd.h>

namespace webscene::media {
namespace {
// Private spool file because AVAssetReader consumes file assets. Only admitted
// bytes are written. It is removed on every success/error path after reader use.
struct source_file {
    std::string path;
    source_file(const encoded_source& source, std::stop_token stop) {
        const auto ext = source.extension;
        if (ext != ".mp4" && ext != ".mov" && ext != ".m4v")
            throw std::invalid_argument("Unsupported video container extension");
        std::string pattern = std::string([NSTemporaryDirectory() fileSystemRepresentation]) + "webscene-media-XXXXXX" + ext;
        std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
        int fd = mkstemps(name.data(), static_cast<int>(ext.size()));
        if (fd < 0) throw std::runtime_error("Cannot create private media spool");
        path = name.data();
        size_t offset = 0;
        while (offset < source.bytes.size()) {
            if (stop.stop_requested()) { close(fd); unlink(path.c_str()); throw std::runtime_error("Video decode cancelled"); }
            auto count = write(fd, source.bytes.data() + offset, std::min<size_t>(source.bytes.size() - offset, 1024 * 1024));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { close(fd); unlink(path.c_str()); throw std::runtime_error("Cannot write media spool"); }
            offset += static_cast<size_t>(count);
        }
        if (close(fd) != 0) { unlink(path.c_str()); throw std::runtime_error("Cannot close media spool"); }
    }
    ~source_file() { unlink(path.c_str()); }
};
std::runtime_error failure(NSError* error, const char* fallback) {
    return std::runtime_error(error ? [[error localizedDescription] UTF8String] : fallback);
}
}
bool native_video_decode_available() noexcept { return true; }
video_frame decode_video_frame(const encoded_source& source, double seconds, decode_limits limits, std::stop_token stop) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid video seek time");
    if (source.bytes.empty() || source.bytes.size() > limits.encoded_bytes)
        throw std::invalid_argument("Encoded video size is outside the admitted limit");
    if (stop.stop_requested()) throw std::runtime_error("Video decode cancelled");
    @autoreleasepool {
        source_file file(source, stop);
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:file.path.c_str()]] options:@{AVURLAssetReferenceRestrictionsKey: @(AVAssetReferenceRestrictionForbidAll)}];
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) throw std::runtime_error("Video has no decodable video track");
        const auto size = track.naturalSize;
        if (size.width <= 0 || size.height <= 0 || size.width * size.height > limits.video_pixels)
            throw std::length_error("Video dimensions exceed decode limit");
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) throw failure(error, "Cannot create video reader");
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:@{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
            (id)kCVPixelBufferMetalCompatibilityKey: @YES
        }];
        output.alwaysCopiesSampleData = NO;
        if (![reader canAddOutput:output]) throw std::runtime_error("Video output format is unavailable");
        [reader addOutput:output];
        reader.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(seconds, 600000), kCMTimePositiveInfinity);
        if (![reader startReading]) throw failure(reader.error, "Cannot start video reader");
        // stop_callback may run on another thread; AVAssetReader cancellation
        // interrupts pending reads and never enters V8 or the UI thread.
        std::stop_callback cancel(stop, [reader] { [reader cancelReading]; });
        CMSampleBufferRef sample = [output copyNextSampleBuffer];
        if (!sample) {
            if (stop.stop_requested()) throw std::runtime_error("Video decode cancelled");
            throw failure(reader.error, "No video frame at requested time");
        }
        struct release_sample { CMSampleBufferRef value; ~release_sample() { CFRelease(value); } } release{sample};
        CVPixelBufferRef pixel = CMSampleBufferGetImageBuffer(sample);
        if (!pixel) throw std::runtime_error("Decoded sample has no pixel buffer");
        const auto width = CVPixelBufferGetWidth(pixel), height = CVPixelBufferGetHeight(pixel);
        if (!width || !height || width > limits.video_pixels / height)
            throw std::length_error("Decoded video exceeds pixel limit");
        CVPixelBufferRetain(pixel);
        video_frame result;
        result.native_surface = std::shared_ptr<void>(pixel, [](void* value) { CVPixelBufferRelease(static_cast<CVPixelBufferRef>(value)); });
        const auto transform = track.preferredTransform;
        result.display_transform = {transform.a, transform.b, transform.c, transform.d, transform.tx, transform.ty};
        result.width = static_cast<uint32_t>(width); result.height = static_cast<uint32_t>(height);
        result.pixel_format = CVPixelBufferGetPixelFormatType(pixel);
        result.timestamp = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample));
        result.duration = CMTimeGetSeconds(asset.duration);
        [reader cancelReading];
        if (stop.stop_requested()) throw std::runtime_error("Video decode cancelled");
        return result;
    }
}
}
