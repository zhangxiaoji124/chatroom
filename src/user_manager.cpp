#include "chat/user_manager.hpp"
#include "chat/storage.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string_view>

namespace chat {

namespace {

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t ep0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t ep1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t sig0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t sig1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

std::string sha256_hex(const std::string& input) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xff));
    }

    uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t W[64];
        for (int i = 0; i < 16; ++i) {
            W[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            W[i] = sig1(W[i - 2]) + W[i - 7] + sig0(W[i - 15]) + W[i - 16];
        }

        uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
        uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + ep1(e) + ch(e, f, g) + K[i] + W[i];
            uint32_t t2 = ep0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }

    std::ostringstream oss;
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(8) << H[i];
    }
    return oss.str();
}

std::string random_hex(size_t length) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    static const char hex_digits[] = "0123456789abcdef";
    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        s.push_back(hex_digits[rng() % 16]);
    }
    return s;
}

std::string normalize_recovery_key(std::string_view key) {
    std::string norm;
    norm.reserve(key.size());
    for (char c : key) {
        if (c != '-' && c != ' ' && !std::isspace(static_cast<unsigned char>(c))) {
            norm.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return norm;
}

bool keys_match(std::string_view stored, std::string_view provided) {
    auto norm_s = normalize_recovery_key(stored);
    auto norm_p = normalize_recovery_key(provided);
    if (norm_s.empty() || norm_p.empty()) return false;
    if (norm_s == norm_p) return true;
    if (norm_s.rfind("REC", 0) == 0 && norm_s.substr(3) == norm_p) return true;
    if (norm_p.rfind("REC", 0) == 0 && norm_p.substr(3) == norm_s) return true;
    return false;
}

} // namespace

std::string UserManager::generate_recovery_key() {
    static const char charset[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    static thread_local std::mt19937_64 rng(std::random_device{}());
    const size_t max_idx = sizeof(charset) - 2;

    std::string key = "REC-";
    for (int i = 0; i < 4; ++i) key += charset[rng() % max_idx];
    key += '-';
    for (int i = 0; i < 4; ++i) key += charset[rng() % max_idx];
    return key;
}

UserManager::UserManager(std::string accounts_file)
    : accounts_file_(std::move(accounts_file)) {
    load_accounts_locked();
}

void UserManager::set_storage(StorageManager* storage) {
    std::lock_guard<std::mutex> lock(mutex_);
    storage_ = storage;
    if (!storage_) return;

    // 从 SQLite 读取用户账号并合并入内存缓存
    auto db_users = storage_->get_all_users();
    for (auto& u : db_users) {
        if (u.recovery_key.empty()) {
            u.recovery_key = generate_recovery_key();
            storage_->save_user(u);
        }
        if (!accounts_.contains(u.username)) {
            accounts_[u.username] = u;
        } else if (accounts_[u.username].recovery_key.empty()) {
            accounts_[u.username].recovery_key = u.recovery_key;
        }
    }
    // 如果内存中有但 SQLite 中没有，则同步入 SQLite
    for (const auto& [_, acc] : accounts_) {
        storage_->save_user(acc);
    }
}

std::vector<UserAccount> UserManager::all_accounts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UserAccount> result;
    result.reserve(accounts_.size());
    for (const auto& [_, acc] : accounts_) {
        result.push_back(acc);
    }
    return result;
}

void UserManager::load_accounts_locked() {
    if (accounts_file_.empty()) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(accounts_file_, ec)) {
        return;
    }
    std::ifstream file(accounts_file_);
    if (!file.is_open()) {
        return;
    }
    try {
        json j;
        file >> j;
        if (j.is_array()) {
            for (const auto& item : j) {
                UserAccount acc = UserAccount::from_json(item);
                if (!acc.username.empty()) {
                    if (acc.recovery_key.empty()) {
                        acc.recovery_key = generate_recovery_key();
                    }
                    accounts_[acc.username] = acc;
                }
            }
        }
    } catch (...) {
        // Ignore parse errors on corrupt file
    }
}

void UserManager::save_accounts_locked() {
    if (accounts_file_.empty()) {
        return;
    }
    try {
        const auto parent = std::filesystem::path(accounts_file_).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        json arr = json::array();
        for (const auto& [_, acc] : accounts_) {
            arr.push_back(acc.to_json());
        }
        std::ofstream file(accounts_file_);
        if (file.is_open()) {
            file << arr.dump(2);
        }
    } catch (...) {
        // Handle filesystem exceptions safely
    }
}

bool UserManager::register_account(const std::string& username,
                                   const std::string& password,
                                   std::string& err_msg,
                                   std::string* out_recovery_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (username.empty() || username.size() > 32) {
        err_msg = "用户名长度必须在 1 到 32 个字符之间";
        return false;
    }
    if (password.size() < 3) {
        err_msg = "密码长度至少需要 3 个字符";
        return false;
    }
    if (accounts_.contains(username)) {
        err_msg = "用户名已被注册，请直接登录";
        return false;
    }

    std::string salt = random_hex(16);
    std::string hash = sha256_hex(password + salt);
    std::string recovery_key = generate_recovery_key();
    int64_t now = now_ms();

    UserAccount acc{
        .username = username,
        .password_hash = hash,
        .salt = salt,
        .created_at = now,
        .last_login = now,
        .recovery_key = recovery_key
    };
    accounts_[username] = acc;
    if (storage_) {
        storage_->save_user(acc);
    }
    save_accounts_locked();
    if (out_recovery_key) {
        *out_recovery_key = recovery_key;
    }
    return true;
}

