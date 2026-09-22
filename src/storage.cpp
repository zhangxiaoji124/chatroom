#include "chat/storage.hpp"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace chat {

StorageManager::StorageManager(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {
}

StorageManager::~StorageManager() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

StorageManager::StorageManager(StorageManager&& other) noexcept {
    std::scoped_lock lock(db_mutex_, other.db_mutex_);
    db_path_ = std::move(other.db_path_);
    db_ = other.db_;
    other.db_ = nullptr;
}

StorageManager& StorageManager::operator=(StorageManager&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(db_mutex_, other.db_mutex_);
        if (db_) {
            sqlite3_close_v2(db_);
        }
        db_path_ = std::move(other.db_path_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

bool StorageManager::execute_sql(const std::string& sql) {
    char* err_msg = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[Storage] SQL error: " << (err_msg ? err_msg : "unknown") << " | Query: " << sql << '\n';
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

void StorageManager::create_tables_and_indexes() {
    const std::string sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            username TEXT PRIMARY KEY,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            last_login INTEGER NOT NULL,
            recovery_key TEXT DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            msg_id TEXT,
            msg_type TEXT NOT NULL,
            from_user TEXT NOT NULL,
            to_user TEXT,
            group_name TEXT,
            channel TEXT NOT NULL,
            text TEXT,
            voice_url TEXT,
            duration INTEGER DEFAULT 0,
            image_url TEXT,
            raw_json TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            is_read INTEGER DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_msg_ts ON messages(timestamp);
        CREATE INDEX IF NOT EXISTS idx_msg_channel_ts ON messages(channel, timestamp);
        CREATE INDEX IF NOT EXISTS idx_msg_group ON messages(group_name, timestamp);
        CREATE INDEX IF NOT EXISTS idx_msg_dm ON messages(from_user, to_user, timestamp);
        CREATE INDEX IF NOT EXISTS idx_msg_unread ON messages(to_user, is_read);
    )";
    execute_sql(sql);
    execute_sql("ALTER TABLE users ADD COLUMN recovery_key TEXT DEFAULT '';");
}

bool StorageManager::init() {
    std::lock_guard<std::mutex> lock(db_mutex_);

    // 确保父目录存在
    const auto parent_dir = std::filesystem::path(db_path_).parent_path();
    if (!parent_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent_dir, ec);
    }

    const int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[Storage] Cannot open database: " << (db_ ? sqlite3_errmsg(db_) : "alloc failure") << '\n';
        if (db_) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        return false;
    }

    // 启用 WAL 模式与并发性能优化
    execute_sql("PRAGMA journal_mode = WAL;");
    execute_sql("PRAGMA synchronous = NORMAL;");
    execute_sql("PRAGMA foreign_keys = ON;");

    create_tables_and_indexes();
    return true;
}

// ========== 用户管理 ==========

bool StorageManager::save_user(const UserAccount& user) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return false;

    const char* sql = R"(
        INSERT INTO users (username, password_hash, salt, created_at, last_login, recovery_key)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(username) DO UPDATE SET
            password_hash = excluded.password_hash,
            salt = excluded.salt,
            created_at = excluded.created_at,
            last_login = excluded.last_login,
            recovery_key = CASE WHEN excluded.recovery_key != '' THEN excluded.recovery_key ELSE users.recovery_key END;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, user.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, user.password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, user.salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, user.created_at);
    sqlite3_bind_int64(stmt, 5, user.last_login);
    sqlite3_bind_text(stmt, 6, user.recovery_key.c_str(), -1, SQLITE_TRANSIENT);

    const int step_res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return step_res == SQLITE_DONE;
}

std::optional<UserAccount> StorageManager::get_user(const std::string& username) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = "SELECT username, password_hash, salt, created_at, last_login, recovery_key FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<UserAccount> acc;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        UserAccount u;
        u.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.created_at = sqlite3_column_int64(stmt, 3);
        u.last_login = sqlite3_column_int64(stmt, 4);
        const char* rk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.recovery_key = rk ? rk : "";
        acc = std::move(u);
    }

    sqlite3_finalize(stmt);
    return acc;
}

std::vector<UserAccount> StorageManager::get_all_users() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<UserAccount> result;
    if (!db_) return result;

    const char* sql = "SELECT username, password_hash, salt, created_at, last_login, recovery_key FROM users ORDER BY created_at ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UserAccount u;
        u.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.created_at = sqlite3_column_int64(stmt, 3);
        u.last_login = sqlite3_column_int64(stmt, 4);
        const char* rk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.recovery_key = rk ? rk : "";
        result.push_back(std::move(u));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::size_t StorageManager::user_count() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM users;";
    sqlite3_stmt* stmt = nullptr;
    std::size_t count = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

