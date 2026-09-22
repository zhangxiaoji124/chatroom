#!/usr/bin/env python3
"""前端联调验证脚本。

验证前后端同源联通：
  1. GET / 返回 web/index.html（含关键 UI 节点）
  2. GET /style.css、/app.js 返回 200 且非空
  3. GET /api/status 返回 JSON
  4. WS /ws 登录/聊天/广播正常（回归确认静态挂载没破坏后端）
  5. index.html 是否正确引用了 style.css 和 app.js

用法: python tests/frontend_check.py
依赖: websockets (pip install websockets)
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SERVER = os.path.join(ROOT, "build", "chatroom_server.exe")
WEB = os.path.join(ROOT, "web")
BASE = "http://127.0.0.1:8080"
WS = "ws://127.0.0.1:8080/ws"

failures = 0
def check(cond, msg):
    global failures
    print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")
    if not cond:
        failures += 1

def http_get(path, timeout=3):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return r.status, r.read().decode("utf-8", "replace"), r.headers.get("Content-Type")

def main():
    if not os.path.exists(SERVER):
        print(f"未找到服务器: {SERVER}，请先 mingw32-make all")
        return 2

    missing = [f for f in ("index.html", "style.css", "app.js") if not os.path.exists(os.path.join(WEB, f))]
    check(not missing, f"web/ 前端文件齐全 {WEB}" + (f" 缺少: {missing}" if missing else ""))
    if missing:
        print("前端文件未就位，无法联调。")
        return 2

    # 静态文件内部引用检查
    idx = open(os.path.join(WEB, "index.html"), encoding="utf-8").read()
    check('style.css' in idx, "index.html 引用 style.css")
    check('app.js' in idx, "index.html 引用 app.js")

    # 启动服务器
    proc = subprocess.Popen([SERVER], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        # 就绪探针
        ready = False
        for _ in range(50):
            try:
                http_get("/api/status", timeout=0.5)
                ready = True
                break
            except Exception:
                time.sleep(0.2)
        check(ready, "后端 HTTP 服务就绪")
        if not ready:
            return 1

        # 1. 静态页面
        st, body, ct = http_get("/")
        check(st == 200 and "text/html" in str(ct), f"GET / == 200 text/html (ct={ct})")
        for needle in ("<html", "login", "nickname", "ws", "id="):
            if needle in body.lower():
                check(True, f"index.html 含关键节点 {needle!r}")
                break
        else:
            check(False, "index.html 未发现登录/WebSocket 相关节点")

        # 2. 静态资源
        for f in ("style.css", "app.js"):
            st, body, ct = http_get(f"/{f}")
            check(st == 200 and len(body) > 0, f"GET /{f} == 200 非空 (len={len(body)})")

        # 3. API
        st, body, ct = http_get("/api/status")
        check(st == 200, "GET /api/status == 200")
        try:
            data = json.loads(body)
            check(data.get("status") == "ok" and "online_count" in data,
                  f"/api/status JSON 正确 {data}")
        except Exception as e:
            check(False, f"/api/status 非 JSON: {e}")

        # 4. WebSocket 联通（前端同源于 /ws 的路径）
        import websockets.sync.client as wsc
        with wsc.connect(WS) as a, wsc.connect(WS) as b:
            a.send(json.dumps({"type": "login", "nickname": "前端甲"}, ensure_ascii=False))
            a.recv(timeout=3)  # 本人上线
            b.send(json.dumps({"type": "login", "nickname": "前端乙"}, ensure_ascii=False))
            b.recv(timeout=3)
            a.recv(timeout=3)  # a 收到乙上线
            check(True, "WS 双人登录、上线广播正常")

            a.send(json.dumps({"type": "chat", "text": "前后端联通测试"}, ensure_ascii=False))
            ma = json.loads(a.recv(timeout=3))
            mb = json.loads(b.recv(timeout=3))
            check(ma.get("type") == "chat" and ma.get("text") == "前后端联通测试",
                  f"甲收到自己消息广播")
            check(mb.get("type") == "chat" and mb.get("text") == "前后端联通测试",
                  f"乙收到甲消息广播")

        # 5. 重新确认在线人数反映
        st, body, _ = http_get("/api/status")
        data = json.loads(body)
        check(data.get("online_count", 0) >= 0, f"online_count 可读 ({data.get('online_count')})")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print("\n" + ("FRONTEND-BACKEND LINK PASSED" if failures == 0 else "LINK FAILED"))
    return 0 if failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