bool UserManager::authenticate_account(const std::string& username,
                                       const std::string& password,
                                       std::string& err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accounts_.find(username);
    if (it == accounts_.end()) {
        err_msg = "用户账号不存在，请先注册";
        return false;
    }

    std::string hash = sha256_hex(password + it->second.salt);
    if (hash != it->second.password_hash) {
        err_msg = "密码错误，请重试";
        return false;
    }

    const int64_t now = now_ms();
    it->second.last_login = now;
    if (storage_) {
        storage_->update_last_login(username, now);
    }
    save_accounts_locked();
    return true;
}

bool UserManager::reset_password(const std::string& username,
                                 const std::string& new_password,
                                 std::string& err_msg) {
    if (new_password.size() < 3) {
        err_msg = "密码长度至少需要 3 个字符";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accounts_.find(username);
    if (it == accounts_.end()) {
        err_msg = "用户账号不存在";
        return false;
    }

    std::string salt = random_hex(16);
    std::string hash = sha256_hex(new_password + salt);
    it->second.salt = salt;
    it->second.password_hash = hash;
    if (storage_) {
        storage_->save_user(it->second);
    }
    save_accounts_locked();
    return true;
}

bool UserManager::verify_and_reset_password(const std::string& username,
                                            const std::string& recovery_key,
                                            const std::string& new_password,
                                            std::string& err_msg) {
    if (new_password.size() < 3) {
        err_msg = "新密码长度至少需要 3 个字符";
        return false;
    }
    if (recovery_key.empty()) {
        err_msg = "密保恢复码不能为空";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accounts_.find(username);
    if (it == accounts_.end()) {
        err_msg = "用户账号不存在";
        return false;
    }
    if (it->second.recovery_key.empty() || !keys_match(it->second.recovery_key, recovery_key)) {
        err_msg = "密保恢复码不匹配，请核对后重试";
        return false;
    }

    std::string salt = random_hex(16);
    std::string hash = sha256_hex(new_password + salt);
    it->second.salt = salt;
    it->second.password_hash = hash;
    if (storage_) {
        storage_->save_user(it->second);
    }
    save_accounts_locked();

    // 注销所有有效 Token，强制使用新密码重新登录
    for (auto t_it = tokens_.begin(); t_it != tokens_.end(); ) {
        if (t_it->second == username) {
            token_expiry_.erase(t_it->first);
            t_it = tokens_.erase(t_it);
        } else {
            ++t_it;
        }
    }
    return true;
}

std::string UserManager::get_recovery_key(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accounts_.find(username);
    if (it != accounts_.end()) {
        return it->second.recovery_key;
    }
    return "";
}

bool UserManager::delete_account(const std::string& username, std::string& err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accounts_.find(username);
    if (it == accounts_.end()) {
        err_msg = "用户账号不存在";
        return false;
    }
    accounts_.erase(it);
    save_accounts_locked();

    // Revoke tokens
    for (auto t_it = tokens_.begin(); t_it != tokens_.end(); ) {
        if (t_it->second == username) {
            token_expiry_.erase(t_it->first);
            t_it = tokens_.erase(t_it);
        } else {
            ++t_it;
        }
    }
    return true;
}

bool UserManager::has_account(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_.contains(username);
}

size_t UserManager::account_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_.size();
}

std::string UserManager::issue_token(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = random_hex(32);
    tokens_[token] = username;
    // 默认 7 天有效期
    token_expiry_[token] = now_ms() + (7LL * 24LL * 3600LL * 1000LL);
    return token;
}

std::string UserManager::verify_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token.empty()) {
        return {};
    }
    auto it = tokens_.find(token);
    if (it == tokens_.end()) {
        return {};
    }
    auto exp_it = token_expiry_.find(token);
    if (exp_it != token_expiry_.end() && exp_it->second < now_ms()) {
        // Expired
        tokens_.erase(it);
        token_expiry_.erase(exp_it);
        return {};
    }
    return it->second;
}

void UserManager::revoke_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_.erase(token);
    token_expiry_.erase(token);
}

bool UserManager::user_online(const std::string& nickname, const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (nickname.empty() || users_.contains(nickname)) {
        return false;
    }
    int64_t ts = now_ms();
    users_[nickname] = OnlineUser{nickname, address, ts};
    online_since_[nickname] = ts;
    return true;
}

bool UserManager::user_offline(const std::string& nickname) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(nickname);
    if (it == users_.end()) {
        return false;
    }
    users_.erase(it);
    online_since_.erase(nickname);
    return true;
}

bool UserManager::is_online(const std::string& nickname) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.contains(nickname);
}

std::vector<OnlineUser> UserManager::online_users() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OnlineUser> result;
    result.reserve(users_.size());
    for (const auto& [_, u] : users_) {
        result.push_back(u);
    }
    return result;
}

size_t UserManager::online_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.size();
}

void UserManager::log_online(const OnlineUser& u) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[上线] " << u.nickname << " (" << u.address << ") at " << u.connect_ts << "\n";
}

void UserManager::log_offline(const std::string& nickname, int64_t duration_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(nickname);
    if (it != users_.end()) {
        std::cout << "[离线] " << nickname << " (" << it->second.address << ")，在线时长 " << (duration_ms / 1000) << "s\n";
    } else {
        std::cout << "[离线] " << nickname << "，在线时长 " << (duration_ms / 1000) << "s\n";
    }
}

} // namespace chat
