// Chatroom 单元测试
// 覆盖 UserManager 与 WsHandler 的核心逻辑。
// 需要链接: src/user_manager.cpp src/ws_handler.cpp
#include "chat/user_manager.hpp"
#include "chat/ws_handler.hpp"
#include "chat/storage.hpp"
#include "chat/rate_limiter.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace chat;

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        } else {                                                        \
            std::printf("  [ok]   %s\n", #cond);                        \
        }                                                               \
    } while (0)

static void test_user_manager() {
    std::printf("== UserManager ==\n");
    UserManager um;

    // 空昵称 / 非空昵称登录
    CHECK(!um.user_online("", "127.0.0.1:1"));
    CHECK(um.user_online("Alice", "127.0.0.1:10001"));
    CHECK(um.online_count() == 1);
    CHECK(um.is_online("Alice"));

    // 昵称重复
    CHECK(!um.user_online("Alice", "127.0.0.1:10002"));
    CHECK(um.online_count() == 1);

    // 第二个用户
    CHECK(um.user_online("Bob", "127.0.0.1:10003"));
    CHECK(um.online_count() == 2);

    // online_users 列表
    auto users = um.online_users();
    CHECK(users.size() == 2);
    bool has_alice = false, has_bob = false;
    for (const auto& u : users) {
        if (u.nickname == "Alice") has_alice = true;
        if (u.nickname == "Bob") has_bob = true;
    }
    CHECK(has_alice && has_bob);

    // 下线
    CHECK(um.user_offline("Bob"));
    CHECK(um.online_count() == 1);
    CHECK(!um.is_online("Bob"));
    CHECK(!um.user_offline("Nobody"));

    // 日志（格式化输出）
    OnlineUser u{"Carol", "127.0.0.1:9999", 1690000000000LL};
    um.log_online(u);
    um.log_offline("Carol", 1234567);  // => 1234s
}

static void test_ws_handler_basics() {
    std::printf("== WsHandler（注册/计数/移除，无真实连接）==\n");
    WsHandler h;

    CHECK(h.connection_count() == 0);
    // 注册一个空的 void*（不真实发送，仅测映射逻辑）
    h.register_ws("Alice", reinterpret_cast<void*>(0x1));
    h.register_ws("Bob", reinterpret_cast<void*>(0x2));
    CHECK(h.connection_count() == 2);
    h.unregister_ws("Alice");
    CHECK(h.connection_count() == 1);
    h.unregister_ws("Nobody");  // 不影响
    CHECK(h.connection_count() == 1);
}

static void test_ws_handler_sink() {
    std::printf("== WsHandler（sink 消息流）==\n");
    WsHandler h;
    std::vector<ChatMessage> received;
    h.set_message_sink([&](const ChatMessage& m) { received.push_back(m); });

    // 没有真实连接，broadcast 不会崩溃；sink 应当被调用。
    h.handle_text("Alice", "hello");
    CHECK(received.size() == 1);
    if (received.size() == 1) {
        CHECK(received[0].from == "Alice");
        CHECK(received[0].text == "hello");
        CHECK(received[0].ts > 0);
    }

    // 再次调用 sink 依旧生效
    h.handle_text("Bob", "hi there");
    CHECK(received.size() == 2);
}

static void test_user_accounts_and_tokens() {
    std::printf("== UserManager 账号与Token测试 ==\n");
    std::string test_accounts_file = "build/test_users.json";
    std::error_code ec;
    std::filesystem::remove(test_accounts_file, ec);
    UserManager um(test_accounts_file);

    std::string err;
    // 注册账号
    CHECK(um.register_account("DevUser", "secret123", err));
    CHECK(um.account_count() == 1);
    CHECK(um.has_account("DevUser"));

    // 重复注册拒绝
    CHECK(!um.register_account("DevUser", "otherpass", err));

    // 密码认证成功
    CHECK(um.authenticate_account("DevUser", "secret123", err));

    // 密码错误拒绝
    CHECK(!um.authenticate_account("DevUser", "wrongpass", err));

    // Token 签发与校验
    std::string token = um.issue_token("DevUser");
    CHECK(!token.empty());
    CHECK(um.verify_token(token) == "DevUser");

    // 无效 Token 校验
    CHECK(um.verify_token("invalid_token_xyz").empty());

    // 注销 Token
    um.revoke_token(token);
    CHECK(um.verify_token(token).empty());
}

static void test_rate_limiter() {
    std::printf("== RateLimiter 限流与防爆破测试 ==\n");
    RateLimiter rl(1000, 5, 5);

    // 连续发送 5 条消息正常通过
    for (int i = 0; i < 5; ++i) {
        CHECK(rl.allow_message("userA"));
    }
    // 第 6 条消息触发限流
    CHECK(!rl.allow_message("userA"));
    // 另一个用户不受影响
    CHECK(rl.allow_message("userB"));

    // 登录防爆破测试
    const std::string ip = "192.168.1.100";
    CHECK(rl.allow_login_attempt(ip));
    for (int i = 0; i < 4; ++i) {
        rl.record_login_failure(ip, 5, 300);
    }
    CHECK(rl.allow_login_attempt(ip));
    // 第 5 次失败，触发锁定
    rl.record_login_failure(ip, 5, 300);
    CHECK(!rl.allow_login_attempt(ip));
    CHECK(rl.get_login_locked_remaining_seconds(ip) > 0);

    // 成功登录后重置
    rl.reset_login_failure(ip);
    CHECK(rl.allow_login_attempt(ip));
    CHECK(rl.get_login_locked_remaining_seconds(ip) == 0);
}

static void test_storage_search_and_recall() {
    std::printf("== StorageManager 搜索与撤回测试 ==\n");
    std::string db_file = "build/test_storage.db";
    std::error_code ec;
    std::filesystem::remove(db_file, ec);
    StorageManager sm(db_file);
    CHECK(sm.init());

    json m1{{"type", "chat"}, {"from", "Alice"}, {"text", "Hello world and Antigravity"}, {"ts", now_ms()}, {"msg_id", "m_1"}};
    json m2{{"type", "chat"}, {"from", "Bob"}, {"text", "Hi Alice from C++20"}, {"ts", now_ms()}, {"msg_id", "m_2"}};
    sm.save_message(m1, 1);
    sm.save_message(m2, 1);

    // 全文搜索测试
    auto r1 = sm.search_messages("Antigravity");
    CHECK(r1.size() == 1);
    if (!r1.empty()) {
        CHECK(r1[0]["from"] == "Alice");
    }

    auto r2 = sm.search_messages("nonexistent");
    CHECK(r2.empty());

    // 消息撤回测试
    std::string err;
    json recalled;
    // 非本人非管理员撤回被拒绝
    CHECK(!sm.recall_message("m_1", "Bob", false, err, recalled));
    // 本人撤回成功
    CHECK(sm.recall_message("m_1", "Alice", false, err, recalled));
    CHECK(recalled["msg_id"] == "m_1");
    // 已撤回消息不能再次撤回
    CHECK(!sm.recall_message("m_1", "Alice", false, err, recalled));
    // 搜索时已撤回消息不应出现
    auto r3 = sm.search_messages("Antigravity");
    CHECK(r3.empty());
}

int main() {
    test_user_manager();
    test_user_accounts_and_tokens();
    test_ws_handler_basics();
    test_ws_handler_sink();
    test_rate_limiter();
    test_storage_search_and_recall();

    std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    std::printf("failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
