#include "audio_graph.h"
#include "decode_service.h"
#include "media_session.h"
#include <thread>
#if defined(__APPLE__)
#include "../native/graphics/iosurface_canvas_images.h"
#endif
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif
using namespace webscene::media;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F function) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Expected decode rejection");
}
std::vector<uint8_t> wav() {
    std::vector<uint8_t> data(44 + 480 * 4);
    auto word = [&](size_t i, uint16_t v) {
        data[i] = v;
        data[i + 1] = v >> 8;
    };
    auto dword = [&](size_t i, uint32_t v) {
        word(i, v);
        word(i + 2, v >> 16);
    };
    memcpy(data.data(), "RIFF", 4);
    dword(4, data.size() - 8);
    memcpy(data.data() + 8, "WAVEfmt ", 8);
    dword(16, 16);
    word(20, 1);
    word(22, 2);
    dword(24, 48000);
    dword(28, 192000);
    word(32, 4);
    word(34, 16);
    memcpy(data.data() + 36, "data", 4);
    dword(40, data.size() - 44);
    for (size_t i = 0; i < 480; ++i) {
        word(44 + i * 4, 16384);
        word(46 + i * 4, static_cast<uint16_t>(-8192));
    }
    return data;
}
std::shared_ptr<encoded_source> load(const char *path, const char *extension) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Fixture missing");
    auto source = std::make_shared<encoded_source>();
    source->extension = extension;
    source->bytes.assign(std::istreambuf_iterator<char>(file), {});
    return source;
}
int main(int argc, char **argv) {
    try {
        auto bytes = wav();
        auto result = decode_audio(bytes);
        check(result.channels == 2 && result.sample_rate == 48000 && result.frames() == 480,
              "WAV format/frame count");
        for (size_t i = 0; i < result.frames(); ++i) {
            check(std::abs(result.samples[i * 2] - .5f) < 1e-6, "Left channel wrong");
            check(std::abs(result.samples[i * 2 + 1] + .25f) < 1e-6, "Right channel wrong");
        }
        rejects([&] { decode_audio({}); });
        rejects([&] { decode_audio(std::vector<uint8_t>{1, 2, 3}); });
        decode_limits small;
        small.decoded_audio_bytes = 16;
        rejects([&] { decode_audio(bytes, small); });
        small = {};
        small.encoded_bytes = 4;
        rejects([&] { decode_audio(bytes, small); });
        std::stop_source cancelled;
        cancelled.request_stop();
        rejects([&] { decode_audio(bytes, {}, cancelled.get_token()); });
        std::future<audio_buffer> future;
        {
            decode_service service;
            auto source = std::make_shared<encoded_source>();
            source->bytes = bytes;
            future = service.audio(source);
            check(future.get().frames() == 480, "Asynchronous decode failed");
            service.close();
            rejects([&] { service.audio(source); });
        }
        // Service destruction settles pending work instead of stranding futures.
        {
            decode_service service;
            auto source = std::make_shared<encoded_source>();
            source->bytes = bytes;
            future = service.audio(source);
        }
        check(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready,
              "Future stranded by teardown");
        try {
            future.get();
        } catch (const std::exception &) {
        }
        {
            media_session session;
            auto source = std::make_shared<encoded_source>();
            source->bytes = bytes;
            session.load(source, false);
            auto ready = [&] {
                for (int i = 0; i < 500; ++i) {
                    auto frame = session.read();
                    if (frame.ready && !frame.seeking)
                        return frame;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                throw std::runtime_error("Media session timeout");
            };
            check(ready().audio->frames() == 480, "Session PCM");
            for (int i = 0; i < 100; ++i)
                session.seek(i * .00001);
            check(ready().ready, "Coalesced seek");
            auto replacement = std::make_shared<encoded_source>();
            replacement->bytes = bytes;
            session.load(replacement, false);
            check(ready().generation == 2, "Stale media generation");
            session.close();
            rejects([&] { session.seek(0); });
        }
        if (argc > 1 && std::string(argv[1]) != "-") {
            auto source = load(argv[1], ".wav");
            decode_service service;
            auto audio = service.audio(source).get();
            check(audio.frames() > audio.sample_rate, "Demo score too short");
            double energy = 0;
            for (float x : audio.samples) {
                check(std::isfinite(x), "Nonfinite demo audio");
                energy += x * x;
            }
            check(energy > 1, "Silent demo audio");
            std::cout << "Original score: " << audio.frames() << " frames, " << audio.sample_rate << " Hz, "
                      << audio.channels << " channels\n";
        }
#if defined(__APPLE__)
        if (argc > 2) {
            auto source = load(argv[2], ".mp4");
            video_frame first, later;
            {
                decode_service service;
                first = service.video(source, 0).get();
                later = service.video(source, 1).get();
            }
            check(first.native_surface && later.native_surface && first.width && first.height,
                  "No native video surface");
            check(later.timestamp >= .95 && later.timestamp > first.timestamp,
                  "Seek did not advance decoded frame");
            auto checksum = [](const video_frame &f) {
                auto pixel = static_cast<CVPixelBufferRef>(f.native_surface.get());
                CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
                auto p = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(pixel));
                uint64_t hash = 1469598103934665603ULL;
                for (size_t y = 0; y < f.height; ++y)
                    for (size_t x = 0; x < f.width * 4; ++x)
                        hash = (hash ^ p[y * CVPixelBufferGetBytesPerRow(pixel) + x]) * 1099511628211ULL;
                CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
                return hash;
            };
            auto decoder = open_video(*source);
            auto end = decoder->read(decoder->duration());
            check(end.native_surface && end.timestamp > 0, "End seek failed");
            auto back = decoder->read(0);
            check(back.timestamp < .1, "Backward seek failed");
            using namespace webscene::graphics;
            std::optional<owned_image_pool::consumer> consumer;
            std::weak_ptr<void> lifetime;
            {
                iosurface_canvas_images pool(128ULL * 1024 * 1024);
                auto decoded = decoder->read(.5);
                lifetime = decoded.native_surface;
                auto color = iosurface_color::adopt_bgra8(
                    static_cast<CVPixelBufferRef>(decoded.native_surface.get()), decoded.native_surface);
                image_metadata metadata;
                metadata.canvas = 1;
                metadata.producer_timeline = 1;
                metadata.producer_value = 1;
                metadata.allocation_generation = 1;
                metadata.content_serial = 1;
                metadata.width = decoded.width;
                metadata.height = decoded.height;
                metadata.format = image_format::bgra8_unorm;
                auto image = pool.adopt(metadata, std::move(color));
                check(bool(image), "No adopted decoder image");
                consumer.emplace(image->begin_consumer().value());
                decoder.reset();
                decoded = {};
                image.reset();
                check(!lifetime.expired(), "Decoder frame recycled before consumer completion");
                check(iosurface_canvas_images::resolve(*consumer).borrowed_handle() != nullptr,
                      "Adopted surface missing");
            }
            {
                struct wake final : completion_wake {
                    int count{};
                    void signal() noexcept override { ++count; }
                };
                auto signal = std::make_shared<wake>();
                iosurface_canvas_images pool(128ULL * 1024 * 1024, signal);
                auto color = iosurface_color::adopt_bgra8(
                    static_cast<CVPixelBufferRef>(first.native_surface.get()), first.native_surface);
                image_metadata m{1, 0, 1, 1, 1, 1, first.width, first.height, image_format::bgra8_unorm};
                std::vector<owned_image_pool::retained> held;
                for (int i = 0; i < 3; ++i) {
                    ++m.content_serial;
                    held.push_back(pool.adopt(m, color).value());
                }
                auto consuming = held.back().begin_consumer();
                held.pop_back();
                check(!pool.adopt(m, color), "Reused surface before GPU completion");
                auto before = signal->count;
                consuming->complete();
                consuming.reset();
                check(signal->count > before, "GPU completion did not wake blocked image publication");
                check(bool(pool.adopt(m, color)), "Latest frame could not publish after backpressure");
            }
            check(!lifetime.expired(), "Pool destruction released a live GPU frame");
            consumer->complete();
            consumer.reset();
            check(lifetime.expired(), "Completed GPU frame leaked");
            check(checksum(first) != checksum(later), "Video frames did not change");
            check(CVPixelBufferGetIOSurface(static_cast<CVPixelBufferRef>(first.native_surface.get())) !=
                      nullptr,
                  "Missing shareable IOSurface");
            rejects([&] { decode_video_frame(*source, -1); });
            rejects([&] { decode_video_frame(*source, 0, {}, cancelled.get_token()); });
            std::cout << "Original video: " << first.width << "x" << first.height << ", timestamps "
                      << first.timestamp << " and " << later.timestamp
                      << ", retained IOSurface frames survive decoder teardown\n";
        }
#endif
#if defined(__APPLE__)
        if (argc > 3) {
            auto source = load(argv[3], ".mp4");
            auto decoder = open_video(*source);
            auto pcm = std::make_shared<audio_buffer>(decoder->audio());
            check(pcm->sample_rate == 48000 && pcm->frames() >= 144000, "Flash/click audio decode");
            audio_graph graph(false);
            auto input = graph.create(audio_graph::kind::source),
                 bus = graph.create(audio_graph::kind::stream);
            auto track = graph.capture(bus);
            auto control = std::make_shared<playback_control>();
            control->set(0, 1, true, 1, false);
            control->epoch = 0;
            graph.set_source(input, pcm, control);
            graph.connect(input, bus);
            graph.resume();
            std::vector<float> mix(144000 * 2), packet(256);
            double maximum = 0;
            for (uint32_t offset = 0; offset < 144000; offset += 128) {
                auto size = std::min(128U, 144000 - offset);
                graph.render_at(mix.data() + offset * 2, size, double(offset) / 48000);
                auto result = track->read(std::span<float>(packet.data(), size * 2));
                check(result.frames == size && result.first_frame == offset && !result.dropped,
                      "Recording time continuity");
                for (uint32_t i = 0; i < size; ++i)
                    mix[(offset + i) * 2] = packet[i * 2];
            }
            for (int second = 0; second < 3; ++second) {
                auto frame = decoder->read(second);
                auto pixel = static_cast<CVPixelBufferRef>(frame.native_surface.get());
                CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
                auto value = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(pixel))[0];
                CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
                check(value > 200, "Missing numbered flash");
                uint32_t onset = second * 48000;
                while (onset < static_cast<uint32_t>((second + .05) * 48000) &&
                       std::abs(mix[onset * 2]) < .1f)
                    ++onset;
                double drift = std::abs(double(onset) / 48000 - frame.timestamp);
                maximum = std::max(maximum, drift);
                check(drift < .01, "Decoded video / recorded audio drift exceeds 10ms");
            }
            std::cout << "Flash/click decoded presentation versus recorded PCM max drift: " << maximum * 1000
                      << " ms (10 ms limit; excludes physical display/device latency)\n";
        }
#endif
        std::cout << "Native media decode tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
