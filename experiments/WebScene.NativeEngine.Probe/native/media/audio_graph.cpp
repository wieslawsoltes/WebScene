#include "audio_graph.h"
#include "miniaudio.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace webscene::media {
namespace {
double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr uint32_t nodes = 64, quantum = 128, history = 32768, capacity = 1024;
struct command {
    enum class operation { create, connect, disconnect, gain, source } op;
    uint32_t a{}, b{};
    double value{}, start{}, tau{};
    const audio_buffer *pcm{};
    playback_control *control{};
};
} // namespace
void playback_control::set(double time, double speed, bool play, double gain, bool mute) {
    sequence.fetch_add(1, std::memory_order_acq_rel);
    position = time;
    epoch = now();
    rate = speed;
    volume = gain;
    muted = mute;
    playing = play;
    sequence.fetch_add(1, std::memory_order_release);
}
double playback_control::time() const noexcept {
    return time_at(now());
}
double playback_control::time_at(double steady_seconds) const noexcept {
    const double p = position.load();
    if (!playing.load()) return p;
    const auto revision = sequence.load(std::memory_order_acquire);
    const auto sample = output_sequence.load(std::memory_order_acquire);
    const auto sample_revision = output_revision.load(std::memory_order_relaxed);
    const auto sample_position = output_position.load(std::memory_order_relaxed);
    const auto sample_epoch = output_epoch.load(std::memory_order_relaxed);
    if (!(sample & 1) && sample_revision == revision &&
        sample == output_sequence.load(std::memory_order_acquire) &&
        std::abs(steady_seconds - sample_epoch) < .25) {
        return sample_position + (steady_seconds - sample_epoch) * rate.load();
    }
    return p + (steady_seconds - epoch.load()) * rate.load();
}
void playback_control::observe_output(uint64_t revision, double media_seconds, double host_seconds) noexcept {
    output_sequence.fetch_add(1, std::memory_order_acq_rel);
    output_position.store(media_seconds, std::memory_order_relaxed);
    output_epoch.store(host_seconds, std::memory_order_relaxed);
    output_revision.store(revision, std::memory_order_relaxed);
    output_sequence.fetch_add(1, std::memory_order_release);
}
struct audio_graph::implementation {
    struct node {
        kind type{kind::gain};
        bool edges[nodes]{};
        float gain{1}, target{1};
        double tau{}, pole{};
        std::atomic<uint32_t> scheduled{};
        struct automation {
            float value;
            double start, tau;
        };
        std::array<automation, 32> events{};
        uint32_t event_count{};
        const audio_buffer *pcm{};
        playback_control *control{};
        std::shared_ptr<audio_capture> capture;
        uint64_t control_version{UINT64_MAX};
        double cursor{};
        std::array<float, quantum * 2> data{};
        std::array<std::atomic<float>, history> samples{};
        std::atomic<uint64_t> written{};
    };
    std::array<node, nodes> graph{};
    std::array<kind, nodes> owner_types{};
    bool owner_edges[nodes][nodes]{};
    uint32_t count{1}, rate;
    std::atomic<uint32_t> rt_count{1};
    std::array<command, capacity> queue{};
    std::atomic<uint32_t> head{}, tail{};
    std::atomic<uint64_t> frames{};
    std::atomic<bool> running{};
    bool use_device, initialized{}, closed{};
    ma_device device{};
    std::vector<std::shared_ptr<const audio_buffer>> keep_pcm;
    std::vector<std::shared_ptr<playback_control>> keep_controls;
    uint64_t pcm_bytes{};
    implementation(bool output, uint32_t sample_rate) : rate(sample_rate), use_device(output) {
        graph[0].type = kind::destination;
        owner_types[0] = kind::destination;
    }
    void push(command c) {
        if (closed)
            throw std::runtime_error("Audio context closed");
        auto h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= capacity)
            throw std::length_error("Audio command queue full");
        queue[h % capacity] = c;
        head.store(h + 1, std::memory_order_release);
    }
    void check(uint32_t id) const {
        if (id >= count)
            throw std::invalid_argument("Invalid audio node");
    }
    void commands() noexcept {
        auto t = tail.load(std::memory_order_relaxed);
        auto h = head.load(std::memory_order_acquire);
        while (t != h) {
            const auto c = queue[t++ % capacity];
            auto &n = graph[c.a];
            switch (c.op) {
            case command::operation::create:
                n.type = static_cast<kind>(c.b);
                rt_count.store(std::max(rt_count.load(), c.a + 1));
                break;
            case command::operation::connect:
                graph[c.b].edges[c.a] = true;
                break;
            case command::operation::disconnect:
                for (auto &d : graph)
                    d.edges[c.a] = false;
                break;
            case command::operation::gain: {
                auto index = n.event_count++;
                n.events[index] = {static_cast<float>(c.value), c.start, c.tau};
                while (index && n.events[index].start < n.events[index - 1].start) {
                    std::swap(n.events[index], n.events[index - 1]);
                    --index;
                }
                break;
            }
            case command::operation::source:
                n.pcm = c.pcm;
                n.control = c.control;
                break;
            }
        }
        tail.store(t, std::memory_order_release);
    }
    void process(uint32_t id, uint32_t size, std::array<bool, nodes> &done, double wall,
                 double clock) noexcept {
        if (done[id])
            return;
        done[id] = true;
        auto &n = graph[id];
        std::fill_n(n.data.data(), size * 2, 0.f);
        if (n.type == kind::source && n.pcm && n.control) {
            auto &c = *n.control;
            auto version = c.sequence.load(std::memory_order_acquire);
            double position = c.position.load(), epoch = c.epoch.load(), speed = c.rate.load(),
                   volume = c.volume.load();
            bool play = c.playing.load(), mute = c.muted.load();
            if (!(version & 1) && version == c.sequence.load(std::memory_order_acquire) && play && !mute &&
                speed > 0) {
                const auto &p = *n.pcm;
                if (n.control_version != version) {
                    n.cursor = (position + (wall - epoch) * speed) * p.sample_rate;
                    n.control_version = version;
                }
                double start = n.cursor;
                // Anchor video to the device-rendered sample cursor instead of
                // an independently advancing wall clock. This is callback-time
                // feedback; backend DAC latency is not yet measured here.
                if (use_device)
                    c.observe_output(version, start / p.sample_rate, wall);
                for (uint32_t i = 0; i < size; ++i) {
                    double source = start + double(i) * speed * p.sample_rate / rate;
                    if (source < 0 || source >= p.frames())
                        continue;
                    auto a = static_cast<uint64_t>(source), b = std::min(a + 1, p.frames() - 1);
                    float f = static_cast<float>(source - a);
                    auto sample = [&](uint32_t channel) {
                        float x = p.samples[a * p.channels + channel],
                              y = p.samples[b * p.channels + channel];
                        return x + (y - x) * f;
                    };
                    float left = sample(0), right = sample(std::min(1U, p.channels - 1));
                    // Web Audio speaker downmix: quad L/R/surrounds and 5.1
                    // L/R/C/LFE/SL/SR. LFE is not folded into stereo.
                    if (p.channels == 4) {
                        left = (left + sample(2)) * .5f;
                        right = (right + sample(3)) * .5f;
                    } else if (p.channels == 6) {
                        left += .7071067811865475f * (sample(2) + sample(4));
                        right += .7071067811865475f * (sample(2) + sample(5));
                    }
                    n.data[i * 2] = left * volume;
                    n.data[i * 2 + 1] = right * volume;
                }
                n.cursor += double(size) * speed * p.sample_rate / rate;
            }
        }
        for (uint32_t input = 0; input < rt_count.load(); ++input)
            if (n.edges[input]) {
                process(input, size, done, wall, clock);
                for (uint32_t i = 0; i < size * 2; ++i)
                    n.data[i] += graph[input].data[i];
            }
        if (n.type == kind::gain)
            for (uint32_t i = 0; i < size; ++i) {
                double t = clock + double(i) / rate;
                while (n.event_count && n.events[0].start <= t) {
                    auto e = n.events[0];
                    for (uint32_t j = 1; j < n.event_count; ++j)
                        n.events[j - 1] = n.events[j];
                    --n.event_count;
                    n.scheduled.fetch_sub(1, std::memory_order_release);
                    n.target = e.value;
                    n.tau = e.tau;
                    n.pole = n.tau ? std::exp(-1. / (n.tau * rate)) : 0;
                    if (!n.tau)
                        n.gain = n.target;
                }
                n.data[i * 2] *= n.gain;
                n.data[i * 2 + 1] *= n.gain;
                if (n.tau)
                    n.gain = n.target + (n.gain - n.target) * n.pole;
            }
        if (n.capture)
            n.capture->write(std::span<const float>(n.data.data(), size * 2));
        if (n.type == kind::analyser || n.type == kind::stream) {
            auto written = n.written.load(std::memory_order_relaxed);
            for (uint32_t i = 0; i < size; ++i)
                n.samples[(written + i) % history].store((n.data[i * 2] + n.data[i * 2 + 1]) * .5f,
                                                         std::memory_order_relaxed);
            n.written.store(written + size, std::memory_order_release);
        }
    }
};
audio_graph::audio_graph(bool device, uint32_t rate) : impl_(std::make_unique<implementation>(device, rate)) {
    if (rate < 8000 || rate > 192000)
        throw std::invalid_argument("Audio sample rate out of range");
}
audio_graph::~audio_graph() { close(); }
uint32_t audio_graph::create(kind type) {
    auto &p = *impl_;
    if (p.count >= nodes)
        throw std::length_error("Audio node limit reached");
    auto id = p.count;
    if (type == kind::stream)
        p.graph[id].capture = std::make_shared<audio_capture>(p.rate);
    p.push({command::operation::create, id, static_cast<uint32_t>(type)});
    p.owner_types[id] = type;
    ++p.count;
    return id;
}
void audio_graph::connect(uint32_t source, uint32_t dest) {
    auto &p = *impl_;
    p.check(source);
    p.check(dest);
    if (p.owner_types[source] == kind::destination || p.owner_types[source] == kind::stream || p.owner_types[dest] == kind::source)
        throw std::invalid_argument("Audio node has no such input/output port");
    if (source == dest)
        throw std::invalid_argument("Unsupported zero-delay audio cycle");
    std::array<bool, nodes> visited{};
    auto reaches = [&](auto &&self, uint32_t x) -> bool {
        if (x == source)
            return true;
        if (visited[x])
            return false;
        visited[x] = true;
        for (uint32_t y = 0; y < p.count; ++y)
            if (p.owner_edges[x][y] && self(self, y))
                return true;
        return false;
    };
    if (reaches(reaches, dest))
        throw std::invalid_argument("Unsupported zero-delay audio cycle");
    p.push({command::operation::connect, source, dest});
    p.owner_edges[source][dest] = true;
}
void audio_graph::disconnect(uint32_t source) {
    auto &p = *impl_;
    p.check(source);
    p.push({command::operation::disconnect, source});
    std::fill_n(p.owner_edges[source], nodes, false);
}
void audio_graph::set_gain(uint32_t id, float value, double start, double tau) {
    auto &p = *impl_;
    p.check(id);
    if (p.owner_types[id] != kind::gain || !std::isfinite(value) || !std::isfinite(start) || start < 0 ||
        !std::isfinite(tau) || tau < 0)
        throw std::invalid_argument("Invalid gain automation");
    if (p.graph[id].scheduled.fetch_add(1, std::memory_order_acq_rel) >= 32) {
        p.graph[id].scheduled.fetch_sub(1);
        throw std::length_error("Audio automation limit");
    }
    try {
        p.push({command::operation::gain, id, 0, value, start, tau});
    } catch (...) {
        p.graph[id].scheduled.fetch_sub(1);
        throw;
    }
}
void audio_graph::set_source(uint32_t id, std::shared_ptr<const audio_buffer> pcm,
                             std::shared_ptr<playback_control> control) {
    auto &p = *impl_;
    p.check(id);
    if (p.owner_types[id] != kind::source || !pcm || !pcm->channels || !pcm->sample_rate || !control)
        throw std::invalid_argument("Invalid audio source");
    auto size = pcm->samples.size() * sizeof(float);
    if (size > 512ULL * 1024 * 1024 - p.pcm_bytes)
        throw std::length_error("Audio context PCM limit");
    p.keep_pcm.push_back(pcm);
    p.keep_controls.push_back(control);
    try {
        command c{command::operation::source, id};
        c.pcm = pcm.get();
        c.control = control.get();
        p.push(c);
        p.pcm_bytes += size;
    } catch (...) {
        p.keep_pcm.pop_back();
        p.keep_controls.pop_back();
        throw;
    }
}
void audio_graph::resume() {
    auto &p = *impl_;
    if (p.closed)
        throw std::runtime_error("Audio context closed");
    if (p.running.load())
        return;
    for (auto &n : p.graph)
        n.control_version = UINT64_MAX;
    if (p.use_device && !p.initialized) {
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = p.rate;
        config.periodSizeInFrames = 128;
        config.periods = 3;
        config.pUserData = this;
        config.dataCallback = [](ma_device *d, void *out, const void *, ma_uint32 frames) {
            static_cast<audio_graph *>(d->pUserData)->render(static_cast<float *>(out), frames);
        };
        if (ma_device_init(nullptr, &config, &p.device) != MA_SUCCESS)
            throw std::runtime_error("Audio output device unavailable");
        p.initialized = true;
    }
    p.running = true;
    if (p.initialized && ma_device_start(&p.device) != MA_SUCCESS) {
        p.running = false;
        throw std::runtime_error("Cannot start audio output");
    }
}
void audio_graph::suspend() {
    auto &p = *impl_;
    p.running = false;
    if (p.initialized)
        ma_device_stop(&p.device);
}
void audio_graph::close() {
    if (!impl_ || impl_->closed)
        return;
    auto &p = *impl_;
    p.running = false;
    if (p.initialized) {
        ma_device_uninit(&p.device);
        p.initialized = false;
    }
    p.closed = true;
    for (auto &n : p.graph)
        if (n.capture)
            n.capture->end();
    p.keep_pcm.clear();
    p.keep_controls.clear();
}
double audio_graph::time() const noexcept { return double(impl_->frames.load()) / impl_->rate; }
uint32_t audio_graph::sample_rate() const noexcept { return impl_->rate; }
void audio_graph::analyser(uint32_t id, std::span<float> out) const {
    auto &p = *impl_;
    p.check(id);
    auto &n = p.graph[id];
    auto end = n.written.load(std::memory_order_acquire);
    size_t count = std::min<size_t>({out.size(), history, static_cast<size_t>(end)});
    std::fill(out.begin(), out.end(), 0.f);
    for (size_t i = 0; i < count; ++i)
        out[i] = n.samples[(end - count + i) % history].load(std::memory_order_relaxed);
}
std::shared_ptr<audio_track> audio_graph::capture(uint32_t id) {
    auto &p = *impl_;
    p.check(id);
    if (p.owner_types[id] != kind::stream || p.closed)
        throw std::invalid_argument("Invalid capture destination");
    return std::make_shared<audio_track>(p.graph[id].capture);
}
void audio_graph::render(float *out, uint32_t frames) noexcept { render_at(out, frames, now()); }
void audio_graph::render_at(float *out, uint32_t frames, double steady_time) noexcept {
    auto &p = *impl_;
    std::fill_n(out, frames * 2, 0.f);
    if (!p.running.load())
        return;
    while (frames) {
        p.commands();
        auto count = std::min(frames, quantum);
        std::array<bool, nodes> done{};
        auto clock = time();
        double wall = steady_time;
        p.process(0, count, done, wall, clock);
        for (uint32_t i = 1; i < p.rt_count.load(); ++i)
            if (p.graph[i].type == kind::stream || p.graph[i].type == kind::analyser)
                p.process(i, count, done, wall, clock);
        std::copy_n(p.graph[0].data.data(), count * 2, out);
        p.frames.fetch_add(count);
        out += count * 2;
        frames -= count;
        steady_time += double(count) / p.rate;
    }
}
} // namespace webscene::media
