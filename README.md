# Chatroom 实时聊天室（C++20 WebSocket）

基于 C++20 WebSocket 的现代实时聊天室系统，包含高性能 C++ 服务端与渐进式前端交互界面。

## 功能特性

- **用户登录 / 退出**：基于昵称的实时在线状态管理与重复登录校验。
- **全局聊天频道**：支持实时广播消息、时间戳展示与平滑滚动。
- **私聊（DM）**：支持定向私聊对话，侧边栏随时发起并接收私聊提醒。
- **在线用户列表**：基于 `/api/online` 轮询的左侧在线用户面板，点击昵称快捷进入私聊。
- **历史消息加载**：登录后通过 `/api/history` 自动拉取最近 50 条历史消息。
- **群组功能**：支持动态加入/创建群组，独立群聊频道隔离消息。
- **表情包**：内置快捷表情选择面板与表情消息渲染。
- **语音消息**：支持浏览器端录音（WebM/Ogg）、后端上传持久化与语音播放器。
- **AI 助手**：在全局聊天中通过 `@机器人` 或 `@AI` 触发 DeepSeek 大语言模型智能问答。
- **消息管理后台**：独立 `admin.html` 监控系统运行状态与历史消息检索。
- **暗黑主题**：支持系统明暗主题自适应与手动切换。
- **@提及提醒**：支持 `@昵称` 消息高亮、系统音效提示与浏览器标签页闪烁提醒。

## 技术栈

- **服务端**：C++20
- **HTTP/WebSocket 库**：`cpp-httplib` 0.18.1
- **JSON 解析**：`nlohmann-json`
- **构建工具**：MinGW GCC / `mingw32-make`
- **前端**：原生 HTML5 / CSS3 / JavaScript (ES6+)

## 构建方法

使用 MinGW 或 GCC (支持 C++20) 运行构建命令：

```cmd
cd D:\chatroom
mingw32-make -j4
```

编译成功后将在 `build/` 目录下生成可执行文件 `build/chatroom_server.exe`。

## 运行方法

启动服务端程序：

```cmd
.\build\chatroom_server.exe
```

启动后控制台将提示监听端口（默认 8080），在浏览器中打开：
[http://localhost:8080](http://localhost:8080)

## 目录结构说明

```
chatroom/
├── src/                # 服务端 C++ 源代码 (server.cpp, ws_handler.cpp, user_manager.cpp 等)
├── include/            # C++ 头文件目录 (chat/server.hpp, chat/ws_handler.hpp 等)
├── web/                # 前端静态资源 (index.html, admin.html, app.js, style.css)
├── data/               # 运行时数据与语音文件存储目录 (messages.jsonl, audio/)
├── third_party/        # 第三方依赖库 (httplib.h, nlohmann/json.hpp)
├── tests/              # 自动化测试与 E2E 脚本
├── tasks/              # 任务开发规范与需求文档
└── Makefile            # 项目构建脚本
```

## WebSocket 协议简介

WebSocket 连接建立在 `/ws` 接口，客户端与服务端统一使用 JSON 文本帧传输，核心数据包格式包含 `type` 字段：

| type | 方向 | 描述 |
| :--- | :--- | :--- |
| `login` | 客户端 -> 服务端 | 登录请求：`{"type":"login","nickname":"张三"}` |
| `system` | 服务端 -> 客户端 | 系统通知与上线/下线广播 |
| `chat` | 双向 | 全局聊天消息：`{"type":"chat","text":"你好","sticker":"happy"}` |
| `dm` | 双向 | 定向私聊消息：`{"type":"dm","to":"李四","text":"私密消息"}` |
| `group_chat` | 双向 | 群组消息：`{"type":"group_chat","group":"C++小组","text":"大家好"}` |
| `voice_send` | 客户端 -> 服务端 | 发送语音消息：`{"type":"voice_send","url":"/data/audio/..."}` |
| `mention` | 服务端 -> 客户端 | `@提及` 定向提醒通知 |
| `error` | 服务端 -> 客户端 | 错误提示消息 |

---
