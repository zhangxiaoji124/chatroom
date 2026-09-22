#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "chat/types.hpp"

namespace chat {

class StorageManager;

// UserManager：负责在线用户管理、上下线记录与后台终端日志。
//
// 职责：
//  - 用户上线（登录/注册昵称）
//  - 用户离线
//  - 在线列表查询
//  - 向控制台输出 上线/离线 事件日志（格式见各自方法）
class UserManager {
public:
    explicit UserManager(std::string accounts_file = "data/users.json");

    // 绑定 SQLite 持久化存储引擎
    void set_storage(StorageManager* storage);

    // 账号注册：检查是否存在，加盐哈希并生成密保恢复码后持久化存储
    bool register_account(const std::string& username, const std::string& password, std::string& err_msg, std::string* out_recovery_key = nullptr);

    // 账号密码验证
    bool authenticate_account(const std::string& username, const std::string& password, std::string& err_msg);

    // 管理员/直接重置密码
    bool reset_password(const std::string& username, const std::string& new_password, std::string& err_msg);

    // 用户凭密保恢复码自主重置密码
    bool verify_and_reset_password(const std::string& username, const std::string& recovery_key, const std::string& new_password, std::string& err_msg);

    // 获取用户的密保恢复码（供管理员或注册展示）
    std::string get_recovery_key(const std::string& username) const;

    // 生成格式化的随机恢复码（如 REC-A1B2-C3D4）
    static std::string generate_recovery_key();

    // 删除账号
    bool delete_account(const std::string& username, std::string& err_msg);

    // 判断用户是否已注册账号
    bool has_account(const std::string& username) const;

    // 获取所有注册账号
    std::vector<UserAccount> all_accounts() const;

    // 注册账号数量
    size_t account_count() const;

    // 签发会话 Token（有效期内可用）
    std::string issue_token(const std::string& username);

    // 校验 Token：有效返回对应 username，无效或过期返回空字符串
    std::string verify_token(const std::string& token);

    // 注销 Token
    void revoke_token(const std::string& token);

    // 用户上线。nickname 必须非空且唯一。
    // 返回 true 表示成功上线；false 表示昵称已被占用。
    bool user_online(const std::string& nickname, const std::string& address);

    // 用户离线。成功返回 true，用户不存在返回 false。
    bool user_offline(const std::string& nickname);

    // 判断用户是否在线。
    bool is_online(const std::string& nickname) const;

    // 返回当前在线用户列表。
    std::vector<OnlineUser> online_users() const;

    // 当前在线人数。
    size_t online_count() const;

    // 打印用户上线日志到控制台，例如：
    //   [上线] 张三 (127.0.0.1:54321) at 1690000000000
    void log_online(const OnlineUser& u) const;

    // 打印用户离线日志到控制台，例如：
    //   [离线] 张三 (127.0.0.1:54321)，在线时长 1234s
    void log_offline(const std::string& nickname, int64_t duration_ms) const;

private:
    void load_accounts_locked();
    void save_accounts_locked();

    mutable std::mutex mutex_;
    StorageManager* storage_{nullptr};
    std::string accounts_file_;
    std::unordered_map<std::string, UserAccount> accounts_; // username -> account
    std::unordered_map<std::string, std::string> tokens_;   // token -> username
    std::unordered_map<std::string, int64_t> token_expiry_; // token -> expire_ts(ms)
    std::unordered_map<std::string, OnlineUser> users_;     // nickname -> user
    std::unordered_map<std::string, int64_t> online_since_; // nickname -> 上线时间戳(ms)
};

} // namespace chat
