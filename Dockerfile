# ==========================================
# 聊天室 Docker 容器化构建与部署配置
# ==========================================

# 阶段 1：编译环境
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    make \
    ca-certificates \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# 编译生成 Linux 原生高性能服务端程序
RUN g++ -std=c++20 -O2 \
    -Iinclude -Ithird_party \
    src/main.cpp \
    src/server.cpp \
    src/ai_client.cpp \
    src/user_manager.cpp \
    src/ws_handler.cpp \
    src/storage.cpp \
    third_party/sqlite3.c \
    -lpthread -ldl \
    -o chatroom_server

# 阶段 2：轻量运行环境
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 从构建阶段复制二进制与前端静态资源
COPY --from=builder /src/chatroom_server /app/chatroom_server
COPY web /app/web

# 创建数据挂载目录
RUN mkdir -p /app/data /app/data/audio /app/data/images

ENV PORT=8080
EXPOSE 8080

VOLUME ["/app/data"]

HEALTHCHECK --interval=15s --timeout=3s --start-period=5s --retries=3 \
  CMD curl -f http://localhost:8080/api/health || exit 1

CMD ["/app/chatroom_server"]
