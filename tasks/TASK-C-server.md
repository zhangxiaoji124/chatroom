# 任务 C：实现协调层 server.cpp + main.cpp（Antigravity）

工作目录：`D:\chatroom`
你要创建/修改：
- `D:\chatroom\src\server.cpp`
- `D:\chatroom\src\main.cpp`

## 背景
C++20 WebSocket 聊天室。两个底层模块已实现并有清晰接口：
- `include/chat/user_manager.hpp` / `src/user_manager.cpp`：在线用户管理（上下线、日志）
- `include/chat/ws_handler.hpp`：WebSocket 连接注册 + 广播（`ws_handler.cpp` 由另一任务实现，其接口已定）
- `include/chat/server.hpp`：ChatServer 类（协调层）声明
- `include/chat/types.hpp`：OnlineUser / ChatMessage / now_ms / json

依赖 `third_party/httplib.h`（cpp-httplib 0.18.1，内置 WebSocket）。

## 你要实现的内容

### ChatServer 类（src/server.cpp，对应 include/chat/server.hpp）

1. 构造函数/析构函数。
2. `setup_routes()`：
   - 调用 `ws_.set_message_sink(...)`，把收到的 ChatMessage 交给 `persist_and_log`。

3. `run()`：启动 httplib::Server：
   - `GET /` → 纯文本提示 "Chatroom Server Running. Use /ws for WebSocket."
   - `GET /api/status` → JSON `{"status":"ok","online_count":N,"ws_connections":M}`
   - `GET /ws` → WebSocket 路由，完整处理**登录/聊天/断开**三种帧：

   **客户端协议（JSON 文本帧）：**
   - 上线：客户端发 `{"type":"login","nickname":"张三"}`
     - 服务端调 `users_.user_online(nick, addr)`，addr 用 `req.remote_addr + ":" + req.remote_port`。
     - 成功：调 `users_.log_online(u)` 打印上线日志；`ws_.register_ws(nick, ws)`；广播 `{"type":"system","msg":"张三 上线了"}`。
     - 失败（昵称占用）：回 `{"type":"error","msg":"昵称已被占用"}`。
   - 聊天：客户端发 `{"type":"chat","text":"你好"}`
     - 调 `ws_.handle_text(nick, text)`（内部会落库+打印+广播）。
   - 断开（set_close_handler）：若已登录，`users_.log_offline` 打印离线日志、`ws_.unregister_ws`、广播 `{"type":"system","msg":"张三 离线了"}`。

   **注意 cpp-httplib WebSocket API（0.18.1）：**
   - `auto ws = req.accept_ws();`
   - `ws->set_open_handler([](const Request&){...})`
   - `ws->set_message_handler([](const std::string& payload, DataSink* sink){...})`
   - `ws->set_close_handler([](const Request&, const std::string& reason){...})`
   - 发送文本：`ws->send(string)`。
   - 广播给所有连接：遍历 `ws_` 的连接。WsHandler 已提供 `broadcast(ChatMessage)`，但**系统通知（上线/离线）需要单独广播 JSON 字符串**——如果 WsHandler 没有 `broadcast_raw(string)` 方法，请在 server.cpp 里用 std::lock_guard<std::mutex> 保护并直接广播，或给 WsHandler 加一个 `broadcast_raw` 方法（在 ws_handler.hpp 声明 + ws_handler.cpp 实现）。**优先方案：给 WsHandler 增加 `broadcast_raw(const std::string&)` 方法**，接收任意 JSON 字符串广播给所有连接。

4. `persist_and_log(msg)`：追加 `msg.to_json().dump()+"\n"` 到 `data/messages.jsonl`（用 `<fstream>` `<filesystem>`，不存在则创建目录），并 `std::cout << "[消息] " << from << ": " << text << "\n"`。

### main.cpp（src/main.cpp）
- `int main()`：创建 `chat::ChatServer server(8080)`，调用 `server.run()`。
- `server.run()` 阻塞，按 Ctrl+C 退出。

## 涉及给 WsHandler 的补充
按上面"优先方案"，需要给 `include/chat/ws_handler.hpp` + `src/ws_handler.cpp` 增加 `broadcast_raw(const std::string&) const` 方法。**你只需要在 server.cpp 里调用它，并把增加的声明写到 ws_handler.hpp 里**（一个方法声明），实现留给另一个 agent；如果编译报未定义，可临时在 src 里加一个最小实现。

## 构建验证
```
cd D:\chatroom
mingw32-make -j4
```
必须全部编译链接通过，生成 `build/chatroom_server.exe`。完成后报告：
- ChatServer 各方法实现要点
- 你给 WsHandler 增加了什么方法
- 编译/链接结果
