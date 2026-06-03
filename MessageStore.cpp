#include "MessageStore.h"

#include <chrono>
#include <iostream>
#include <sstream>

#include <sqlite3.h>

namespace {

bool execSql(sqlite3* conn, const char* sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(conn, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[db] exec failed: " << (errMsg ? errMsg : "unknown") << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

}  // namespace

MessageStore::MessageStore(const std::filesystem::path& dbPath, size_t connectionCount)
    : dbFilePath(dbPath), connections(connectionCount, nullptr), connectionBusy(connectionCount, false) {
    if (connectionCount == 0) {
        return;
    }

    std::error_code ec;
    if (dbFilePath.has_parent_path()) {
        std::filesystem::create_directories(dbFilePath.parent_path(), ec);
    }

    for (size_t i = 0; i < connectionCount; ++i) {
        sqlite3* conn = nullptr;
        int rc = sqlite3_open(dbFilePath.string().c_str(), &conn);
        if (rc != SQLITE_OK) {
            std::cerr << "[db] open failed: " << sqlite3_errmsg(conn) << std::endl;
            if (conn) {
                sqlite3_close(conn);
            }
            return;
        }

        execSql(conn, "PRAGMA journal_mode=WAL;");
        execSql(conn, "PRAGMA synchronous=NORMAL;");
        execSql(conn, "PRAGMA busy_timeout=5000;");
        if (!initSchema(conn)) {
            sqlite3_close(conn);
            return;
        }
        connections[i] = conn;
    }

    ready.store(true);
    std::cout << "[db] connected: " << dbFilePath.string()
              << " (pool=" << connectionCount << ", WAL mode)" << std::endl;
}

MessageStore::~MessageStore() {
    for (sqlite3* conn : connections) {
        if (conn) {
            sqlite3_close(conn);
        }
    }
}

bool MessageStore::isReady() const {
    return ready.load();
}

bool MessageStore::initSchema(sqlite3* conn) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS chat_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender_id INTEGER,"
        "  sender_name TEXT NOT NULL,"
        "  msg_type TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now','localtime'))"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_chat_messages_created_at ON chat_messages(created_at);"
        "CREATE TABLE IF NOT EXISTS chat_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  event_type TEXT NOT NULL,"
        "  detail TEXT NOT NULL,"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now','localtime'))"
        ");";

    return execSql(conn, sql);
}

sqlite3* MessageStore::acquireConnection() {
    std::unique_lock<std::mutex> lock(poolMutex);
    poolCondition.wait(lock, [this]() {
        for (bool busy : connectionBusy) {
            if (!busy) {
                return true;
            }
        }
        return false;
    });

    for (size_t i = 0; i < connections.size(); ++i) {
        if (!connectionBusy[i] && connections[i] != nullptr) {
            connectionBusy[i] = true;
            return connections[i];
        }
    }
    return nullptr;
}

void MessageStore::releaseConnection(sqlite3* conn) {
    if (!conn) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        for (size_t i = 0; i < connections.size(); ++i) {
            if (connections[i] == conn) {
                connectionBusy[i] = false;
                break;
            }
        }
    }
    poolCondition.notify_one();
}

void MessageStore::saveMessage(int senderId, const std::string& senderName, const std::string& text,
                               const std::string& msgType) {
    if (!ready.load()) {
        failCount.fetch_add(1);
        return;
    }

    auto begin = std::chrono::steady_clock::now();
    sqlite3* conn = acquireConnection();
    if (!conn) {
        failCount.fetch_add(1);
        return;
    }

    const char* sql = "INSERT INTO chat_messages(sender_id, sender_name, msg_type, content) VALUES(?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        failCount.fetch_add(1);
        releaseConnection(conn);
        return;
    }

    sqlite3_bind_int(stmt, 1, senderId);
    sqlite3_bind_text(stmt, 2, senderName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msgType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, text.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    releaseConnection(conn);

    auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();

    if (rc != SQLITE_DONE) {
        failCount.fetch_add(1);
        return;
    }

    writeCount.fetch_add(1);
    totalWriteMicros.fetch_add(elapsedMicros);
    long long prevMax = maxWriteMicros.load();
    while (elapsedMicros > prevMax &&
           !maxWriteMicros.compare_exchange_weak(prevMax, elapsedMicros)) {
    }
}

void MessageStore::saveEvent(const std::string& eventType, const std::string& detail) {
    if (!ready.load()) {
        failCount.fetch_add(1);
        return;
    }

    auto begin = std::chrono::steady_clock::now();
    sqlite3* conn = acquireConnection();
    if (!conn) {
        failCount.fetch_add(1);
        return;
    }

    const char* sql = "INSERT INTO chat_events(event_type, detail) VALUES(?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        failCount.fetch_add(1);
        releaseConnection(conn);
        return;
    }

    sqlite3_bind_text(stmt, 1, eventType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, detail.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    releaseConnection(conn);

    auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();

    if (rc != SQLITE_DONE) {
        failCount.fetch_add(1);
        return;
    }

    writeCount.fetch_add(1);
    totalWriteMicros.fetch_add(elapsedMicros);
    long long prevMax = maxWriteMicros.load();
    while (elapsedMicros > prevMax &&
           !maxWriteMicros.compare_exchange_weak(prevMax, elapsedMicros)) {
    }
}

std::size_t MessageStore::totalWritten() const {
    return writeCount.load();
}

std::size_t MessageStore::totalFailed() const {
    return failCount.load();
}

double MessageStore::avgWriteMs() const {
    std::size_t count = writeCount.load();
    if (count == 0) {
        return 0.0;
    }
    return static_cast<double>(totalWriteMicros.load()) / static_cast<double>(count) / 1000.0;
}

long long MessageStore::maxWriteMs() const {
    return maxWriteMicros.load() / 1000;
}

std::string MessageStore::statsText() const {
    std::ostringstream oss;
    oss << "[db] writes=" << writeCount.load()
        << ", failed=" << failCount.load()
        << ", avg=" << avgWriteMs() << "ms"
        << ", max=" << maxWriteMs() << "ms"
        << ", path=" << dbFilePath.string();
    return oss.str();
}

std::filesystem::path MessageStore::dbPath() const {
    return dbFilePath;
}
