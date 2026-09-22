#include "chat/ws_handler.hpp"

#include "httplib.h"

#include <utility>

namespace {

static httplib::ws::WebSocket* to_ws(void* pointer) {
    return static_cast<httplib::ws::WebSocket*>(pointer);
}

} // namespace

namespace chat {

void WsHandler::set_message_sink(MessageSink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
}

void WsHandler::register_ws(const std::string& nickname, void* ws) {
    std::lock_guard<std::mutex> lock(mutex_);
    conns_[nickname] = ws;
}

void WsHandler::unregister_ws(const std::string& nickname) {
    std::lock_guard<std::mutex> lock(mutex_);
    conns_.erase(nickname);
}

void WsHandler::handle_text(const std::string& from, const std::string& text) {
    const ChatMessage msg{from, text, now_ms()};

    MessageSink sink;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sink = sink_;
    }

    if (sink) {
        sink(msg);
    }
    broadcast(msg);
}

void WsHandler::broadcast(const ChatMessage& msg) {
    broadcast_raw(msg.to_json().dump());
}

void WsHandler::broadcast_raw(const std::string& json_str) {
    // Keep registration changes serialized with sends. In particular, a
    // background AI broadcast cannot retain a WebSocket pointer after its
    // connection handler has unregistered it and returned.
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = conns_.begin(); it != conns_.end();) {
        if (!to_ws(it->second)->send(json_str)) {
            it = conns_.erase(it);
        } else {
            ++it;
        }
    }
}

bool WsHandler::send_raw(const std::string& nickname,
                         const std::string& json_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = conns_.find(nickname);
    if (it == conns_.end()) {
        return false;
    }
    if (to_ws(it->second)->send(json_str)) {
        return true;
    }
    conns_.erase(it);
    return false;
}

size_t WsHandler::connection_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return conns_.size();
}

bool WsHandler::is_registered(const std::string& nickname) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return conns_.contains(nickname);
}

} // namespace chat
