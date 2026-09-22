#include "chat/cluster_manager.hpp"
#include "httplib.h"

#include <iostream>
#include <random>
#include <chrono>

namespace chat {

namespace {

inline std::string gen_random_hex(size_t len) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    static const char hex_digits[] = "0123456789abcdef";
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        s.push_back(hex_digits[rng() % 16]);
    }
    return s;
}

// 规范化 URL 为 "http://host:port" 并解析
bool parse_peer_url(std::string_view raw, std::string& host_out, int& port_out, std::string& base_url_out) {
    std::string s(raw);
    if (s.rfind("http://", 0) == 0) {
        s = s.substr(7);
    } else if (s.rfind("https://", 0) == 0) {
        s = s.substr(8);
    }
    // 去除末尾斜杠
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    auto colon = s.find(':');
    if (colon == std::string::npos) return false;
    host_out = s.substr(0, colon);
    if (host_out.empty()) host_out = "127.0.0.1";
    try {
        port_out = std::stoi(s.substr(colon + 1));
    } catch (...) {
        return false;
    }
    base_url_out = "http://" + host_out + ":" + std::to_string(port_out);
    return true;
}

} // namespace

ClusterManager::ClusterManager() = default;

ClusterManager::~ClusterManager() {
    stop();
}

void ClusterManager::init_node(const std::string& node_id, const std::string& host, int port) {
    node_id_ = node_id.empty() ? ("node_" + std::to_string(port) + "_" + gen_random_hex(4)) : node_id;
    host_ = host.empty() ? "127.0.0.1" : host;
    port_ = port;
}

void ClusterManager::add_peer(const std::string& peer_url) {
    std::string host;
    int port = 0;
    std::string base_url;
    if (!parse_peer_url(peer_url, host, port, base_url)) {
        std::cerr << "[Cluster] 无法解析对等节点地址: " << peer_url << '\n';
        return;
    }

    // 避免将自身作为对等节点
    if ((host == "127.0.0.1" || host == "localhost" || host == host_) && port == port_) {
        return;
    }

    std::lock_guard<std::mutex> lock(nodes_mutex_);
    for (const auto& p : peers_) {
        if (p.port == port && (p.host == host || (p.host == "127.0.0.1" && host == "localhost"))) {
            return; // 已存在
        }
    }

    ClusterNode node;
    node.node_id = "node_" + std::to_string(port);
    node.host = host;
    node.port = port;
    node.base_url = base_url;
    node.alive = false;
    peers_.push_back(std::move(node));

    std::cout << "[Cluster] 注册对等节点: " << base_url << '\n';
}

void ClusterManager::start() {
    if (running_.exchange(true)) return;
    if (peers_.empty()) {
        std::cout << "[Cluster] 未配置其他集群对等节点，运行于单机独立模式\n";
        return;
    }

    std::cout << "[Cluster] 启动分布式集群网格，本节点: " << node_id_ << " (端口 " << port_ << ")\n";
    heartbeat_thread_ = std::thread(&ClusterManager::heartbeat_loop, this);
}

void ClusterManager::stop() {
    if (!running_.exchange(false)) return;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

void ClusterManager::set_event_dispatcher(EventDispatchFn fn) {
    dispatcher_ = std::move(fn);
}

void ClusterManager::broadcast_event(const std::string& type, const json& data) {
    if (peers_.empty()) return;

    const std::string event_id = node_id_ + "_" + std::to_string(now_ms()) + "_" + gen_random_hex(6);

    // 本地记录已见，防止环路反弹
    {
        std::lock_guard<std::mutex> lock(dedup_mutex_);
        seen_event_ids_.insert(event_id);
        seen_event_order_.push_back(event_id);
        if (seen_event_order_.size() > 10000) {
            for (size_t i = 0; i < 2000; ++i) {
                seen_event_ids_.erase(seen_event_order_[i]);
            }
            seen_event_order_.erase(seen_event_order_.begin(), seen_event_order_.begin() + 2000);
        }
    }

    const json payload{
        {"event_id", event_id},
        {"origin_node_id", node_id_},
        {"type", type},
        {"data", data},
        {"ts", now_ms()}
    };

    std::vector<std::string> target_urls;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (const auto& p : peers_) {
            target_urls.push_back(p.base_url);
        }
    }

    for (const auto& url : target_urls) {
        std::thread([this, url, payload]() {
            send_event_to_peer(url, payload);
        }).detach();
    }
}

