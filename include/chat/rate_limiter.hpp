#pragma once
#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <cstdint>
#include "chat/types.hpp"

namespace chat {

// RateLimiter：滑动窗口限流与防暴破安全防护
class RateLimiter {
public:
    // window_ms: 统计窗口（默认 1000ms）
    // max_requests: 窗口内最大允许消息数（默认 5 条）
    // burst_capacity: 突发容限（默认 10 条）
    explicit RateLimiter(int64_t window_ms = 1000,
                         std::size_t max_requests = 5,
                         std::size_t burst_capacity = 10);

    // 检查并记录消息发送，返回 true 表示允许通过，false 表示触发限流
    bool allow_message(const std::string& key);

    // 登录防爆破：检查当前 key（账号或 IP）是否处于锁定状态
    // 返回 true 表示未被锁定允许尝试，false 表示已被封禁锁定
    bool allow_login_attempt(const std::string& key);

    // 记录一次登录失败，若连续失败达到阈值（如 5 次），将锁定指定时长（默认 300 秒）
    void record_login_failure(const std::string& key, int max_failures = 5, int64_t lock_seconds = 300);

    // 登录成功，重置失败计数
    void reset_login_failure(const std::string& key);

    // 获取剩余锁定秒数（若未锁定则返回 0）
    int64_t get_login_locked_remaining_seconds(const std::string& key) const;

private:
    int64_t window_ms_;
    std::size_t max_requests_;
    std::size_t burst_capacity_;

    mutable std::mutex mutex_;
    // 消息发送滑动窗口：key -> 时间戳队列(ms)
    std::unordered_map<std::string, std::deque<int64_t>> message_timestamps_;

    struct LoginFailureRecord {
        int failure_count = 0;
        int64_t locked_until_ms = 0;
    };
    // 登录防爆破记录：key -> 失败统计与封禁到期时间
    std::unordered_map<std::string, LoginFailureRecord> login_failures_;
};

} // namespace chat