bool StorageManager::update_last_login(const std::string& username, int64_t ts) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return false;

    const char* sql = "UPDATE users SET last_login = ? WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);

    const int step_res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return step_res == SQLITE_DONE;
}

// ========== 消息持久化与检索 ==========

bool StorageManager::save_message(const json& msg, int is_read) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return false;

    const std::string msg_id = msg.value("msg_id", "");
    const std::string msg_type = msg.value("type", "chat");
    const std::string from_user = msg.value("from", "");
    const std::string to_user = msg.value("to", "");
    const std::string group_name = msg.value("group", "");
    
    std::string channel = "global";
    if (!group_name.empty()) {
        channel = "group";
    } else if (msg_type == "dm") {
        channel = "dm";
    }

    const std::string text = msg.value("text", "");
    const std::string voice_url = msg.value("voice_url", "");
    const int duration = msg.value("duration", 0);
    const std::string image_url = msg.value("image_url", "");
    const std::string raw_json = msg.dump();
    const int64_t ts = msg.value("ts", now_ms());

    const char* sql = R"(
        INSERT INTO messages (msg_id, msg_type, from_user, to_user, group_name, channel, text, voice_url, duration, image_url, raw_json, timestamp, is_read)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, msg_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, msg_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, from_user.c_str(), -1, SQLITE_TRANSIENT);
    if (!to_user.empty()) sqlite3_bind_text(stmt, 4, to_user.c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 4);

    if (!group_name.empty()) sqlite3_bind_text(stmt, 5, group_name.c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 5);

    sqlite3_bind_text(stmt, 6, channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, text.c_str(), -1, SQLITE_TRANSIENT);
    if (!voice_url.empty()) sqlite3_bind_text(stmt, 8, voice_url.c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 8);

    sqlite3_bind_int(stmt, 9, duration);
    if (!image_url.empty()) sqlite3_bind_text(stmt, 10, image_url.c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 10);

    sqlite3_bind_text(stmt, 11, raw_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 12, ts);
    sqlite3_bind_int(stmt, 13, is_read);

    const int step_res = sqlite3_step(stmt);
    const bool ok = (step_res == SQLITE_DONE);
    int64_t inserted_id = 0;
    if (ok) {
        inserted_id = sqlite3_last_insert_rowid(db_);
    }
    sqlite3_finalize(stmt);

    if (ok) {
        std::string dest = group_name.empty() ? (to_user.empty() ? "Global" : ("@" + to_user)) : ("#" + group_name);
        std::cout << "[DB:INSERT] #" << inserted_id << " [" << channel << "] " << from_user << " -> " << dest
                  << ": \"" << (text.empty() ? ("[" + msg_type + "]") : text) << "\"" << '\n';
    }
    return ok;
}

