#include "chat/rate_limiter.hpp"
#include <algorithm>

namespace chat {

RateLimiter::RateLimiter(int64_t window_ms,
                         std::size_t max_requests,
                         std::size_t burst_capacity)
    : window_ms_(window_ms),
      max_requests_(max_requests),
      burst_capacity_(burst_capacity) {
}

bool RateLimiter::allow_message(const std::string& key) {
    if (key.empty()) return true;

    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = now_ms();
    auto& timestamps = message_timestamps_[key];

    // 清理窗口外的过期时间戳
    const int64_t cutoff = now - window_ms_;
    while (!timestamps.empty() && timestamps.front() < cutoff) {
        timestamps.pop_front();
    }

    if (timestamps.size() >= max_requests_) {
        return false;
    }

    timestamps.push_back(now);

    // 适度维护内存：如果映射过大，清理空条目
    if (message_timestamps_.size() > 5000) {
        for (auto it = message_timestamps_.begin(); it != message_timestamps_.end(); ) {
            if (it->second.empty() || it->second.back() < cutoff) {
                it = message_timestamps_.erase(it);
            } else {
                ++it;
            }
        }
    }

    return true;
}

bool RateLimiter::allow_login_attempt(const std::string& key) {
    if (key.empty()) return true;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = login_failures_.find(key);
    if (it == login_failures_.end()) {
        return true;
    }

    const int64_t now = now_ms();
    if (it->second.locked_until_ms > now) {
        return false;
    }

    // 锁定已过期，重置失败次数
    if (it->second.locked_until_ms != 0 && it->second.locked_until_ms <= now) {
        it->second.failure_count = 0;
        it->second.locked_until_ms = 0;
    }

    return true;
}

void RateLimiter::record_login_failure(const std::string& key, int max_failures, int64_t lock_seconds) {
    if (key.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = now_ms();
    auto& rec = login_failures_[key];

    // 若之前已锁定但时间已过，重新计数
    if (rec.locked_until_ms != 0 && rec.locked_until_ms <= now) {
        rec.failure_count = 0;
        rec.locked_until_ms = 0;
    }

    rec.failure_count++;
    if (rec.failure_count >= max_failures) {
        rec.locked_until_ms = now + lock_seconds * 1000;
    }
}

void RateLimiter::reset_login_failure(const std::string& key) {
    if (key.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    login_failures_.erase(key);
}

int64_t RateLimiter::get_login_locked_remaining_seconds(const std::string& key) const {
    if (key.empty()) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = login_failures_.find(key);
    if (it == login_failures_.end()) {
        return 0;
    }

    const int64_t now = now_ms();
    if (it->second.locked_until_ms > now) {
        return (it->second.locked_until_ms - now + 999) / 1000;
    }

    return 0;
}

} // namespace chat
