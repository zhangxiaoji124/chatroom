#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include "chat/types.hpp"

namespace chat {

// 分布式节点元信息
struct ClusterNode {
    std::string node_id;
    std::string host;
    int port{0};
    std::string base_url;
    bool alive{false};
    int64_t last_heartbeat_ms{0};
    int64_t latency_ms{0};
    int online_users{0};

    json to_json() const {
        return json{
            {"node_id", node_id},
            {"host", host},
            {"port", port},
            {"base_url", base_url},
            {"alive", alive},
            {"last_heartbeat_ms", last_heartbeat_ms},
            {"latency_ms", latency_ms},
            {"online_users", online_users}
        };
    }
};

// ClusterManager：集群消息总线、跨节点事件发布订阅与健康保活管理器
class ClusterManager {
public:
    using EventDispatchFn = std::function<void(const std::string& type, const json& data)>;

    ClusterManager();
    ~ClusterManager();

    // 初始化本节点信息
    void init_node(const std::string& node_id, const std::string& host, int port);

    // 添加对等节点 (如 "http://127.0.0.1:8081" 或 "127.0.0.1:8081")
    void add_peer(const std::string& peer_url);

    // 启动心跳保活检测后台线程
    void start();

    // 停止心跳检测
    void stop();

    // 注册收到跨节点事件后的本地分发回调
    void set_event_dispatcher(EventDispatchFn fn);

    // 跨节点广播事件至其他对等节点
    void broadcast_event(const std::string& type, const json& data);

    // 处理从其他节点接收到的集群事件，执行去重并本地分发
    // 返回 true 表示新事件并已处理，false 表示环路或重复事件已被丢弃
    bool handle_incoming_event(const std::string& event_id,
                               const std::string& origin_node_id,
                               const std::string& type,
                               const json& data);

    // 获取当前节点 ID
    std::string node_id() const;

    // 获取当前节点端口
    int port() const;

    // 是否处于集群模式
    bool is_clustered() const;

    // 获取所有节点状态（包含本地节点）
    std::vector<ClusterNode> get_all_nodes(int local_online_users = 0) const;

private:
    void heartbeat_loop();
    void send_event_to_peer(const std::string& base_url, const json& payload);

    std::string node_id_;
    std::string host_{"127.0.0.1"};
    int port_{0};

    mutable std::mutex nodes_mutex_;
    std::vector<ClusterNode> peers_;

    mutable std::mutex dedup_mutex_;
    std::unordered_set<std::string> seen_event_ids_;
    std::vector<std::string> seen_event_order_;

    EventDispatchFn dispatcher_;

    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
};

} // namespace chat
