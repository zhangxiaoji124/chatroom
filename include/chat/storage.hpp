#pragma once
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "chat/types.hpp"

struct sqlite3;

namespace chat {

class StorageManager {
public:
    explicit StorageManager(const std::string& db_path = "data/chatroom.db");
    ~StorageManager();

    // 禁止拷贝，允许移动
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    StorageManager(StorageManager&&) noexcept;
    StorageManager& operator=(StorageManager&&) noexcept;

    // 打开数据库、建表与建索引
    bool init();

    // ========== 用户管理 ==========
    bool save_user(const UserAccount& user);
    std::optional<UserAccount> get_user(const std::string& username);
    std::vector<UserAccount> get_all_users();
    std::size_t user_count();
    bool update_last_login(const std::string& username, int64_t ts);

    // ========== 消息持久化与检索 ==========
    // is_read: 0 表示未读，1 表示已读
    bool save_message(const json& msg, int is_read = 0);
    std::vector<json> get_recent_messages(std::size_t limit = 50,
                                          const std::string& channel = "",
                                          const std::string& group = "");
    std::size_t message_count();

    // 模糊全文搜索历史消息
    std::vector<json> search_messages(const std::string& query,
                                      const std::string& channel = "",
                                      const std::string& group = "",
                                      std::size_t limit = 50);

    // 消息撤回（2分钟时效，支持本人或管理员）
    bool recall_message(const std::string& msg_id,
                        const std::string& operator_name,
                        bool is_admin,
                        std::string& err_msg,
                        json& recalled_msg_out);

    // ========== 私聊与未读消息管理 ==========
    // 获取指定接收方来自各发送方的未读消息数量统计 { "from_user": count }
    std::map<std::string, int> get_unread_counts(const std::string& to_user);
    // 获取指定用户的所有未读私聊详情
    std::vector<json> get_unread_dms(const std::string& to_user);
    // 将 from_user 发送给 to_user 的私聊标记为已读
    bool mark_dms_read(const std::string& to_user, const std::string& from_user);

    // ========== 控制台观测与数据库变更监控 ==========
    struct DbMessageRecord {
        int64_t id = 0;
        std::string msg_id;
        std::string msg_type;
        std::string from_user;
        std::string to_user;
        std::string group_name;
        std::string channel;
        std::string text;
        std::string voice_url;
        int duration = 0;
        std::string image_url;
        int64_t timestamp = 0;
        int is_read = 0;
        bool is_recalled = false;
    };
    std::vector<DbMessageRecord> get_db_messages(int64_t since_id = 0, std::size_t limit = 50);

    // ========== 历史数据平滑迁移 ==========
    // 如果 SQLite 为空，自动从旧 JSON/JSONL 文件迁移数据
    void migrate_from_json_files(const std::string& users_json_path = "data/users.json",
                                 const std::string& messages_jsonl_path = "data/messages.jsonl");

private:
    std::string db_path_;
    sqlite3* db_{nullptr};
    mutable std::mutex db_mutex_;

    bool execute_sql(const std::string& sql);
    void create_tables_and_indexes();
};

} // namespace chat
