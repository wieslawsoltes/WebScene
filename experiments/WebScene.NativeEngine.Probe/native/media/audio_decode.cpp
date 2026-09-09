#include "media_decode.h"
#include "miniaudio.h"
#include <algorithm>
#include <stdexcept>

namespace webscene::media {
audio_buffer decode_audio(std::span<const uint8_t> bytes, decode_limits limits, std::stop_token stop) {
    if (bytes.empty() || bytes.size() > limits.encoded_bytes)
        throw std::invalid_argument("Encoded audio size is outside the admitted limit");
    if (stop.stop_requested()) throw std::runtime_error("Audio decode cancelled");
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder{};
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS)
        throw std::runtime_error("Unsupported or invalid audio data");
    struct cleanup { ma_decoder* value; ~cleanup() { ma_decoder_uninit(value); } } guard{&decoder};
    audio_buffer result{decoder.outputChannels, decoder.outputSampleRate, {}};
    if (!result.channels || result.channels > 32 || !result.sample_rate)
        throw std::runtime_error("Invalid audio channel count or sample rate");
    const uint64_t maximum_samples = limits.decoded_audio_bytes / sizeof(float);
    // Decode in bounded chunks; never trust an encoded file's declared length.
    std::vector<float> chunk(4096 * result.channels);
    for (;;) {
        if (stop.stop_requested()) throw std::runtime_error("Audio decode cancelled");
        ma_uint64 frames = 0;
        const auto status = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &frames);
        if (status != MA_SUCCESS && status != MA_AT_END)
            throw std::runtime_error("Audio decoder failed");
        const uint64_t count = frames * result.channels;
        if (count > maximum_samples || result.samples.size() > maximum_samples - count)
            throw std::length_error("Decoded audio exceeds memory limit");
        result.samples.insert(result.samples.end(), chunk.data(), chunk.data() + count);
        if (status == MA_AT_END || frames == 0) break;
    }
    if (result.samples.empty()) throw std::runtime_error("Audio contains no decoded frames");
    return result;
}
}
