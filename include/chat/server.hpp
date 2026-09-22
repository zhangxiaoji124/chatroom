#pragma once
#include <string>
#include <memory>
#include <vector>
#include "chat/user_manager.hpp"
#include "chat/ws_handler.hpp"
#include "chat/storage.hpp"
#include "chat/rate_limiter.hpp"
#include "chat/cluster_manager.hpp"

namespace chat {

// ChatServer：聊天室协调层。
// 组合 UserManager（用户/上下线）、WsHandler（连接/广播）、StorageManager（SQLite 持久化）、
// RateLimiter（安全风控限流）与 ClusterManager（分布式跨节点集群总线）。
class ChatServer {
public:
    explicit ChatServer(int port, const std::string& node_id = "", const std::vector<std::string>& peers = {});
    ~ChatServer();

    // 启动服务器（阻塞，直到收到退出信号；Ctrl+C 退出）。
    void run();

    // 供测试与扩展：暴露内部组件。
    UserManager&    users()        { return users_; }
    WsHandler&      ws()           { return ws_; }
    StorageManager& storage()      { return storage_; }
    RateLimiter&    rate_limiter() { return rate_limiter_; }
    ClusterManager& cluster()      { return cluster_; }

    // 动态添加集群节点
    void add_peer(const std::string& peer_url) { cluster_.add_peer(peer_url); }

private:
    int port_;
    bool running_;
    StorageManager storage_;
    RateLimiter rate_limiter_;
    UserManager users_;
    WsHandler   ws_;
    ClusterManager cluster_;

    // 注册所有 HTTP + WebSocket 路由（含集群接口 /api/cluster/*）。
    void setup_routes();
    // 消息落库（持久化至 SQLite 与 data/messages.jsonl）+ 控制台打印。
    void persist_and_log(const ChatMessage& msg);
};

} // namespace chat
