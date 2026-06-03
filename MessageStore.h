#ifndef CHATROOM_MESSAGE_STORE_H
#define CHATROOM_MESSAGE_STORE_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

class MessageStore {
public:
    explicit MessageStore(const std::filesystem::path& dbPath, size_t connectionCount = 4);
    ~MessageStore();

    MessageStore(const MessageStore&) = delete;
    MessageStore& operator=(const MessageStore&) = delete;

    bool isReady() const;

    void saveMessage(int senderId, const std::string& senderName, const std::string& text, const std::string& msgType);

    void saveEvent(const std::string& eventType, const std::string& detail);

    std::size_t totalWritten() const;

    std::size_t totalFailed() const;

    double avgWriteMs() const;

    long long maxWriteMs() const;

    std::string statsText() const;

    std::filesystem::path dbPath() const;

private:
    sqlite3* acquireConnection();
    void releaseConnection(sqlite3* conn);
    bool initSchema(sqlite3* conn);

    std::filesystem::path dbFilePath;
    std::vector<sqlite3*> connections;
    std::vector<bool> connectionBusy;
    std::mutex poolMutex;
    std::condition_variable poolCondition;
    std::atomic<bool> ready{false};

    std::atomic<std::size_t> writeCount{0};
    std::atomic<std::size_t> failCount{0};
    std::atomic<long long> totalWriteMicros{0};
    std::atomic<long long> maxWriteMicros{0};
};

#endif
