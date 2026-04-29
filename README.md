# C++ Chat Room + Web Monitor

一个基于 C++ 的多客户端聊天程序，配套 Spring Boot 图形化控制台。

- C++ 程序支持：服务端、客户端、压测、一键启动模式
- Web 控制台支持：服务端控制、客户端控制、在线用户/日志、实时监控图表

## 目录结构

```text
.
├─ main.cpp                     # C++ 主程序入口
├─ Thread_Pools.*               # 线程池
├─ build-all.bat                # 一键构建脚本（UI + C++）
├─ cmake-build-release/Release/untitled18.exe
└─ chat-ui/                     # Spring Boot 可视化控制台
```

## 环境要求

- Windows 10/11
- JDK 17+
- Maven 3.9+
- CMake 3.2+
- Visual Studio Build Tools（MSVC）

## 快速开始

### 1. 一键构建

在项目根目录执行：

```bat
build-all.bat
```

构建成功后会生成：

- `cmake-build-release/Release/untitled18.exe`
- `chat-ui/target/chat-ui-1.0.0.jar`

### 2. 一键启动（推荐）

运行：

```text
cmake-build-release/Release/untitled18.exe
```

在菜单中选择：

```text
4) One-click start (UI + server)
```

然后程序会：

- 启动 Web 控制台（默认 `http://127.0.0.1:8088/`）
- 自动打开浏览器
- 启动聊天服务端（默认端口 `9000`）

## Web 控制台说明

- 主页：压测指标可视化
- 服务端页：启动/停止服务端、在线用户、日志
- 客户端页：启动多个客户端、发送消息、查看会话日志

当前已优化：

- 页面入口默认新开标签页，避免覆盖当前页面
- 客户端页支持一键新建客户端窗口

## 常见端口

- 控制台服务：`8088`
- 聊天服务：`9000`

## 常见问题

### 1) 链接失败 `LNK1104` 无法打开 `untitled18.exe`

通常是旧进程占用可执行文件。先关闭运行中的 `untitled18.exe` 后再构建。

### 2) UI 打不开

确认 `chat-ui` 已启动并监听 `8088`，再访问：

`http://127.0.0.1:8088/`

### 3) 两个客户端看起来互不通信

请确认：

- 两个客户端连接的是同一个服务端 IP/端口
- 服务端已成功启动且端口可用

## 许可证

如需开源发布，建议补充 `LICENSE` 文件（如 MIT）。
