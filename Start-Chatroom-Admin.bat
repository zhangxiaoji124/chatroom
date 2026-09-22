@echo off
setlocal
cd /d "%~dp0"

if exist "build\ChatroomAdminApp.exe" (
    start "" "build\ChatroomAdminApp.exe"
) else (
    echo [提示] 正在启动后台服务器...
    start "" "build\chatroom_server.exe" 8080
    timeout /t 1 >nul
    start "" "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --app=http://localhost:8080/admin.html --window-size=1260,880
)
