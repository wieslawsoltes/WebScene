#include "native_stream.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace webscene::media {
namespace {
class apple_stream final : public native_stream {
    struct controls {
        uint64_t revision{}, seek_revision{};
        double rate{1}, volume{1}, time{};
        bool playing{}, muted{};
    } controls_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    snapshot published_;
    std::function<void()> notify_;
    std::jthread worker_;

    void publish(snapshot state) {
        bool changed;
        {
            std::lock_guard lock(mutex_);
            changed = state.video.native_surface != published_.video.native_surface ||
                state.ready != published_.ready || state.seeking != published_.seeking ||
                state.error != published_.error || state.ended != published_.ended ||
                state.buffering != published_.buffering;
            state.version = published_.version + (changed ? 1 : 0);
            published_ = std::move(state);
        }
        if (changed && notify_) notify_();
    }
    void run(std::string url, std::stop_token stop) {
        @autoreleasepool {
            NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
            if (!address || !([address.scheme isEqualToString:@"file"] ||
                             [address.scheme isEqualToString:@"http"] ||
                             [address.scheme isEqualToString:@"https"])) {
                snapshot failure; failure.error = "Expected a file, HTTP or HTTPS media URL";
                publish(std::move(failure)); return;
            }
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL:address options:nil];
            AVPlayerItem* item = [AVPlayerItem playerItemWithAsset:asset];
            // This is a look-ahead duration, never a limit on source file size.
            item.preferredForwardBufferDuration = 3;
            item.canUseNetworkResourcesForLiveStreamingWhilePaused = NO;
            AVPlayerItemVideoOutput* output = [[AVPlayerItemVideoOutput alloc]
                initWithPixelBufferAttributes:@{
                    (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                    (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
                    (id)kCVPixelBufferMetalCompatibilityKey: @YES}];
            output.suppressesPlayerRendering = YES;
            [item addOutput:output];
            AVPlayer* player = [AVPlayer playerWithPlayerItem:item];
            player.automaticallyWaitsToMinimizeStalling = YES;
            player.actionAtItemEnd = AVPlayerActionAtItemEndPause;
            uint64_t applied = UINT64_MAX, seek_applied = 0;
            auto completed_seek = std::make_shared<std::atomic<uint64_t>>(0);
            snapshot state;
            bool tracks_checked = false;
            while (!stop.stop_requested()) {
                @autoreleasepool {
                    controls desired;
                    { std::lock_guard lock(mutex_); desired = controls_; }
                    if (desired.revision != applied) {
                        player.volume = static_cast<float>(desired.volume);
                        player.muted = desired.muted;
                        if (desired.playing) [player playImmediatelyAtRate:static_cast<float>(desired.rate)];
                        else [player pause];
                        applied = desired.revision;
                    }
                    if (item.status == AVPlayerItemStatusFailed || player.status == AVPlayerStatusFailed) {
                        NSError* error = item.error ?: player.error;
                        state.error = error ? error.localizedDescription.UTF8String : "Cannot open media source";
                        state.ready = false; publish(state); break;
                    }
                    if (item.status == AVPlayerItemStatusReadyToPlay) {
                        if (!tracks_checked && item.tracks.count) {
                            tracks_checked = true;
                            bool decodable_video = false;
                            for (AVPlayerItemTrack* track in item.tracks)
                                if ([track.assetTrack.mediaType isEqualToString:AVMediaTypeVideo] &&
                                    track.assetTrack.decodable) decodable_video = true;
                            if (!decodable_video) {
                                state.error = "The macOS media stack cannot decode this video's format";
                                publish(state); break;
                            }
                        }
                        const double duration = CMTimeGetSeconds(item.duration);
                        state.duration = std::isfinite(duration) ? duration : INFINITY;
                        if (desired.seek_revision != seek_applied) {
                            seek_applied = desired.seek_revision;
                            const auto revision = seek_applied;
                            auto completion = completed_seek;
                            state.seeking = true;
                            [player seekToTime:CMTimeMakeWithSeconds(desired.time, 600000)
                                toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero
                                completionHandler:^(BOOL finished) {
                                    if (finished) completion->store(revision, std::memory_order_release);
                                }];
                        }
                        state.seeking = seek_applied != completed_seek->load(std::memory_order_acquire);
                        const CMTime time = player.currentTime;
                        const double seconds = CMTimeGetSeconds(time);
                        state.time = std::isfinite(seconds) ? seconds : 0;
                        state.playing = player.timeControlStatus == AVPlayerTimeControlStatusPlaying;
                        state.buffering = player.timeControlStatus == AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate;
                        state.ended = std::isfinite(state.duration) && state.time >= state.duration - 0.001;
                        if ([output hasNewPixelBufferForItemTime:time]) {
                            CMTime display;
                            CVPixelBufferRef pixel = [output copyPixelBufferForItemTime:time itemTimeForDisplay:&display];
                            if (pixel) {
                                const auto width = CVPixelBufferGetWidth(pixel), height = CVPixelBufferGetHeight(pixel);
                                if (!width || !height || width > decode_limits{}.video_pixels / height) {
                                    CVPixelBufferRelease(pixel);
                                    state.error = "Video dimensions exceed the decoded frame budget";
                                    publish(state); break;
                                }
                                state.video.native_surface = std::shared_ptr<void>(pixel, [](void* p) {
                                    CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p));
                                });
                                state.video.width = static_cast<uint32_t>(width);
                                state.video.height = static_cast<uint32_t>(height);
                                state.video.pixel_format = CVPixelBufferGetPixelFormatType(pixel);
                                state.video.timestamp = CMTimeGetSeconds(display);
                                state.video.duration = state.duration;
                            }
                        }
                        // Some files start with audio before their first video
                        // sample. Waiting for a frame at time zero would prevent
                        // JS from starting playback, so publish item readiness.
                        if (!state.video.native_surface) {
                            const auto size = item.presentationSize;
                            if (size.width > 0 && size.height > 0 &&
                                size.width > decode_limits{}.video_pixels / size.height) {
                                state.error = "Video dimensions exceed the decoded frame budget";
                                publish(state); break;
                            }
                            state.video.width = static_cast<uint32_t>(size.width);
                            state.video.height = static_cast<uint32_t>(size.height);
                        }
                        state.ready = state.video.width && state.video.height;
                    }
                    publish(state);
                    std::unique_lock lock(mutex_);
                    // Poll only while loading, seeking or playing. A paused
                    // ready source retains one frame and sleeps until control.
                    const auto interval = !state.ready || state.seeking || desired.playing
                        ? std::chrono::milliseconds(8) : std::chrono::milliseconds(250);
                    wake_.wait_for(lock, stop, interval, [&] { return controls_.revision != applied; });
                }
            }
            [player pause];
            [item cancelPendingSeeks];
            [asset cancelLoading];
            [player replaceCurrentItemWithPlayerItem:nil];
            [item removeOutput:output];
        }
    }
public:
    apple_stream(std::string url, std::function<void()> notify) : notify_(std::move(notify)),
        worker_([this, url = std::move(url)](std::stop_token stop) { run(url, stop); }) {}
    ~apple_stream() override { worker_.request_stop(); wake_.notify_all(); if (worker_.joinable()) worker_.join(); }
    void control(double rate, bool playing, double volume, bool muted) override {
        { std::lock_guard lock(mutex_);
          controls_.rate = rate; controls_.playing = playing; controls_.volume = volume;
          controls_.muted = muted; ++controls_.revision; }
        wake_.notify_all();
    }
    void seek(double seconds) override {
        if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid native media seek");
        { std::lock_guard lock(mutex_);
          controls_.time = seconds; ++controls_.seek_revision; ++controls_.revision; }
        wake_.notify_all();
    }
    snapshot read() override { std::lock_guard lock(mutex_); return published_; }
};
}
bool native_stream_supported() noexcept { return true; }
std::unique_ptr<native_stream> open_native_stream(std::string url, std::function<void()> notify) {
    return std::make_unique<apple_stream>(std::move(url), std::move(notify));
}
}
