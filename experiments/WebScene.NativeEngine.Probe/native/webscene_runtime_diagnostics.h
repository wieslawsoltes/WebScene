#pragma once

#include "webscene_native_engine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace webscene_native {

struct runtime_diagnostic final {
    std::string kind{"exception"};
    std::string message;
    std::string stack;
    std::string source;
    std::string document_url;
    std::string level;
    std::string stage;
    uint32_t frame_id{0};
    int line{0};
    int column{0};
    bool promise_rejection{false};
    bool truncated{false};
    std::vector<std::pair<std::string, std::string>> arguments;
};

// A signal callback may run on the producer or configuring thread. It must
// never re-enter the engine. Unregister waits for an in-flight signal callback.
class runtime_diagnostics final {
public:
    static constexpr size_t maximum_records = 1024;
    static constexpr size_t maximum_bytes = 4 * 1024 * 1024;
    static constexpr size_t maximum_text = 8192;

    bool enabled(uint32_t flag) const noexcept {
        return (flags_.load(std::memory_order_relaxed) & flag) != 0;
    }

    void configure(uint32_t flags, webscene_diagnostic_available_callback callback, void* data) {
        {
            std::lock_guard lock(callback_mutex_);
            callback_ = callback;
            data_ = data;
            flags_.store(flags, std::memory_order_release);
        }
        bool pending;
        { std::lock_guard lock(mutex_); pending = !queue_.empty() || !failure_.empty(); }
        if (pending) notify();
    }

    static std::string quote(std::string_view text, bool& truncated) {
        auto length = std::min(text.size(), maximum_text);
        if (length < text.size()) {
            truncated = true;
            while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
        }
        std::string result{"\""};
        for (auto c : text.substr(0, length)) {
            const auto b = static_cast<unsigned char>(c);
            if (c == '"' || c == '\\') { result += '\\'; result += c; }
            else if (b < 32) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00"; result += hex[b >> 4]; result += hex[b & 15];
            } else result += c;
        }
        return result + '"';
    }

    void publish(runtime_diagnostic record) {
        const auto fatal = record.kind == "failure";
        if (fatal && record.stack.empty()) {
            if (const auto newline = record.message.find('\n'); newline != std::string::npos) {
                record.stack = record.message.substr(newline + 1);
                record.message.resize(newline);
            }
        }
        if (!fatal && !enabled(record.kind == "console"
                ? WEBSCENE_DIAGNOSTIC_CONSOLE : WEBSCENE_DIAGNOSTIC_EXCEPTIONS)) return;
        bool wake;
        {
            std::lock_guard lock(mutex_);
            if (fatal && failure_published_) return;
            const auto sequence = ++sequence_;
            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto json = std::string{"{\"kind\":"} + quote(record.kind, record.truncated)
                + ",\"sequence\":" + std::to_string(sequence)
                + ",\"timestamp\":" + std::to_string(timestamp);
            const auto field = [&](const char* name, const std::string& value) {
                json += std::string{",\""} + name + "\":" + quote(value, record.truncated);
            };
            field("message", record.message); field("stack", record.stack);
            field("source", record.source); field("documentUrl", record.document_url);
            field("level", record.level); field("stage", record.stage);
            json += ",\"frameId\":" + std::to_string(record.frame_id)
                + ",\"line\":" + std::to_string(record.line)
                + ",\"column\":" + std::to_string(record.column)
                + ",\"promiseRejection\":" + (record.promise_rejection ? "true" : "false")
                + ",\"arguments\":[";
            for (size_t i = 0; i < record.arguments.size() && i < 32; ++i) {
                if (i) json += ',';
                json += "{\"type\":" + quote(record.arguments[i].first, record.truncated)
                    + ",\"value\":" + quote(record.arguments[i].second, record.truncated) + '}';
            }
            record.truncated = record.truncated || record.arguments.size() > 32;
            json += std::string{"] ,\"truncated\":"} + (record.truncated ? "true}" : "false}");
            wake = queue_.empty() && failure_.empty() && dropped_ == 0;
            if (fatal) {
                failure_published_ = true;
                failure_ = std::move(json);
                last_failure_ = failure_;
                failure_sequence_ = sequence;
                wake = true;
            } else {
                while (!queue_.empty() && (queue_.size() >= maximum_records
                    || bytes_ + json.size() > maximum_bytes)) {
                    bytes_ -= queue_.front().second.size(); queue_.pop_front(); ++dropped_;
                }
                bytes_ += json.size();
                queue_.emplace_back(sequence, std::move(json));
            }
        }
        if (wake) notify();
    }

    void note_dropped() {
        bool wake;
        { std::lock_guard lock(mutex_); wake = queue_.empty() && failure_.empty() && dropped_ == 0; ++dropped_; }
        if (wake) notify();
    }

    size_t take(char* destination, size_t capacity) {
        std::lock_guard lock(mutex_);
        if (dropped_) {
            const auto text = "{\"kind\":\"dropped\",\"droppedCount\":" + std::to_string(dropped_) + '}';
            if (copy(text, destination, capacity)) dropped_ = 0;
            return text.size() + 1;
        }
        if (!failure_.empty() && (queue_.empty() || queue_.front().first > failure_sequence_)) {
            const auto size = failure_.size() + 1;
            if (copy(failure_, destination, capacity)) failure_.clear();
            return size;
        }
        if (queue_.empty()) return 0;
        const auto size = queue_.front().second.size() + 1;
        if (copy(queue_.front().second, destination, capacity)) {
            bytes_ -= queue_.front().second.size(); queue_.pop_front();
        }
        return size;
    }

    size_t copy_failure(char* destination, size_t capacity) {
        std::lock_guard lock(mutex_);
        if (last_failure_.empty()) return 0;
        copy(last_failure_, destination, capacity);
        return last_failure_.size() + 1;
    }

private:
    static bool copy(const std::string& text, char* destination, size_t capacity) {
        if (!destination || capacity <= text.size()) return false;
        std::memcpy(destination, text.c_str(), text.size() + 1); return true;
    }
    void notify() {
        std::lock_guard lock(callback_mutex_);
        if (callback_) { try { callback_(data_); } catch (...) {} }
    }
    std::atomic<uint32_t> flags_{0};
    std::mutex mutex_, callback_mutex_;
    webscene_diagnostic_available_callback callback_{nullptr};
    void* data_{nullptr};
    std::deque<std::pair<uint64_t, std::string>> queue_;
    size_t bytes_{0};
    uint64_t sequence_{0}, dropped_{0}, failure_sequence_{0};
    std::string failure_;
    std::string last_failure_;
    bool failure_published_{false};
};
}
