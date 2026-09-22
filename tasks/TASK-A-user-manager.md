# 任务 A：实现 user_manager.cpp（Antigravity）

工作目录：`D:\chatroom`
你要创建/修改的文件：`D:\chatroom\src\user_manager.cpp`

## 背景
这是一个 C++20 WebSocket 聊天室服务器。接口已在 `D:\chatroom\include\chat\user_manager.hpp` 定义，
你**只能实现 .cpp**，不要改动头文件（那是我协调层定的契约）。

## 要求
实现 `chat::UserManager` 的全部方法：

1. `user_online(nickname, address)` — 昵称非空且唯一才能上线。
   - 成功：记录用户 + 上线时间戳，返回 true。
   - 失败（昵称空/已占用）：返回 false，不记录。

2. `user_offline(nickname)` — 移除用户，返回是否成功。

3. `is_online(nickname)` / `online_users()` / `online_count()` — 线程安全查询。

4. `log_online(u)` — 打印到控制台，格式（用 std::cout）：
   `[上线] 昵称 (地址) at 时间戳ms`
   时间戳 = u.connect_ts。

5. `log_offline(nickname, duration_ms)` — 打印：
   `[离线] 昵称，在线时长 Xs`
   X 秒 = duration_ms / 1000。

## 技术要点
- 线程安全：所有公共方法用 `mutex_` 保护（声明里是 `mutable std::mutex mutex_`）。
- 包含 `<iostream>`、`<chrono>`。
- 用 `online_since_` 记录每个用户上线时间（ms），`users_` 存 OnlineUser。
- OnlineUser 结构体在 `types.hpp`：{ nickname, address, connect_ts }。
- 代码风格：现代 C++20，命名清晰，简短注释。

## 验证
写好代码后，用以下命令确认能编译（仅编译不链接）：
```
cd D:\chatroom
g++ -std=c++20 -Wall -Wextra -Iinclude -Ithird_party -c src/user_manager.cpp -o build/user_manager.o
```
必须无警告无报错。完成后报告你实现了哪些方法、编译结果。
