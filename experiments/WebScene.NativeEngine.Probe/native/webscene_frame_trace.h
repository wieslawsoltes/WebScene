#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Worker-owned diagnostic ring. Never prints or allocates while recording;
// timestamps share steady_clock's epoch with the native scheduling metrics.
struct webscene_frame_trace final {
    struct sample { const char* stage; long long timestamp; unsigned long long sequence; };
    std::vector<sample> samples;
    size_t count = 0;
    ~webscene_frame_trace() { dump(); }
    struct scope {
        webscene_frame_trace& trace;
        const char* end;
        unsigned long long sequence;
        scope(webscene_frame_trace& owner, const char* start, const char* finish, unsigned long long id)
            : trace(owner), end(finish), sequence(id) { trace.mark(start, id); }
        ~scope() { trace.mark(end, sequence); }
    };
    webscene_frame_trace() {
        const auto* setting = std::getenv("WEBSCENE_TRACE_FRAME_PIPELINE");
        if (setting && setting[0] == '1') samples.resize(65536);
    }
    void mark(const char* stage, unsigned long long sequence) {
        if (samples.empty()) return;
        samples[count++ % samples.size()] = {stage,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(), sequence};
    }
    void dump() {
        const auto begin = count > samples.size() ? count - samples.size() : 0;
        for (auto i = begin; i < count; ++i) {
            const auto& value = samples[i % samples.size()];
            std::fprintf(stderr, "Frame pipeline: {\"stage\":\"%s\",\"ns\":%lld,\"sequence\":%llu}\n",
                value.stage, value.timestamp, value.sequence);
        }
        count = 0;
    }
};
