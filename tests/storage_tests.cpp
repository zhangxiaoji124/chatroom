#include "chat/storage.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace chat;

void test_storage_basic() {
    const std::string test_db = "build/test_storage.db";
    std::filesystem::remove(test_db);

    {
        StorageManager storage(test_db);
        assert(storage.init() && "Storage init should succeed");

        // 1. User operations
        assert(storage.user_count() == 0);
        UserAccount u1{"alice", "hash1", "salt1", 1000, 1000};
        UserAccount u2{"bob", "hash2", "salt2", 2000, 2000};
        assert(storage.save_user(u1));
        assert(storage.save_user(u2));
        assert(storage.user_count() == 2);

        auto fetched_alice = storage.get_user("alice");
        assert(fetched_alice.has_value());
        assert(fetched_alice->username == "alice");
        assert(fetched_alice->password_hash == "hash1");

        assert(storage.update_last_login("alice", 5000));
        fetched_alice = storage.get_user("alice");
        assert(fetched_alice->last_login == 5000);

        auto all_users = storage.get_all_users();
        assert(all_users.size() == 2);
        std::cout << "[PASS] User operations passed\n";

        // 2. Message operations
        json msg1{{"type", "chat"}, {"from", "alice"}, {"text", "hello world"}, {"ts", 100}};
        json msg2{{"type", "chat"}, {"from", "bob"}, {"text", "hi alice",}, {"ts", 200}};
        json group_msg{{"type", "group_chat"}, {"from", "alice"}, {"group", "dev"}, {"text", "dev msg"}, {"ts", 300}};
        
        assert(storage.save_message(msg1, 1));
        assert(storage.save_message(msg2, 1));
        assert(storage.save_message(group_msg, 1));
        assert(storage.message_count() == 3);

        auto recent_all = storage.get_recent_messages(10);
        assert(recent_all.size() == 3);
        assert(recent_all[0]["text"] == "hello world");
        assert(recent_all[2]["text"] == "dev msg");

        auto dev_msgs = storage.get_recent_messages(10, "", "dev");
        assert(dev_msgs.size() == 1);
        assert(dev_msgs[0]["text"] == "dev msg");

        auto global_msgs = storage.get_recent_messages(10, "global");
        assert(global_msgs.size() == 2);
        std::cout << "[PASS] Message operations passed\n";

        // 3. Offline DMs and unread management
        json dm1{{"type", "dm"}, {"from", "alice"}, {"to", "bob"}, {"text", "Are you there?"}, {"ts", 400}};
        json dm2{{"type", "dm"}, {"from", "alice"}, {"to", "bob"}, {"text", "Ping 2"}, {"ts", 500}};
        json dm3{{"type", "dm"}, {"from", "charlie"}, {"to", "bob"}, {"text", "Hi from charlie"}, {"ts", 600}};
        
        // Save as unread (is_read = 0)
        assert(storage.save_message(dm1, 0));
        assert(storage.save_message(dm2, 0));
        assert(storage.save_message(dm3, 0));

        auto unread_counts = storage.get_unread_counts("bob");
        assert(unread_counts["alice"] == 2);
        assert(unread_counts["charlie"] == 1);

        auto unread_dms = storage.get_unread_dms("bob");
        assert(unread_dms.size() == 3);
        assert(unread_dms[0]["text"] == "Are you there?");
        assert(unread_dms[1]["text"] == "Ping 2");
        assert(unread_dms[2]["text"] == "Hi from charlie");

        // Mark alice's DMs as read
        assert(storage.mark_dms_read("bob", "alice"));
        unread_counts = storage.get_unread_counts("bob");
        assert(unread_counts.find("alice") == unread_counts.end());
        assert(unread_counts["charlie"] == 1);

        // Mark remaining
        assert(storage.mark_dms_read("bob", ""));
        unread_counts = storage.get_unread_counts("bob");
        assert(unread_counts.empty());
        std::cout << "[PASS] Unread and DM operations passed\n";
    }

    std::error_code ec;
    std::filesystem::remove(test_db, ec);
    std::filesystem::remove(test_db + "-wal", ec);
    std::filesystem::remove(test_db + "-shm", ec);
}

int main() {
    std::cout << "Running storage unit tests...\n";
    test_storage_basic();
    std::cout << "All storage unit tests PASSED successfully!\n";
    return 0;
}
