#!/usr/bin/env python3
"""自动化测试：验证桌面应用与 PWA 相关资源与启动套件。"""
import json
import os
import subprocess
import sys
import time
import urllib.request

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SERVER = os.path.join(ROOT, "build", "chatroom_server_v2.exe")
APP_EXE = os.path.join(ROOT, "build", "ChatroomApp.exe")
PORT = 8094
BASE = f"http://127.0.0.1:{PORT}"

failures = 0
def check(cond, msg):
    global failures
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {msg}")
    if not cond:
        failures += 1

def http_get(path):
    with urllib.request.urlopen(BASE + path, timeout=4) as r:
        return r.status, r.read(), r.headers.get("Content-Type")

def main():
    print(f"=== 桌面客户端与 PWA 自动化测试 (Port {PORT}) ===")

    # 1. 检查桌面端二进制与脚本
    print("\n--- 1. 桌面端原生启动器与快捷方式检查 ---")
    check(os.path.exists(APP_EXE), f"原生启动器已生成: {APP_EXE}")
    check(os.path.exists(os.path.join(ROOT, "Start-Chatroom-Desktop.bat")), "启动脚本 Start-Chatroom-Desktop.bat 存在")
    check(os.path.exists(os.path.join(ROOT, "scripts", "create_desktop_shortcut.bat")), "快捷方式创建脚本 create_desktop_shortcut.bat 存在")

    proc = subprocess.Popen([SERVER, str(PORT)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        ready = False
        for _ in range(50):
            try:
                st, body, _ = http_get("/api/status")
                if st == 200:
                    ready = True
                    break
            except Exception:
                time.sleep(0.2)
        check(ready, "测试服务器就绪")
        if not ready:
            return 1

        print("\n--- 2. PWA Manifest 清单与图标资产测试 ---")
        st, body_bytes, ct = http_get("/manifest.json")
        check(st == 200, "GET /manifest.json == 200")
        manifest = json.loads(body_bytes.decode("utf-8"))
        check(manifest.get("display") == "standalone", "manifest.json display 为 standalone")
        check(manifest.get("name") == "Chatroom · 极客实时聊天室", f"manifest.json name 正确: {manifest.get('name')}")
        check(len(manifest.get("icons", [])) >= 2, f"manifest.json 包含图标: {len(manifest.get('icons', []))} 个")

        st, sw_bytes, _ = http_get("/sw.js")
        sw_text = sw_bytes.decode("utf-8")
        check(st == 200 and "caches.open" in sw_text, "GET /sw.js == 200 包含 Service Worker 缓存逻辑")

        st, icon192, _ = http_get("/icons/icon-192.png")
        check(st == 200 and len(icon192) > 500, f"GET /icons/icon-192.png == 200 (size={len(icon192)})")

        st, icon512, _ = http_get("/icons/icon-512.png")
        check(st == 200 and len(icon512) > 1000, f"GET /icons/icon-512.png == 200 (size={len(icon512)})")

        st, fav, _ = http_get("/favicon.ico")
        check(st == 200 and len(fav) > 500, f"GET /favicon.ico == 200 (size={len(fav)})")

        print("\n--- 3. 桌面端 HTML 与系统通知控件检查 ---")
        st, html_bytes, _ = http_get("/")
        html = html_bytes.decode("utf-8")
        check('rel="manifest"' in html, "index.html 包含 manifest.json 链接")
        check("notification-toggle" in html, "index.html 包含桌面系统通知按钮 (#notification-toggle)")
        check("pwa-install-button" in html, "index.html 包含桌面端安装按钮 (#pwa-install-button)")

        st, js_bytes, _ = http_get("/app.js")
        js_text = js_bytes.decode("utf-8")
        check("sendDesktopNotification" in js_text, "app.js 包含 sendDesktopNotification 原生通知")
        check("serviceWorker.register" in js_text, "app.js 包含 Service Worker 注册逻辑")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print("\n" + ("DESKTOP & PWA ALL PASSED" if failures == 0 else f"DESKTOP & PWA FAILED ({failures} failures)"))
    return 0 if failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
