#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace chat {

using json = nlohmann::json;

// 在线用户记录（UserManager 维护）
struct OnlineUser {
    std::string nickname;   // 唯一昵称（登录标识）
    std::string address;    // 客户端地址 ip:port
    int64_t     connect_ts; // 上线时间戳(ms)
};

// 一条聊天消息（WsHandler 广播与落库）
struct ChatMessage {
    std::string from;       // 发送者昵称
    std::string text;       // 消息正文
    int64_t     ts;         // 发送时间戳(ms)
    json to_json() const {
        return json{{"type","chat"},{"from",from},{"text",text},{"ts",ts}};
    }
};

// 注册用户账号记录（持久化存储）
struct UserAccount {
    std::string username;
    std::string password_hash;
    std::string salt;
    int64_t     created_at = 0;
    int64_t     last_login = 0;
    std::string recovery_key;

    json to_json() const {
        return json{
            {"username", username},
            {"password_hash", password_hash},
            {"salt", salt},
            {"created_at", created_at},
            {"last_login", last_login},
            {"recovery_key", recovery_key}
        };
    }

    static UserAccount from_json(const json& j) {
        UserAccount acc;
        acc.username = j.value("username", "");
        acc.password_hash = j.value("password_hash", "");
        acc.salt = j.value("salt", "");
        acc.created_at = j.value("created_at", 0LL);
        acc.last_login = j.value("last_login", 0LL);
        acc.recovery_key = j.value("recovery_key", "");
        return acc;
    }
};

// 上线/离线事件（用于终端日志与广播通知）
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace chat
