#include "media_decode.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unistd.h>

namespace webscene::media {
namespace {
// Private spool file because AVAssetReader consumes file assets. Only admitted
// bytes are written. It is removed on every success/error path after reader use.
struct source_file {
    std::string path;
    source_file(const encoded_source &source, std::stop_token stop) {
        const auto ext = source.extension;
        if (ext != ".mp4" && ext != ".mov" && ext != ".m4v")
            throw std::invalid_argument("Unsupported video container extension");
        std::string pattern =
            std::string([NSTemporaryDirectory() fileSystemRepresentation]) + "webscene-media-XXXXXX" + ext;
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back(0);
        int fd = mkstemps(name.data(), static_cast<int>(ext.size()));
        if (fd < 0)
            throw std::runtime_error("Cannot create private media spool");
        path = name.data();
        size_t offset = 0;
        while (offset < source.bytes.size()) {
            if (stop.stop_requested()) {
                close(fd);
                unlink(path.c_str());
                throw std::runtime_error("Video decode cancelled");
            }
            auto count = write(fd, source.bytes.data() + offset,
                               std::min<size_t>(source.bytes.size() - offset, 1024 * 1024));
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                close(fd);
                unlink(path.c_str());
                throw std::runtime_error("Cannot write media spool");
            }
            offset += static_cast<size_t>(count);
        }
        if (close(fd) != 0) {
            unlink(path.c_str());
            throw std::runtime_error("Cannot close media spool");
        }
    }
    ~source_file() { unlink(path.c_str()); }
};
std::runtime_error failure(NSError *error, const char *fallback) {
    return std::runtime_error(error ? [[error localizedDescription] UTF8String] : fallback);
}
audio_buffer read_audio(AVAsset *asset, decode_limits limits, std::stop_token stop, uint32_t rate) {
    auto *track = [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject];
    if (!track)
        return {};
    if (stop.stop_requested())
        throw std::runtime_error("Audio decode cancelled");
    NSError *error = nil;
    auto *reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    if (!reader)
        throw failure(error, "Cannot create audio reader");
    auto *settings = [@{
        AVFormatIDKey : @(kAudioFormatLinearPCM),
        AVLinearPCMBitDepthKey : @32,
        AVLinearPCMIsFloatKey : @YES,
        AVLinearPCMIsNonInterleaved : @NO
    } mutableCopy];
    if (rate)
        settings[AVSampleRateKey] = @(rate);
    auto *output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
    output.alwaysCopiesSampleData = NO;
    if (![reader canAddOutput:output])
        throw std::runtime_error("Audio output format unavailable");
    [reader addOutput:output];
    std::stop_callback cancel(stop, [reader] { [reader cancelReading]; });
    if (![reader startReading])
        throw failure(reader.error, "Cannot start audio reader");
    audio_buffer result;
    for (;;) {
        if (stop.stop_requested())
            throw std::runtime_error("Audio decode cancelled");
        auto sample = [output copyNextSampleBuffer];
        if (!sample) {
            if (reader.status != AVAssetReaderStatusCompleted)
                throw failure(reader.error, "Audio decode failed");
            break;
        }
        struct release {
            CMSampleBufferRef sample;
            ~release() { CFRelease(sample); }
        } owner{sample};
        auto format =
            CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample));
        if (!format || format->mFormatID != kAudioFormatLinearPCM || format->mBitsPerChannel != 32 ||
            !(format->mFormatFlags & kAudioFormatFlagIsFloat) ||
            (format->mFormatFlags & kAudioFormatFlagIsNonInterleaved) || !format->mChannelsPerFrame ||
            format->mChannelsPerFrame > 32)
            throw std::runtime_error("Invalid PCM output format");
        if (result.channels &&
            (result.channels != format->mChannelsPerFrame || result.sample_rate != format->mSampleRate))
            throw std::runtime_error("Midstream audio format change unsupported");
        result.channels = format->mChannelsPerFrame;
        result.sample_rate = static_cast<uint32_t>(format->mSampleRate);
        auto block = CMSampleBufferGetDataBuffer(sample);
        if (!block)
            throw std::runtime_error("Missing PCM buffer");
        auto bytes = CMBlockBufferGetDataLength(block);
        if (bytes % sizeof(float) || bytes > limits.decoded_audio_bytes ||
            result.samples.size() > (limits.decoded_audio_bytes - bytes) / sizeof(float))
            throw std::length_error("Decoded audio exceeds memory limit");
        auto offset = result.samples.size();
        result.samples.resize(offset + bytes / sizeof(float));
        if (CMBlockBufferCopyDataBytes(block, 0, bytes, result.samples.data() + offset) !=
            kCMBlockBufferNoErr)
            throw std::runtime_error("Cannot read PCM output");
    }
    return result;
}

}
class apple_video_decoder final : public video_decoder {
    std::unique_ptr<source_file> file_;
    AVURLAsset *asset_;
    AVAssetTrack *track_;
    AVAssetReader *reader_;
    AVAssetReaderTrackOutput *output_;
    decode_limits limits_;
    video_frame last_;
    double requested_{-1}, duration_{};
    void restart(double seconds) {
        [reader_ cancelReading];
        NSError *error = nil;
        reader_ = [[AVAssetReader alloc] initWithAsset:asset_ error:&error];
        if (!reader_)
            throw failure(error, "Cannot create video reader");
        output_ = [[AVAssetReaderTrackOutput alloc]
             initWithTrack:track_
            outputSettings:@{
                (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
                (id)kCVPixelBufferMetalCompatibilityKey : @YES
            }];
        output_.alwaysCopiesSampleData = NO;
        if (![reader_ canAddOutput:output_])
            throw std::runtime_error("Video output format is unavailable");
        [reader_ addOutput:output_];
        reader_.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(seconds, 600000), kCMTimePositiveInfinity);
        if (![reader_ startReading])
            throw failure(reader_.error, "Cannot start video reader");
        last_ = {};
    }

