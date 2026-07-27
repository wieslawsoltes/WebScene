#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace webscene_native {

// The socket implementation runs independently of V8. Its callbacks only copy
// data into this queue; JavaScript delivery always happens later on the native
// engine worker through v8_dom_runtime::pump_task().
class native_websocket_transport final {
public:
    enum class event_type {
        opened,
        message,
        error,
        closed
    };

    struct event final {
        uint64_t socket_id{0};
        event_type type{event_type::error};
        std::vector<uint8_t> payload;
        std::string protocol;
        std::string extensions;
        std::string reason;
        uint16_t close_code{0};
        bool binary{false};
        bool was_clean{false};
    };

    native_websocket_transport();
    ~native_websocket_transport();

    native_websocket_transport(const native_websocket_transport&) = delete;
    native_websocket_transport& operator=(const native_websocket_transport&) = delete;

    uint64_t open(
        std::string url,
        std::string origin,
        std::vector<std::string> protocols);
    bool send(uint64_t socket_id, const uint8_t* data, size_t size, bool binary);
    bool close(uint64_t socket_id, uint16_t code, std::string_view reason);
    size_t buffered_amount(uint64_t socket_id) const;

    bool try_pop(event& value);
    bool has_pending_events() const noexcept;
    void release(uint64_t socket_id);
    void shutdown();

private:
    struct state;
    std::shared_ptr<state> state_;
};

} // namespace webscene_native
