#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "chat/types.hpp"

namespace httplib { namespace ws { class WebSocket; } }

namespace chat {

// WsHandler：负责 WebSocket 连接注册、消息分发与广播。
//
// 服务器端模型（cpp-httplib master）：
//   svr.WebSocket("/ws", handler) 中，handler 在自己的线程里
//   用 `while (ws.read(msg)) { ... }` 阻塞式读取每条消息。
//   因此每个 WebSocket 对象在 read 循环存活期间有效。
//
// 职责：
//  - register_ws(unregister_ws)：昵称 -> ws::WebSocket* 映射
//  - handle_text：把一条来自某用户的消息交给 sink（落库+终端打印）并广播
//  - broadcast / broadcast_raw：给所有已注册连接发文本帧
class WsHandler {
public:
    using MessageSink = std::function<void(const ChatMessage&)>;

    void set_message_sink(MessageSink sink);

    // 注册/更新 昵称 -> WebSocket。同一昵称重复注册会覆盖。
    void register_ws(const std::string& nickname, void* ws);

    // 移除昵称对应的连接。
    void unregister_ws(const std::string& nickname);

    // 处理一条来自 from 的文本消息：构造 ChatMessage 交给 sink，
    // 然后 broadcast 给所有连接。
    void handle_text(const std::string& from, const std::string& text);

    // 广播 ChatMessage（JSON 文本帧）。
    void broadcast(const ChatMessage& msg);

    // 广播任意 JSON 字符串文本帧（用于系统通知，如上线/离线）。
    void broadcast_raw(const std::string& json_str);

    // Serializes targeted sends with broadcasts, including AI-worker sends.
    bool send_raw(const std::string& nickname, const std::string& json_str);

    // 检查昵称是否已连接到本节点
    bool is_registered(const std::string& nickname) const;

    // 当前已注册连接数。
    size_t connection_count() const;

private:
    MessageSink sink_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, void*> conns_;  // nickname -> ws::WebSocket*
};

} // namespace chat
