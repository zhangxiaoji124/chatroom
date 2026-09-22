@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

echo ===================================================
echo   正在启动分布式聊天室集群 (Cluster Mesh + Gateway)
echo ===================================================

if not exist "build\chatroom_server_v2.exe" (
    echo [提示] 找不到 build\chatroom_server_v2.exe，尝试编译...
    mingw32-make
)

python scripts\launch_cluster.py
pause