void ClusterManager::send_event_to_peer(const std::string& base_url, const json& payload) {
    try {
        httplib::Client cli(base_url);
        cli.set_connection_timeout(std::chrono::milliseconds(1000));
        cli.set_read_timeout(std::chrono::milliseconds(2000));
        auto res = cli.Post("/api/cluster/event", payload.dump(), "application/json");
        if (!res || res->status != 200) {
            // 忽略临时网络波动
        }
    } catch (...) {
        // 网络异常安静退出
    }
}

bool ClusterManager::handle_incoming_event(const std::string& event_id,
                                          const std::string& origin_node_id,
                                          const std::string& type,
                                          const json& data) {
    // 来源是自身则拦截丢弃，避免循环广播风暴
    if (origin_node_id == node_id_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(dedup_mutex_);
        if (seen_event_ids_.contains(event_id)) {
            return false; // 重复事件已过滤
        }
        seen_event_ids_.insert(event_id);
        seen_event_order_.push_back(event_id);
        if (seen_event_order_.size() > 10000) {
            for (size_t i = 0; i < 2000; ++i) {
                seen_event_ids_.erase(seen_event_order_[i]);
            }
            seen_event_order_.erase(seen_event_order_.begin(), seen_event_order_.begin() + 2000);
        }
    }

    // 本地回调分发
    if (dispatcher_) {
        dispatcher_(type, data);
    }
    return true;
}

std::string ClusterManager::node_id() const {
    return node_id_;
}

int ClusterManager::port() const {
    return port_;
}

bool ClusterManager::is_clustered() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    return !peers_.empty();
}

std::vector<ClusterNode> ClusterManager::get_all_nodes(int local_online_users) const {
    std::vector<ClusterNode> result;

    // 本节点
    ClusterNode self_node;
    self_node.node_id = node_id_;
    self_node.host = host_;
    self_node.port = port_;
    self_node.base_url = "http://" + host_ + ":" + std::to_string(port_);
    self_node.alive = true;
    self_node.last_heartbeat_ms = now_ms();
    self_node.latency_ms = 0;
    self_node.online_users = local_online_users;
    result.push_back(std::move(self_node));

    // 对等节点
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (const auto& p : peers_) {
            result.push_back(p);
        }
    }
    return result;
}

void ClusterManager::heartbeat_loop() {
    while (running_) {
        std::vector<ClusterNode> copy_peers;
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            copy_peers = peers_;
        }

        for (auto& peer : copy_peers) {
            if (!running_) break;
            const int64_t t_start = now_ms();
            bool is_alive = false;
            int online = 0;
            std::string reported_id;

            try {
                httplib::Client cli(peer.base_url);
                cli.set_connection_timeout(std::chrono::milliseconds(1000));
                cli.set_read_timeout(std::chrono::milliseconds(1000));
                auto res = cli.Get("/api/cluster/ping");
                if (res && res->status == 200) {
                    is_alive = true;
                    try {
                        auto j = json::parse(res->body);
                        online = j.value("online_users", 0);
                        reported_id = j.value("node_id", "");
                    } catch (...) {}
                }
            } catch (...) {
                is_alive = false;
            }

            const int64_t t_latency = is_alive ? (now_ms() - t_start) : 9999;

            // 回写状态
            {
                std::lock_guard<std::mutex> lock(nodes_mutex_);
                for (auto& p : peers_) {
                    if (p.base_url == peer.base_url) {
                        p.alive = is_alive;
                        p.latency_ms = t_latency;
                        if (is_alive) {
                            p.last_heartbeat_ms = now_ms();
                            p.online_users = online;
                            if (!reported_id.empty()) p.node_id = reported_id;
                        }
                        break;
                    }
                }
            }
        }

        // 心跳休眠 2 秒
        for (int i = 0; i < 20 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace chat
