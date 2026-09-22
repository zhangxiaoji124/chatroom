#!/usr/bin/env python3
"""测试管理后台桌面应用、全员公告与移出用户端点。"""
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
ADMIN_APP = os.path.join(ROOT, "build", "ChatroomAdminApp.exe")
PORT = 8095
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

def http_post_json(path, data):
    body = json.dumps(data).encode("utf-8")
    req = urllib.request.Request(
        BASE + path,
        data=body,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=4) as r:
        return r.status, json.loads(r.read().decode("utf-8"))

def main():
    print(f"=== 管理后台桌面应用自动化测试 (Port {PORT}) ===")

    print("\n--- 1. 管理后台桌面二进制与快捷方式检查 ---")
    check(os.path.exists(ADMIN_APP), f"管理后台启动器已生成: {ADMIN_APP}")
    check(os.path.exists(os.path.join(ROOT, "Start-Chatroom-Admin.bat")), "Start-Chatroom-Admin.bat 存在")
    check(os.path.exists(os.path.join(ROOT, "scripts", "create_admin_shortcut.py")), "create_admin_shortcut.py 存在")

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

        print("\n--- 2. 管理后台静态页面与 Manifest 清单测试 ---")
        st, admin_html_bytes, _ = http_get("/admin.html")
        admin_html = admin_html_bytes.decode("utf-8")
        check(st == 200, "GET /admin.html == 200")
        check('rel="manifest" href="admin-manifest.json"' in admin_html, "admin.html 引用 admin-manifest.json")
        check("broadcast-form" in admin_html, "admin.html 包含全员公告表单 (#broadcast-form)")
        check("export-json-btn" in admin_html, "admin.html 包含导出按钮 (#export-json-btn)")
        check("refresh-interval-select" in admin_html, "admin.html 包含刷新频率下拉选择 (#refresh-interval-select)")

        st, admin_manifest_bytes, _ = http_get("/admin-manifest.json")
        manifest = json.loads(admin_manifest_bytes.decode("utf-8"))
        check(manifest.get("display") == "standalone", "admin-manifest.json display 为 standalone")
        check("控制台" in manifest.get("name", ""), f"admin-manifest.json 包含控制台名称: {manifest.get('name')}")

        st, admin_icon, _ = http_get("/icons/admin-icon-192.png")
        check(st == 200 and len(admin_icon) > 500, f"GET /icons/admin-icon-192.png == 200 (size={len(admin_icon)})")

        st, admin_fav, _ = http_get("/admin-favicon.ico")
        check(st == 200 and len(admin_fav) > 500, f"GET /admin-favicon.ico == 200 (size={len(admin_fav)})")

        print("\n--- 3. 全员公告推送 API 测试 (POST /api/admin/broadcast) ---")
        st, bcast_res = http_post_json("/api/admin/broadcast", {"message": "测试系统全员维护公告"})
        check(st == 200 and bcast_res.get("status") == "ok", "POST /api/admin/broadcast 成功返回 200 ok")

        try:
            http_post_json("/api/admin/broadcast", {"message": ""})
            check(False, "空广播应返回 400")
        except urllib.error.HTTPError as e:
            check(e.code == 400, "空广播被正确拦截返回 HTTP 400")

        print("\n--- 4. 用户踢出 API 测试 (POST /api/admin/kick) ---")
        try:
            http_post_json("/api/admin/kick", {"nickname": "不存在的用户_999"})
            check(False, "踢出离线/不存在用户应返回 400")
        except urllib.error.HTTPError as e:
            check(e.code == 400, "踢出不存在用户返回 HTTP 400")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print("\n" + ("ADMIN DESKTOP ALL PASSED" if failures == 0 else f"ADMIN DESKTOP FAILED ({failures} failures)"))
    return 0 if failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
