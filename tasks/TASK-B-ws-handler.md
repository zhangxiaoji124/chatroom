# 任务 B：实现 ws_handler.cpp（WebSocket 广播模块）

工作目录：`D:\chatroom`
文件：`D:\chatroom\src\ws_handler.cpp`

## 背景
C++20 WebSocket 聊天室。`include/chat/ws_handler.hpp` 已定义接口（**不要改头文件**）。
`third_party/httplib.h` 是 **cpp-httplib master 版**（WebSocket 在 `httplib::ws::WebSocket`）。

## 服务器端 WebSocket 模型（必须理解）
新版 cpp-httplib 用阻塞式 read 循环，不是回调式：
```cpp
svr.WebSocket("/ws", [](const httplib::Request&, httplib::ws::WebSocket& ws) {
    std::string msg;
    while (ws.read(msg)) {   // 阻塞读取，返回 httplib::ws::ReadResult
        // 处理 msg（Text 帧）
    }
    // read 返回 0/Fail，连接关闭
});
```
- `ws.read(msg)` 返回 `httplib::ws::ReadResult`（`Text=1, Binary=2, Fail=0`），并填充 msg。
- `ws.send(const std::string&)` 发送文本帧，**线程安全**（内部 write_mutex_）。
- `ws.is_open()` 判断连接仍打开。

## 要求：实现 WsHandler 全部方法

`include/chat/ws_handler.hpp` 里存的是 `void*`（即 `httplib::ws::WebSocket*`）。实现时：
```cpp
#include "chat/ws_handler.hpp"
#include "httplib.h"
static httplib::ws::WebSocket* to_ws(void* p) {
    return static_cast<httplib::ws::WebSocket*>(p);
}
```

1. `set_message_sink(sink)` — 保存 `sink_`。

2. `register_ws(nickname, ws)` / `unregister_ws(nickname)` — 线程安全读写 `conns_`。

3. `handle_text(from, text)`：
   - 构造 `ChatMessage{from, text, now_ms()}`（now_ms 在 types.hpp）。
   - 若 `sink_` 存在，调用 `sink_(msg)`。
   - 调 `broadcast(msg)`。

4. `broadcast(msg)`：
   - `broadcast_raw(msg.to_json().dump())`。

5. `broadcast_raw(json_str)`：
   - 在锁内遍历 `conns_`，对每个 `to_ws(ws)` 调用 `send(json_str)`。
   - `send` 调用必须**在锁外**执行（send 内部有 write_mutex_，避免死锁/卡锁）：先收集 ws 指针列表，释放锁后再逐个 send。失败(返回 false)的从 conns_ 移除。

6. `connection_count()` — 返回 `conns_.size()`。

## 线程安全要点
- `conns_` 的读写用 `mutex_` 保护。
- `send` 在锁外用独立的 ws 指针列表做，防止持锁网络阻塞。
- 移除失败连接时不重复持有读写锁（可用临时集合 + 二次加锁删除）。

## 验证
```
cd D:\chatroom
g++ -std=c++20 -Wall -Wextra -Iinclude -Ithird_party -c src/ws_handler.cpp -o build/ws_handler.o
```
必须无警告无报错。完成后报告实现方法与编译结果。
