# 💬 Chatroom 现代化分布式实时聊天系统 (C++20)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/Architecture-Distributed%20Mesh-orange.svg)](#系统架构)
[![Database](https://img.shields.io/badge/Storage-SQLite3%20(WAL)-blueviolet.svg)](https://www.sqlite.org/)
[![Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)](#快速开始)

基于现代 **C++20** 构建的高性能、去中心化分布式实时聊天室系统。系统集成了点对点集群网格总线（P2P Cluster Mesh）、透明负载均衡网关、SQLite 持久化引擎、用户认证与密保风控体系、富文本/音视频多媒体交互，以及全功能管理控制台。

---

## 🌟 核心特性概览

### 1. 🌐 分布式集群与网关 (Distributed Cluster Mesh)
- **去中心化网格 (P2P Mesh)**：节点间轻量互联，无需外部 Redis/Kafka/ZooKeeper 依赖，开箱即用。
- **动态自发现与心跳保活**：节点间自动探活（`/api/cluster/ping`），实时监测往返延迟与集群在线人数。
- **跨节点实时通信**：公聊广播、群组隔离、定向私聊（DM）、消息撤回（Recall）全网毫秒级同步。
- **广播风暴阻断**：全局事件指纹（`event_id`）配合 10,000 级滑动窗口去重，自动拦截环路反弹。
- **反向代理与负载均衡网关 (`scripts/gateway.py`)**：统一 8000 接入端口，支持普通 HTTP 轮询转发与 WebSocket 升级握手双工透明管道（Raw Socket Pipe）。
- **一键集群编排 (`scripts/launch_cluster.py`)**：一键并行拉起 3 个 C++ 节点（8080, 8081, 8082）与负载均衡网关。

### 2. 🔐 账号安全与风控体系
- **用户认证与鉴权**：支持账号注册、登录校验、密码安全哈希（SHA-256 + 随机 Salt）与 Token 会话管理。
- **专属密保恢复码 (Recovery Key)**：注册即生成形如 `REC-XXXX-XXXX` 的安全恢复码，支持自助忘记密码无感重置。
- **智能速率限制 (RateLimiter)**：单用户每秒上限 5 条消息，防脚本恶意刷屏。
- **暴力破解防护**：IP 连续密码错误 5 次自动触发 60 秒封锁保护。

### 3. 💬 消息与富媒体体验
- **离线私聊与消息同步**：接收方离线时自动存入离线留言箱，上线即刻拉取未读消息列表与同步通知。
- **双向撤回机制**：支持公聊、群聊、私聊 2 分钟内自主撤回，管理员享有全局撤回审计权。
- **表情互动与快捷回应**：支持在消息上点赞/表情回应（👍❤️😄😢🎉）与富文本回复（Reply-To）。
- **多媒体文件交互**：内置高效图片上传展示面板与浏览器端音频录音（WebM/Ogg）播放器。
- **AI 智能助手集成**：全局聊天支持 `@机器人` / `@AI` 呼叫内置 DeepSeek 大模型驱动的客服助手。

### 4. 📊 管理控制台与数据库审计 (Admin Dashboard)
- **集群拓扑看板**：实时直观呈现各节点运行状态、当前延迟、在线人数分布。
- **全员广播推送**：管理端一键向全集群实时推送系统置顶公告。
- **在线用户控制**：实时监控在线用户 IP、连线时长，支持一键违规踢出。
- **密码与账号管理**：后台查看用户恢复码、重置密码或注销账号。
- **消息审计与导出**：支持关键字/用户检索，一键导出 JSON / CSV 消息日志报表。
- **控制台终端监听 (`scripts/watch_db.py`)**：实时在命令行滚动监控 SQLite 数据库的消息写入与撤回。

### 5. 🖥️ 桌面客户端体验
- 提供预编译桌面端独立启动器（`ChatroomApp.exe`、`ChatroomAdminApp.exe`）。
- 完整支持 PWA（Progressive Web App）桌面安装与离线图标封装。

---

## 🏛️ 系统架构

```
                         [客户端 / 浏览器 / 桌面端]
                                     │
                                     ▼
                ┌────────────────────────────────────────┐
                │   分布式网关负载均衡器 (Gateway: 8000)   │
                │  - WebSocket 透明双工双向管道转发       │
                │  - HTTP 动态健康检测与轮询调度           │
                └───────┬────────────┬────────────┬──────┘
                        │            │            │
            ┌───────────▼┐      ┌────▼───────┐  ┌─▼──────────┐
            │  Node 1    │◄────►│  Node 2    │◄►│  Node 3    │
            │  (:8080)   │ HTTP │  (:8081)   │  │  (:8082)   │
            │ SQLite/Bus │ Mesh │ SQLite/Bus │  │ SQLite/Bus │
            └────────────┘      └────────────┘  └────────────┘
```

---

## 📂 项目目录结构

```text
chatroom/
├── include/chat/               # C++ 模块头文件
│   ├── cluster_manager.hpp     # 分布式集群总线与节点心跳
│   ├── server.hpp              # 核心服务协调调度层
│   ├── storage.hpp             # SQLite3 持久化与迁移引擎
│   ├── user_manager.hpp        # 账号注册/鉴权/Token/密保码
│   ├── ws_handler.hpp          # WebSocket 连接与广播管理
│   ├── rate_limiter.hpp        # 令牌桶限流与防爆破
│   └── ai_client.hpp           # AI 对话调用客户端
├── src/                        # C++ 源代码实现
│   ├── main.cpp                # 命令行入口与参数解析
│   ├── cluster_manager.cpp     # 集群对等通信与滑动窗口去重
│   ├── server.cpp              # HTTP / WebSocket 业务路由
│   ├── storage.cpp             # SQLite 数据库底层读写
│   ├── user_manager.cpp        # 密码加密与会话状态维护
│   ├── ws_handler.cpp          # WebSocket 会话分发
│   ├── rate_limiter.cpp        # 安全限流器
│   └── ai_client.cpp           # DeepSeek API 请求适配
├── web/                        # 现代渐进式前端应用
│   ├── index.html / app.js     # 聊天客户端界面与交互
│   ├── admin.html / admin.js   # 管理控制台与集群拓扑看板
│   ├── style.css / admin.css   # 响应式布局与明暗主题
│   └── sw.js / manifest.json   # PWA 桌面离线支持
├── scripts/                    # 运维与辅助工具脚本
│   ├── gateway.py              # 分布式反向代理与负载均衡器
│   ├── launch_cluster.py       # 一键拉起 3 节点集群 + 网关
│   ├── watch_db.py             # 终端实时监听数据库消息变更
│   └── manage_users.py         # 命令行管理账号与密保码
├── tests/                      # 全量自动化测试套件
│   ├── cluster_e2e_test.py     # 分布式集群集成测试
│   ├── unit_tests.cpp          # C++ 核心单元测试
│   ├── features_e2e.py         # 全功能端到端验证
│   ├── recovery_key_test.py    # 密保找回密码测试
│   └── offline_dm_test.py      # 离线私聊与消息同步测试
├── third_party/                # 零外部安装依赖第三方库
│   ├── httplib.h               # cpp-httplib (单头文件网络库)
│   ├── sqlite3.c / sqlite3.h   # SQLite3 嵌入式数据库引擎
│   └── nlohmann/json.hpp       # Modern C++ JSON 库
├── data/                       # 运行时持久化数据（已 gitignore）
│   └── chatroom.db             # 核心 SQLite 数据库
├── Dockerfile                  # 容器化构建描述
├── docker-compose.yml          # 一键容器编排
└── Makefile                    # MinGW / GCC 构建编译脚本
```

---

## 🚀 快速开始

### 环境依赖
- **操作系统**：Windows 10/11 或 Linux / macOS
- **编译器**：支持 C++20 的 GCC / MinGW（GCC 11+）
- **构建工具**：`mingw32-make` 或 `make`
- **可选工具**：Python 3.8+（用于运行集群网关与集成测试）

### 1. 编译构建
在项目根目录下执行编译：
```bash
mingw32-make -j4
```
编译完成后将生成核心二进制产物：
- `build/chatroom_server_v2.exe`：核心聊天室服务端
- `build/ChatroomApp.exe`：用户桌面快捷客户端
- `build/ChatroomAdminApp.exe`：管理后台桌面控制台

---

## 🏃 运行方式

### 模式 A：一键启动分布式集群（推荐）
运行集群编排脚本，同时启动 3 个 C++ 节点（端口 8080, 8081, 8082）与 1 个透明负载均衡网关（端口 8000）：
```powershell
python scripts/launch_cluster.py
```
启动成功后，即可直接访问：
- **客户端统一入口**：[http://127.0.0.1:8000](http://127.0.0.1:8000)
- **管理控制台**：[http://127.0.0.1:8000/admin.html](http://127.0.0.1:8000/admin.html)
- **网关集群健康状态**：[http://127.0.0.1:8000/api/gateway/status](http://127.0.0.1:8000/api/gateway/status)

---

### 模式 B：单机独立模式运行（向下兼容）
如只需单机单实例开发调试，直接启动即可（不传 `--peers` 参数自动保持单机模式）：
```powershell
.\build\chatroom_server_v2.exe 8080
```
浏览器打开 [http://127.0.0.1:8080](http://127.0.0.1:8080) 即可开始聊天。

---

### 模式 C：手动自定义多节点集群组网
在多个终端或服务器上分别运行：
```powershell
# 节点 1 (端口 8080)
.\build\chatroom_server_v2.exe 8080 --node-id node_8080 --peers 127.0.0.1:8081,127.0.0.1:8082

# 节点 2 (端口 8081)
.\build\chatroom_server_v2.exe 8081 --node-id node_8081 --peers 127.0.0.1:8080,127.0.0.1:8082

# 节点 3 (端口 8082)
.\build\chatroom_server_v2.exe 8082 --node-id node_8082 --peers 127.0.0.1:8080,127.0.0.1:8081

# 启动反向代理网关 (端口 8000)
python scripts/gateway.py --port 8000 --backends 127.0.0.1:8080,127.0.0.1:8081,127.0.0.1:8082
```

---

## 🛠️ 实用工具与运维指令

### 1. 终端动态监听数据库变更
在控制台实时观察数据库中公聊、私聊、撤回等消息的即时流动：
```powershell
python scripts/watch_db.py
```

### 2. 命令行用户与密保码管理
查看所有注册用户、查看密保恢复码或重置密码：
```powershell
# 查看用户列表与密保码
python scripts/manage_users.py list

# 快速重置密码
python scripts/manage_users.py reset <用户名> <新密码>
```

---

## 🧪 自动化测试套件

项目自带完整的单元测试与端到端集成测试，确保每次迭代零回归：

```powershell
# 1. 运行 C++ 核心单元测试
mingw32-make test

# 2. 运行分布式集群多节点集成测试 (跨节点公聊/私聊/撤回/网关)
python tests/cluster_e2e_test.py

# 3. 运行密保恢复码与找回密码测试
python tests/recovery_key_test.py

# 4. 运行离线私聊与消息同步测试
python tests/offline_dm_test.py

# 5. 运行全功能 E2E 测试
python tests/features_e2e.py
```

---

## 📡 核心协议与接口

### WebSocket 消息格式 (`/ws`)
所有交互均使用 JSON 文本帧传输，核心帧结构包含 `type`：

| 帧类型 (`type`) | 方向 | 核心字段说明 | 描述 |
| :--- | :---: | :--- | :--- |
| `login` | C -> S | `nickname`, `token` (可选) | 用户登录或携带 Token 认证 |
| `register` | C -> S | `username`, `password` | 注册账号，返回专属 `recovery_key` |
| `chat` | 双向 | `text`, `sticker`, `reply_to`, `msg_id` | 全局聊天消息（支持富文本/回复） |
| `dm` | 双向 | `to`, `text`, `msg_id`, `reply_to` | 定向私聊消息（支持离线队列） |
| `group_chat` | 双向 | `group`, `text`, `msg_id` | 群组频道消息 |
| `reaction` | 双向 | `msg_id`, `emoji`, `group`, `to` | 对消息添加表情快捷回应 |
| `recall` | 双向 | `msg_id` | 撤回指定消息，全集群同步抹除 |
| `voice_send` | C -> S | `url`, `group` (可选) | 发送语音音频消息 |
| `image_send` | C -> S | `url`, `group`, `to` | 发送图片富媒体消息 |
| `typing` | 双向 | `from`, `to`, `group` | 正在输入中状态提醒 |
| `unread_sync` | S -> C | `counts`, `messages` | 上线时主动推送的离线未读列表 |

### 分布式内部 HTTP 接口
- `GET /api/cluster/ping`：节点心跳探测与在线人数上报。
- `POST /api/cluster/event`：跨节点事件广播传输接口（内置滑动窗口去重）。
- `GET /api/cluster/nodes`：获取当前全集群节点网格拓扑与健康指标。
- `GET /api/gateway/status`：网关反向代理健康状态监控。

---

## 📄 开源许可证

本项目采用 [MIT 许可证](LICENSE) 开源发布。