std::vector<json> StorageManager::get_recent_messages(std::size_t limit,
                                                      const std::string& channel,
                                                      const std::string& group) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<json> list;
    if (!db_ || limit == 0) return list;

    std::string sql;
    if (!group.empty()) {
        sql = "SELECT raw_json FROM messages WHERE group_name = ? ORDER BY timestamp DESC, id DESC LIMIT ?;";
    } else if (channel == "global") {
        sql = "SELECT raw_json FROM messages WHERE channel = 'global' ORDER BY timestamp DESC, id DESC LIMIT ?;";
    } else {
        sql = "SELECT raw_json FROM messages ORDER BY timestamp DESC, id DESC LIMIT ?;";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    if (!group.empty()) {
        sqlite3_bind_text(stmt, 1, group.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
    } else {
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (raw) {
            try {
                list.push_back(json::parse(raw));
            } catch (...) {}
        }
    }

    sqlite3_finalize(stmt);
    // 反转为时间递增顺序，便于前端直接顺序展示
    std::reverse(list.begin(), list.end());
    return list;
}

std::size_t StorageManager::message_count() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM messages;";
    sqlite3_stmt* stmt = nullptr;
    std::size_t count = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

std::vector<json> StorageManager::search_messages(const std::string& query,
                                                  const std::string& channel,
                                                  const std::string& group,
                                                  std::size_t limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<json> list;
    if (!db_ || query.empty() || limit == 0) return list;

    std::string sql;
    const std::string pattern = "%" + query + "%";

    if (!group.empty()) {
        sql = "SELECT raw_json FROM messages WHERE group_name = ? AND text LIKE ? ORDER BY timestamp DESC LIMIT ?;";
    } else if (channel == "global") {
        sql = "SELECT raw_json FROM messages WHERE channel = 'global' AND text LIKE ? ORDER BY timestamp DESC LIMIT ?;";
    } else {
        sql = "SELECT raw_json FROM messages WHERE text LIKE ? ORDER BY timestamp DESC LIMIT ?;";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    if (!group.empty()) {
        sqlite3_bind_text(stmt, 1, group.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(limit));
    } else if (channel == "global") {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
    } else {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (raw) {
            try {
                list.push_back(json::parse(raw));
            } catch (...) {}
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

bool StorageManager::recall_message(const std::string& msg_id,
                                    const std::string& operator_name,
                                    bool is_admin,
                                    std::string& err_msg,
                                    json& recalled_msg_out) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_ || msg_id.empty()) {
        err_msg = "无效的消息 ID";
        return false;
    }

    const char* select_sql = "SELECT from_user, to_user, group_name, channel, text, timestamp, raw_json FROM messages WHERE msg_id = ? LIMIT 1;";
    sqlite3_stmt* select_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        err_msg = "数据库查询失败";
        return false;
    }
    sqlite3_bind_text(select_stmt, 1, msg_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(select_stmt) != SQLITE_ROW) {
        sqlite3_finalize(select_stmt);
        err_msg = "消息不存在或已被清理";
        return false;
    }

    const char* from_ptr = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 0));
    const char* to_ptr = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 1));
    const char* group_ptr = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 2));
    const char* channel_ptr = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 3));
    const char* text_ptr = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 4));
    const int64_t ts = sqlite3_column_int64(select_stmt, 5);

    const std::string from_user = from_ptr ? from_ptr : "";
    const std::string to_user = to_ptr ? to_ptr : "";
    const std::string group_name = group_ptr ? group_ptr : "";
    const std::string channel = channel_ptr ? channel_ptr : "global";
    const std::string text = text_ptr ? text_ptr : "";

    sqlite3_finalize(select_stmt);

    if (text == "[消息已撤回]") {
        err_msg = "该消息已被撤回";
        return false;
    }

    const int64_t now = now_ms();
    constexpr int64_t kRecallWindowMs = 120 * 1000; // 2 分钟 (120 秒)
    if (!is_admin && (now - ts > kRecallWindowMs)) {
        err_msg = "发送已超过 2 分钟，无法撤回";
        return false;
    }

    if (!is_admin && from_user != operator_name) {
        err_msg = "只能撤回自己发送的消息";
        return false;
    }

    json updated_payload{
        {"type", "recall"},
        {"msg_id", msg_id},
        {"operator", operator_name},
        {"from", from_user},
        {"channel", channel},
        {"ts", now}
    };
    if (!to_user.empty()) updated_payload["to"] = to_user;
    if (!group_name.empty()) updated_payload["group"] = group_name;

    const std::string updated_raw = updated_payload.dump();

    const char* update_sql = "UPDATE messages SET text = '[消息已撤回]', raw_json = ? WHERE msg_id = ?;";
    sqlite3_stmt* update_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
        err_msg = "更新撤回状态失败";
        return false;
    }
    sqlite3_bind_text(update_stmt, 1, updated_raw.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt, 2, msg_id.c_str(), -1, SQLITE_TRANSIENT);

    const int update_res = sqlite3_step(update_stmt);
    sqlite3_finalize(update_stmt);

    if (update_res != SQLITE_DONE) {
        err_msg = "撤回保存失败";
        return false;
    }

    std::cout << "[DB:RECALL] msg_id=" << msg_id << " by " << operator_name << " in [" << channel << "]\n";
    recalled_msg_out = std::move(updated_payload);
    return true;
}

// ========== 私聊与未读消息管理 ==========

std::map<std::string, int> StorageManager::get_unread_counts(const std::string& to_user) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::map<std::string, int> counts;
    if (!db_ || to_user.empty()) return counts;

    const char* sql = "SELECT from_user, COUNT(*) FROM messages WHERE to_user = ? AND is_read = 0 GROUP BY from_user;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return counts;
    }

    sqlite3_bind_text(stmt, 1, to_user.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const int cnt = sqlite3_column_int(stmt, 1);
        if (sender) {
            counts[sender] = cnt;
        }
    }
    sqlite3_finalize(stmt);
    return counts;
}

std::vector<json> StorageManager::get_unread_dms(const std::string& to_user) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<json> dms;
    if (!db_ || to_user.empty()) return dms;

    const char* sql = "SELECT raw_json FROM messages WHERE to_user = ? AND is_read = 0 ORDER BY timestamp ASC, id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return dms;
    }

    sqlite3_bind_text(stmt, 1, to_user.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (raw) {
            try {
                dms.push_back(json::parse(raw));
            } catch (...) {}
        }
    }
    sqlite3_finalize(stmt);
    return dms;
}

