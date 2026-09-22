#include "chat/server.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>

int main(int argc, char* argv[]) {
    int port = 8080;
    if (const char* env_p = std::getenv("PORT")) {
        port = std::atoi(env_p);
    }

    std::string node_id;
    std::vector<std::string> peers;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--peers" && i + 1 < argc) {
            std::string peers_str = argv[++i];
            std::stringstream ss(peers_str);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) peers.push_back(item);
            }
        } else if (arg == "--peer" && i + 1 < argc) {
            peers.push_back(argv[++i]);
        } else if (arg == "--node-id" && i + 1 < argc) {
            node_id = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: chatroom_server_v2 [port] [--node-id <id>] [--peers <url1,url2,...>] [--peer <url>]\n";
            return 0;
        } else if (i == 1 && arg[0] != '-') {
            int p = std::atoi(argv[1]);
            if (p > 0 && p <= 65535) port = p;
        }
    }

    if (port <= 0 || port > 65535) {
        port = 8080;
    }

    if (node_id.empty()) {
        node_id = "node_" + std::to_string(port);
    }

    std::cout << "========================================\n";
    std::cout << "  分布式多节点聊天室服务 (Distributed Mesh)\n";
    std::cout << "  Node ID : " << node_id << "\n";
    std::cout << "  Port    : " << port << "\n";
    if (!peers.empty()) {
        std::cout << "  Peers   : ";
        for (size_t i = 0; i < peers.size(); ++i) {
            std::cout << peers[i] << (i + 1 < peers.size() ? ", " : "");
        }
        std::cout << "\n";
    } else {
        std::cout << "  Mode    : Standalone (单机模式)\n";
    }
    std::cout << "========================================\n";

    chat::ChatServer server(port, node_id, peers);
    server.run();
    return 0;
}

