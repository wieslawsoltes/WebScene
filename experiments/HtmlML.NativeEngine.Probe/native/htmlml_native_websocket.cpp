#include "htmlml_native_websocket.h"

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessageType.h>
#include <ixwebsocket/IXNetSystem.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace htmlml_native {
namespace {

constexpr size_t maximum_queued_websocket_bytes = 64U * 1024U * 1024U;
constexpr size_t maximum_queued_websocket_events = 4096U;

class network_system_lifetime final {
public:
    network_system_lifetime()
        : ready(ix::initNetSystem())
    {
    }

    ~network_system_lifetime()
    {
        if (ready) static_cast<void>(ix::uninitNetSystem());
    }

    bool ready;
};

bool network_system_ready()
{
    static network_system_lifetime lifetime;
    return lifetime.ready;
}

struct socket_record final {
    std::shared_ptr<ix::WebSocket> socket;
    std::atomic<bool> terminal_event_queued{false};
};

} // namespace

struct native_websocket_transport::state final {
    mutable std::mutex mutex;
    std::unordered_map<uint64_t, std::shared_ptr<socket_record>> sockets;
    std::deque<event> events;
    size_t queued_bytes{0};
    uint64_t next_socket_id{1};
    bool shutting_down{false};

    bool enqueue(event value, bool priority = false)
    {
        std::lock_guard lock(mutex);
        if (shutting_down) return false;
        if (priority) {
            while (!events.empty()
                && (events.size() >= maximum_queued_websocket_events
                    || value.payload.size()
                        > maximum_queued_websocket_bytes - queued_bytes)) {
                queued_bytes -= events.front().payload.size();
                events.pop_front();
            }
        }
        if (events.size() >= maximum_queued_websocket_events
            || value.payload.size() > maximum_queued_websocket_bytes - queued_bytes) {
            return false;
        }
        queued_bytes += value.payload.size();
        events.push_back(std::move(value));
        return true;
    }
};

native_websocket_transport::native_websocket_transport()
    : state_(std::make_shared<state>())
{
    static_cast<void>(network_system_ready());
}

native_websocket_transport::~native_websocket_transport()
{
    shutdown();
}

uint64_t native_websocket_transport::open(
    std::string url,
    std::string origin,
    std::vector<std::string> protocols)
{
    auto shared_state = state_;
    if (!network_system_ready()) return 0;
    auto record = std::make_shared<socket_record>();
    record->socket = std::make_shared<ix::WebSocket>();
    uint64_t socket_id = 0;
    {
        std::lock_guard lock(shared_state->mutex);
        if (shared_state->shutting_down) return 0;
        socket_id = shared_state->next_socket_id++;
        shared_state->sockets.emplace(socket_id, record);
    }

    record->socket->setUrl(url);
    record->socket->disableAutomaticReconnection();
    record->socket->setHandshakeTimeout(30);
    if (!origin.empty()) {
        record->socket->setExtraHeaders({{"Origin", std::move(origin)}});
    }
    for (auto& protocol : protocols) {
        record->socket->addSubProtocol(protocol);
    }

    std::weak_ptr<state> weak_state(shared_state);
    std::weak_ptr<socket_record> weak_record(record);
    record->socket->setOnMessageCallback(
        [weak_state, weak_record, socket_id](const ix::WebSocketMessagePtr& message) {
            auto state_value = weak_state.lock();
            auto record_value = weak_record.lock();
            if (!state_value || !record_value || !message) return;

            event value;
            value.socket_id = socket_id;
            switch (message->type) {
                case ix::WebSocketMessageType::Open: {
                    value.type = event_type::opened;
                    value.protocol = message->openInfo.protocol;
                    const auto extension =
                        message->openInfo.headers.find("sec-websocket-extensions");
                    if (extension != message->openInfo.headers.end()) {
                        value.extensions = extension->second;
                    }
                    static_cast<void>(state_value->enqueue(std::move(value), true));
                    return;
                }
                case ix::WebSocketMessageType::Message:
                    value.type = event_type::message;
                    value.binary = message->binary;
                    value.payload.assign(message->str.begin(), message->str.end());
                    if (!state_value->enqueue(std::move(value))
                        && !record_value->terminal_event_queued.exchange(true)) {
                        event overflow;
                        overflow.socket_id = socket_id;
                        overflow.type = event_type::error;
                        overflow.reason = "WebSocket receive queue exceeded its safety limit";
                        static_cast<void>(state_value->enqueue(std::move(overflow), true));
                        overflow = {};
                        overflow.socket_id = socket_id;
                        overflow.type = event_type::closed;
                        overflow.close_code = 1009;
                        overflow.reason = "WebSocket receive queue exceeded its safety limit";
                        overflow.was_clean = false;
                        static_cast<void>(state_value->enqueue(std::move(overflow), true));
                        record_value->socket->close(
                            1009,
                            "WebSocket receive queue exceeded its safety limit");
                    }
                    return;
                case ix::WebSocketMessageType::Error:
                    if (record_value->terminal_event_queued.exchange(true)) return;
                    value.type = event_type::error;
                    value.reason = message->errorInfo.reason;
                    static_cast<void>(state_value->enqueue(std::move(value), true));
                    value = {};
                    value.socket_id = socket_id;
                    value.type = event_type::closed;
                    value.close_code = 1006;
                    value.reason = message->errorInfo.reason;
                    value.was_clean = false;
                    static_cast<void>(state_value->enqueue(std::move(value), true));
                    return;
                case ix::WebSocketMessageType::Close:
                    if (record_value->terminal_event_queued.exchange(true)) return;
                    value.type = event_type::closed;
                    value.close_code = message->closeInfo.code;
                    value.reason = message->closeInfo.reason;
                    value.was_clean =
                        message->closeInfo.code != 0
                        && message->closeInfo.code != 1006;
                    static_cast<void>(state_value->enqueue(std::move(value), true));
                    return;
                case ix::WebSocketMessageType::Ping:
                case ix::WebSocketMessageType::Pong:
                case ix::WebSocketMessageType::Fragment:
                    return;
            }
        });
    record->socket->start();
    return socket_id;
}