  public:
    apple_video_decoder(const encoded_source &source, decode_limits limits, std::stop_token stop)
        : limits_(limits) {
        if (source.bytes.empty() || source.bytes.size() > limits.encoded_bytes)
            throw std::invalid_argument("Encoded video size is outside the admitted limit");
        if (stop.stop_requested())
            throw std::runtime_error("Video decode cancelled");
        file_ = std::make_unique<source_file>(source, stop);
        asset_ = [AVURLAsset
            URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:file_->path.c_str()]]
                    options:@{
                        AVURLAssetReferenceRestrictionsKey : @(AVAssetReferenceRestrictionForbidAll)
                    }];
        track_ = [[asset_ tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track_)
            throw std::runtime_error("Video has no decodable video track");
        const auto size = track_.naturalSize;
        if (size.width <= 0 || size.height <= 0 || size.width * size.height > limits.video_pixels)
            throw std::length_error("Video dimensions exceed decode limit");
        duration_ = CMTimeGetSeconds(asset_.duration);
        if (!std::isfinite(duration_) || duration_ <= 0)
            throw std::runtime_error("Invalid video duration");
    }
    ~apple_video_decoder() override { [reader_ cancelReading]; }
    double duration() const noexcept override { return duration_; }
    audio_buffer audio(std::stop_token stop) override {
        @autoreleasepool {
            return read_audio(asset_, limits_, stop, 0);
        }
    }
    video_frame read(double seconds, std::stop_token stop) override {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::invalid_argument("Invalid video seek time");
        if (stop.stop_requested())
            throw std::runtime_error("Video decode cancelled");
        @autoreleasepool {
            // Reuse one asset/spool and reader during sequential playback. Only
            // discontinuities reopen a reader. Keep at most the current lease.
            seconds = std::min(
                seconds, std::max(0.0, duration_ - 1.0 / std::max(1.0, double(track_.nominalFrameRate))));
            if (!reader_ || seconds < requested_ || seconds - requested_ > .5) {
                if (std::getenv("WEBSCENE_MEDIA_TRACE"))
                    std::fprintf(stderr, "media_restart requested=%.6f previous=%.6f\n", seconds, requested_);
                restart(seconds);
            }
            requested_ = seconds;
            AVAssetReader *active_reader = reader_;
            std::stop_callback cancel(stop, [active_reader] { [active_reader cancelReading]; });
            while (!last_.native_surface || last_.timestamp + 1e-7 < seconds) {
                CMSampleBufferRef sample = [output_ copyNextSampleBuffer];
                if (!sample) {
                    if (stop.stop_requested())
                        throw std::runtime_error("Video decode cancelled");
                    if (reader_.status == AVAssetReaderStatusCompleted && last_.native_surface)
                        break;
                    throw failure(reader_.error, "No video frame at requested time");
                }
                struct release_sample {
                    CMSampleBufferRef value;
                    ~release_sample() { CFRelease(value); }
                } release{sample};
                CVPixelBufferRef pixel = CMSampleBufferGetImageBuffer(sample);
                if (!pixel)
                    throw std::runtime_error("Decoded sample has no pixel buffer");
                const auto width = CVPixelBufferGetWidth(pixel), height = CVPixelBufferGetHeight(pixel);
                if (!width || !height || width > limits_.video_pixels / height)
                    throw std::length_error("Decoded video exceeds pixel limit");
                CVPixelBufferRetain(pixel);
                video_frame frame;
                frame.native_surface = std::shared_ptr<void>(
                    pixel, [](void *value) { CVPixelBufferRelease(static_cast<CVPixelBufferRef>(value)); });
                const auto transform = track_.preferredTransform;
                frame.display_transform = {transform.a, transform.b,  transform.c,
                                           transform.d, transform.tx, transform.ty};
                frame.width = static_cast<uint32_t>(width);
                frame.height = static_cast<uint32_t>(height);
                frame.pixel_format = CVPixelBufferGetPixelFormatType(pixel);
                frame.timestamp = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample));
                frame.duration = duration_;
                frame.sample_duration = CMTimeGetSeconds(CMSampleBufferGetDuration(sample));
                if (!std::isfinite(frame.sample_duration) || frame.sample_duration <= 0)
                    frame.sample_duration = 1.0 / std::max(1.0, double(track_.nominalFrameRate));
                last_ = std::move(frame);
            }
            if (stop.stop_requested())
                throw std::runtime_error("Video decode cancelled");
            return last_;
        }
    }
};
bool native_video_decode_available() noexcept { return true; }
std::unique_ptr<video_decoder> open_video(const encoded_source &source, decode_limits limits,
                                          std::stop_token stop) {
    @autoreleasepool {
        return std::make_unique<apple_video_decoder>(source, limits, stop);
    }
}
video_frame decode_video_frame(const encoded_source &source, double seconds, decode_limits limits,
                               std::stop_token stop) {
    return open_video(source, limits, stop)->read(seconds, stop);
}
audio_buffer decode_native_audio(std::span<const uint8_t> bytes, decode_limits limits, std::stop_token stop,
                                 uint32_t rate) {
    @autoreleasepool {
        encoded_source source{{bytes.begin(), bytes.end()}, ".mp4"};
        source_file file(source, stop);
        auto *asset = [AVURLAsset
            URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:file.path.c_str()]]
                    options:@{
                        AVURLAssetReferenceRestrictionsKey : @(AVAssetReferenceRestrictionForbidAll)
                    }];
        auto result = read_audio(asset, limits, stop, rate);
        if (result.samples.empty())
            throw std::runtime_error("Unsupported or invalid audio data");
        return result;
    }
}

}
