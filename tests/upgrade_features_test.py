#!/usr/bin/env python3
"""自动化测试：验证升级后的全部新特性（图片上传、Reaction、Typing、Quote Reply、静态资源挂载等）。"""
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
PORT = 8092
BASE = f"http://127.0.0.1:{PORT}"
WS_URL = f"ws://127.0.0.1:{PORT}/ws"

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

def http_post_json(path, data, content_type):
    req = urllib.request.Request(BASE + path, data=data, headers={"Content-Type": content_type}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, None

def recv_until(ws, pred, timeout=4.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            raw = ws.recv(timeout=0.25)
            if not raw:
                continue
            data = json.loads(raw)
            if pred(data):
                return data
        except Exception:
            continue
    return None

def main():
    if not os.path.exists(SERVER):
        print(f"未找到服务器可执行文件: {SERVER}")
        return 2

    print(f"=== 启动新版测试服务器 (Port {PORT}) ===")
    proc = subprocess.Popen([SERVER, str(PORT)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        # 1. 就绪等待
        ready = False
        for _ in range(60):
            try:
                st, body, _ = http_get("/api/status")
                if st == 200:
                    ready = True
                    break
            except Exception:
                time.sleep(0.2)
        check(ready, "新版服务器就绪")
        if not ready:
            return 1

        print("\n--- 1. 静态资源与新 DOM 节点检查 ---")
        st, body_bytes, ct = http_get("/")
        html = body_bytes.decode("utf-8", "replace")
        check("sound-toggle" in html, "index.html 包含音效开关 (#sound-toggle)")
        check("image-button" in html, "index.html 包含发图按钮 (#image-button)")
        check("reply-banner" in html, "index.html 包含引用回复条 (#reply-banner)")
        check("typing-indicator" in html, "index.html 包含正在输入指示器 (#typing-indicator)")
        check("image-lightbox" in html, "index.html 包含图片全屏灯箱 (#image-lightbox)")

        st, css_bytes, _ = http_get("/style.css")
        css = css_bytes.decode("utf-8", "replace")
        check(".code-block" in css and ".reaction-badge" in css and ".user-avatar" in css,
              "style.css 包含代码块、Reaction 徽章与个性头像样式")

        print("\n--- 2. 图片上传接口 (POST /api/upload/image) ---")
        # 1x1 假 PNG 头部与内容
        fake_png = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15c4" + b"\x00"*200
        st, res = http_post_json("/api/upload/image", fake_png, "image/png")
        check(st == 200 and res and res.get("url", "").startswith("/data/images/"),
              f"图片上传成功: url={res.get('url') if res else None}")

        if res and res.get("url"):
            img_url = res["url"]
            st, img_bytes, ct = http_get(img_url)
            check(st == 200 and len(img_bytes) == len(fake_png), f"回读上传图片成功 (size={len(img_bytes)})")

        # 测试不支持的媒体类型
        st, _ = http_post_json("/api/upload/image", b"hello world", "text/plain")
        check(st == 415, f"非图片类型被拒绝 (status={st})")

        # 测试超大图片 (11MB)
        large_data = b"0" * (11 * 1024 * 1024)
        st, _ = http_post_json("/api/upload/image", large_data, "image/png")
        check(st == 413, f"超过 10MB 图片被拒绝 (status={st})")

        print("\n--- 3. WebSocket 升级特性联调 ---")
        import websockets.sync.client as wsc
        with wsc.connect(WS_URL) as ws1, wsc.connect(WS_URL) as ws2:
            # 登录
            ws1.send(json.dumps({"type": "login", "nickname": "极客甲"}, ensure_ascii=False))
            ws2.send(json.dumps({"type": "login", "nickname": "极客乙"}, ensure_ascii=False))
            time.sleep(0.3)

            # 3.1 正在输入指示器 (Typing)
            ws1.send(json.dumps({"type": "typing", "channel": "global"}, ensure_ascii=False))
            m = recv_until(ws2, lambda d: d.get("type") == "typing" and d.get("from") == "极客甲")
            check(m is not None, f"乙收到甲的 Typing 正在输入信号: {m}")

            # 3.2 发送带 Markdown 代码块与 msg_id 的聊天消息
            code_text = "```cpp\n#include <iostream>\nint main() { std::cout << 42; }\n```"
            msg_id_1 = f"test_{int(time.time()*1000)}_1"
            ws1.send(json.dumps({"type": "chat", "text": code_text, "msg_id": msg_id_1}, ensure_ascii=False))
            m = recv_until(ws2, lambda d: d.get("type") == "chat" and d.get("from") == "极客甲" and d.get("msg_id") == msg_id_1)
            check(m is not None and "```cpp" in m.get("text", ""), "乙收到甲发送的 C++ Markdown 代码块消息")

            # 3.3 消息 Reaction (点赞表情)
            ws2.send(json.dumps({"type": "reaction", "msg_id": msg_id_1, "emoji": "👍"}, ensure_ascii=False))
            m1 = recv_until(ws1, lambda d: d.get("type") == "reaction" and d.get("msg_id") == msg_id_1 and d.get("emoji") == "👍")
            m2 = recv_until(ws2, lambda d: d.get("type") == "reaction" and d.get("msg_id") == msg_id_1 and d.get("emoji") == "👍")
            check(m1 is not None and m2 is not None, "甲和乙均收到 👍 Reaction 广播")

            # 3.4 引用回复 (Quote Reply)
            reply_obj = {"id": msg_id_1, "from": "极客甲", "text": "代码写得真棒"}
            ws2.send(json.dumps({"type": "chat", "text": "确实很清晰！", "reply_to": reply_obj}, ensure_ascii=False))
            m = recv_until(ws1, lambda d: d.get("type") == "chat" and d.get("from") == "极客乙" and "reply_to" in d)
            check(m is not None and m.get("reply_to", {}).get("from") == "极客甲",
                  f"甲收到包含 reply_to 引用元数据的消息: {m.get('reply_to') if m else None}")

            # 3.5 发送图片消息 (image_send)
            ws1.send(json.dumps({"type": "image_send", "url": img_url}, ensure_ascii=False))
            m = recv_until(ws2, lambda d: d.get("type") == "image" and d.get("url") == img_url)
            check(m is not None and m.get("from") == "极客甲", f"乙收到甲发送的图片消息广播: {m}")

            # 3.6 私聊图片消息 (DM Image)
            ws2.send(json.dumps({"type": "image_send", "to": "极客甲", "url": img_url}, ensure_ascii=False))
            m_dm = recv_until(ws1, lambda d: d.get("type") == "image" and d.get("from") == "极客乙" and d.get("to") == "极客甲")
            check(m_dm is not None, f"甲收到来自乙的定向私聊图片: {m_dm}")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print("\n" + ("UPGRADE FEATURES ALL PASSED" if failures == 0 else f"UPGRADE TEST FAILED ({failures} failures)"))
    return 0 if failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