bool native_websocket_transport::send(
    uint64_t socket_id,
    const uint8_t* data,
    size_t size,
    bool binary)
{
    std::shared_ptr<socket_record> record;
    {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->sockets.find(socket_id);
        if (found == state_->sockets.end() || state_->shutting_down) return false;
        record = found->second;
    }
    const std::string payload(
        reinterpret_cast<const char*>(data),
        size);
    const auto sent = binary
        ? record->socket->sendBinary(payload)
        : record->socket->sendText(payload);
    return sent.success;
}

bool native_websocket_transport::close(
    uint64_t socket_id,
    uint16_t code,
    std::string_view reason)
{
    std::shared_ptr<socket_record> record;
    {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->sockets.find(socket_id);
        if (found == state_->sockets.end() || state_->shutting_down) return false;
        record = found->second;
    }
    record->socket->close(code, std::string(reason));
    return true;
}

size_t native_websocket_transport::buffered_amount(uint64_t socket_id) const
{
    std::shared_ptr<socket_record> record;
    {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->sockets.find(socket_id);
        if (found == state_->sockets.end() || state_->shutting_down) return 0;
        record = found->second;
    }
    return record->socket->bufferedAmount();
}

bool native_websocket_transport::try_pop(event& value)
{
    std::lock_guard lock(state_->mutex);
    if (state_->events.empty()) return false;
    value = std::move(state_->events.front());
    state_->queued_bytes -= value.payload.size();
    state_->events.pop_front();
    return true;
}

bool native_websocket_transport::has_pending_events() const noexcept
{
    std::lock_guard lock(state_->mutex);
    return !state_->events.empty();
}

void native_websocket_transport::release(uint64_t socket_id)
{
    std::shared_ptr<socket_record> record;
    {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->sockets.find(socket_id);
        if (found == state_->sockets.end()) return;
        record = std::move(found->second);
        state_->sockets.erase(found);
    }
    record->socket->stop();
    record->socket->setOnMessageCallback(nullptr);
}

void native_websocket_transport::shutdown()
{
    if (!state_) return;
    std::unordered_map<uint64_t, std::shared_ptr<socket_record>> sockets;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->shutting_down) return;
        state_->shutting_down = true;
        sockets.swap(state_->sockets);
        state_->events.clear();
        state_->queued_bytes = 0;
    }
    for (auto& [socket_id, record] : sockets) {
        static_cast<void>(socket_id);
        record->socket->stop();
        record->socket->setOnMessageCallback(nullptr);
    }
}

} // namespace htmlml_native