bool StorageManager::mark_dms_read(const std::string& to_user, const std::string& from_user) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_ || to_user.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    if (from_user.empty()) {
        const char* sql = "UPDATE messages SET is_read = 1 WHERE to_user = ? AND is_read = 0;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, to_user.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        const char* sql = "UPDATE messages SET is_read = 1 WHERE to_user = ? AND from_user = ? AND is_read = 0;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, to_user.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, from_user.c_str(), -1, SQLITE_TRANSIENT);
    }

    const int step_res = sqlite3_step(stmt);
    const int affected = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    if (step_res == SQLITE_DONE && affected > 0) {
        std::cout << "[DB:READ] " << to_user << " marked " << affected << " DMs from " << (from_user.empty() ? "all" : from_user) << " as read\n";
    }
    return step_res == SQLITE_DONE;
}

std::vector<StorageManager::DbMessageRecord> StorageManager::get_db_messages(int64_t since_id, std::size_t limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<DbMessageRecord> list;
    if (!db_ || limit == 0) return list;

    std::string sql;
    if (since_id > 0) {
        sql = "SELECT id, msg_id, msg_type, from_user, to_user, group_name, channel, text, voice_url, duration, image_url, timestamp, is_read "
              "FROM messages WHERE id > ? ORDER BY id ASC LIMIT ?;";
    } else {
        sql = "SELECT id, msg_id, msg_type, from_user, to_user, group_name, channel, text, voice_url, duration, image_url, timestamp, is_read "
              "FROM messages ORDER BY id DESC LIMIT ?;";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    if (since_id > 0) {
        sqlite3_bind_int64(stmt, 1, since_id);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
    } else {
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DbMessageRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        const char* c_msg_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* c_msg_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* c_from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* c_to = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const char* c_group = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const char* c_channel = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const char* c_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        const char* c_voice = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        rec.duration = sqlite3_column_int(stmt, 9);
        const char* c_image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        rec.timestamp = sqlite3_column_int64(stmt, 11);
        rec.is_read = sqlite3_column_int(stmt, 12);

        if (c_msg_id) rec.msg_id = c_msg_id;
        if (c_msg_type) rec.msg_type = c_msg_type;
        if (c_from) rec.from_user = c_from;
        if (c_to) rec.to_user = c_to;
        if (c_group) rec.group_name = c_group;
        if (c_channel) rec.channel = c_channel;
        if (c_text) rec.text = c_text;
        if (c_voice) rec.voice_url = c_voice;
        if (c_image) rec.image_url = c_image;
        rec.is_recalled = (rec.text == "[消息已撤回]");

        list.push_back(std::move(rec));
    }

    sqlite3_finalize(stmt);
    if (since_id <= 0) {
        std::reverse(list.begin(), list.end());
    }
    return list;
}

// ========== 历史数据平滑迁移 ==========

void StorageManager::migrate_from_json_files(const std::string& users_json_path,
                                             const std::string& messages_jsonl_path) {
    // 1. 迁移 users.json
    if (user_count() == 0 && std::filesystem::exists(users_json_path)) {
        try {
            std::ifstream file(users_json_path);
            if (file) {
                json j;
                file >> j;
                if (j.is_array()) {
                    for (const auto& item : j) {
                        UserAccount acc = UserAccount::from_json(item);
                        if (!acc.username.empty()) {
                            save_user(acc);
                        }
                    }
                    std::cout << "[Storage] Migrated " << j.size() << " users from " << users_json_path << " into SQLite.\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Storage] Warning: failed migrating users from " << users_json_path << ": " << e.what() << '\n';
        }
    }

    // 2. 迁移 messages.jsonl
    if (message_count() == 0 && std::filesystem::exists(messages_jsonl_path)) {
        try {
            std::ifstream file(messages_jsonl_path);
            std::string line;
            std::size_t migrated_count = 0;
            execute_sql("BEGIN TRANSACTION;");
            while (std::getline(file, line)) {
                if (line.empty()) continue;
                try {
                    json msg = json::parse(line);
                    // 历史消息默认标为已读 is_read = 1
                    save_message(msg, 1);
                    ++migrated_count;
                } catch (...) {}
            }
            execute_sql("COMMIT;");
            if (migrated_count > 0) {
                std::cout << "[Storage] Migrated " << migrated_count << " messages from " << messages_jsonl_path << " into SQLite.\n";
            }
        } catch (const std::exception& e) {
            execute_sql("ROLLBACK;");
            std::cerr << "[Storage] Warning: failed migrating messages from " << messages_jsonl_path << ": " << e.what() << '\n';
        }
    }
}

} // namespace chat
