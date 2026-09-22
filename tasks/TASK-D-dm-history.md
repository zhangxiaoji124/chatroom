# 任务 D：私聊 DM + 在线用户列表 + 历史消息加载（Antigravity）

工作目录：`D:\chatroom`
你要修改的文件：
- `D:\chatroom\src\server.cpp`（服务端：新增私聊、在线列表、历史接口）
- `D:\chatroom\include\chat\server.hpp`（如需加方法）
- `D:\chatroom\web\index.html`（前端：私聊 + 在线列表 UI）
- `D:\chatroom\web\app.js`（前端逻辑）
- `D:\chatroom\web\style.css`（样式）

## 背景
C++20 WebSocket 聊天室已有：全局聊天、表情、群组、AI助手(DeepSeek)、语音消息、管理后台。
`src/server.cpp` 的 RuntimeState 有 `connections`（昵称->WebSocket*）映射和 `memberships`/`groups`。
`WsHandler` 提供 `send_raw(nickname, json_str)` 定向发送和 `broadcast_raw(json_str)` 广播。

## 你要实现的功能

### 1. 私聊（DM）
**服务端 WebSocket 协议新增：**
- 客户端发 `{"type":"dm","to":"张三","text":"你好"}` 
  - 校验：`to` 必须是非空字符串且存在于 `state.connections`（在线用户）；否则回 `{"type":"error","msg":"对方不在线"}`。
  - 成功：定向发送给接收方 `{"type":"dm","from":当前昵称,"text":"...","ts":...}`，同时**回显给发送方**相同格式（`to` 字段），发送方前端据此区分左右气泡。
- 服务端**不落库**（本轮不做 DM 持久化，聚焦实时转发）。

**前端 UI（index.html + app.js）：**
- 聊天区左侧新增**在线用户列表**侧栏（`aside#user-list`）：显示所有在线用户（点击昵称可发起私聊）+ "全局频道"入口。
- 点击某用户 → 进入与该用户的**私聊模式**：
  - 顶部当前频道显示「私聊：张三」
  - 输入框发送的消息走 `dm` 协议
  - 私聊时群组按钮/全局输入隐藏，提供"返回全局"按钮
- 接收私聊消息时：若当前不在该私聊，也要**在界面上提示**（例如聊天区内出现一条 `🔒 来自张三的私聊消息`，点击可跳转）。

**服务端如何知道在线用户列表？** 从 `users_.online_users()`（UserManager）拿，新增 `GET /api/online` 返回 JSON 数组 `[{"nickname":"张三","address":"...","connect_ts":123}]`。前端登录后轮询该接口刷新列表。

### 2. 历史消息加载
**服务端新增 API：**
- `GET /api/history?limit=N`（N 默认 100，上限 1000）：
  - 返回 data/messages.jsonl 最近 N 条（复用现成 `recent_messages(limit)` 函数逻辑）。
  - 格式：`[{"type":"chat","from":"张三","text":"你好","ts":123}, ...]`

**前端：**
- 登录成功后：先调用 `/api/history?limit=50` 渲染最近 50 条历史消息（显示在消息区顶部，早的在上）。
- 之后再通过 WebSocket 接收实时消息并追加。

### 3. 在线用户状态广播
- 服务端在用户上线/离线时，除现有广播外，再广播 `{"type":"users","online":["张三","李四",...]}` 给所有连接（可选：或让前端轮询 `/api/online`）。
- **实现建议**：优先做轮询 `/api/online`（简单可靠）；广播方案可选。

## 重要约束
- 不要修改群组(`group`)、AI助手(`@机器人`)、语音(`voice_send`)、表情(`sticker`)相关的现有逻辑。
- 不要修改 `Makefile`、`tests/` 已有测试。
- C++ 代码请遵循现有风格（`RuntimeState` 的 mutex 保护、`ws_.send_raw` 定向发送）。
- 用 `mingw32-make -j4` 编译验证必须通过，生成 `build/chatroom_server.exe`。

## 构建验证
```
cd D:\chatroom
mingw32-make -j4
```
必须全部编译链接通过。完成后报告：各功能实现要点、前端改动、编译结果。
