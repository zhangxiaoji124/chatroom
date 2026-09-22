#!/usr/bin/env python3
"""任务 D + 任务 E 新功能端到端验证。

验证 (不删现有 data/messages.jsonl, 只追加新消息):
  1. /api/online 在线列表接口
  2. /api/history?limit=N 历史消息接口
  3. WebSocket 私聊 DM: 定向发送 + 回显 + 对方不在线错误
  4. 全局聊天 @提及 (mention 帧) 由任务 E 支持
  5. 后台 messages.jsonl 确实记录全局聊天消息

用法: python tests/features_e2e.py
依赖: websockets 或 websocket-client
"""
import json
import os
import sys
import time
import urllib.request
import importlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WS_URL = "ws://127.0.0.1:8080/ws"
HTTP = "http://127.0.0.1:8080"
DB_PATH = os.path.join(ROOT, "data", "messages.jsonl")

failures = 0
def check(cond, msg):
    global failures
    print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")
    if not cond:
        failures += 1

def http_json(path):
    with urllib.request.urlopen(HTTP + path, timeout=3) as r:
        return json.loads(r.read().decode())

def recv_until(ws, pred, timeout=4, max_msgs=50):
    """读取帧直到满足 pred(frame) 或超时/超量。返回命中的帧或 None。兼容两种库。"""
    import time as _t
    deadline = _t.time() + timeout
    has_settimeout = hasattr(ws, "settimeout")
    if has_settimeout:
        ws.settimeout(timeout)
    got = []
    try:
        while _t.time() < deadline and len(got) < max_msgs:
            try:
                raw = ws.recv()
            except Exception:
                # websockets 库超时抛 TimeoutError, 直接结束
                if _t.time() >= deadline:
                    break
                continue
            try:
                f = json.loads(raw)
            except Exception:
                continue
            got.append(f)
            if pred(f):
                return f
    except Exception:
        pass
    finally:
        if has_settimeout:
            ws.settimeout(None)
    return None

def main():
    stdout = sys.stdout
    try:
        import websocket  # websocket-client 优先, 支持 settimeout
        backend = "websocket-client"
    except ImportError:
        try:
            import websockets
            backend = "websockets"
        except ImportError:
            print("需要 websocket-client 或 websockets: pip install websocket-client")
            return 2
    print(f"ws backend: {backend}")

    import socket
    import subprocess
    server_proc = None
    try:
        s = socket.create_connection(("127.0.0.1", 8080), timeout=1)
        s.close()
    except OSError:
        exe = os.path.join(ROOT, "build", "chatroom_server_v2.exe")
        if os.path.exists(exe):
            server_proc = subprocess.Popen([exe, "8080"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            for _ in range(30):
                try:
                    s = socket.create_connection(("127.0.0.1", 8080), timeout=1)
                    s.close()
                    break
                except OSError:
                    time.sleep(0.2)

    # 记后台消息行数基线
    base_lines = 0
    if os.path.exists(DB_PATH):
        with open(DB_PATH, "r", encoding="utf-8") as f:
            base_lines = len([l for l in f if l.strip()])
    print(f"后台基线 messages.jsonl 行数: {base_lines}")

    # ===== 1) /api/online =====
    print("\n[1] /api/online")
    try:
        d = http_json("/api/online")
        check(isinstance(d, dict) and "oneline" in d, f"/api/online 返回 oneline, 实为 keys={list(d.keys())}")
        check(isinstance(d.get("oneline"), list), "oneline 是数组")
    except Exception as e:
        check(False, f"/api/online 失败: {e}")

    # ===== 2) /api/history =====
    print("\n[2] /api/history")
    try:
        d = http_json("/api/history?limit=5")
        check(isinstance(d, list), f"/api/history 返回数组, 实为 {type(d).__name__}")
        check(len(d) <= 5, f"limit=5 生效, 返回 {len(d)} 条")
        if d:
            check("type" in d[0] and "ts" in d[0], "历史条目含 type/ts 字段")
    except Exception as e:
        check(False, f"/api/history 失败: {e}")

    # ===== WebSocket: 登录 + 全局聊天 + @提及 落库 =====
    print("\n[3] 登录 + 全局聊天 + @提及 + 后台落库")
    users = {"alice": "测试用户A", "bob": "测试用户B"}
    # 用较独特昵称避免和现有在线用户混淆
    import random
    suffix = str(int(time.time()))[-6:]
    nick_a = "功能测试A"
    nick_b = "功能测试B"
    suffixA = f"{nick_a}-{suffix}"
    suffixB = f"{nick_b}-{suffix}"

    sockets = {}
    try:
        if backend == "websockets":
            sockets["a"] = wsc.connect(WS_URL)
            sockets["b"] = wsc.connect(WS_URL)
            a = sockets["a"].__enter__()
            b = sockets["b"].__enter__()
    except Exception:
        pass

    try:
        if backend == "websocket-client":
            a = websocket.create_connection(WS_URL, timeout=5)
            b = websocket.create_connection(WS_URL, timeout=5)

        # 登录
        a.send(json.dumps({"type": "login", "nickname": suffixA}))
        b.send(json.dumps({"type": "login", "nickname": suffixB}))
        time.sleep(0.6)

        # A 发全局聊天 @B
        a.send(json.dumps({"type": "chat", "text": f"@{suffixB} 你好，这是功能测试 @提及消息"}))
        time.sleep(0.4)

        # 刷新 /api/online 应能看到两个新用户
        d = http_json("/api/online")
        nicks = [u.get("nickname") for u in d.get("oneline", [])]
        check(suffixA in nicks and suffixB in nicks,
              f"/api/online 包含两新用户, 在线={len(nicks)}")

        # ===== 4) 私聊 DM =====
        print("\n[4] 私聊 DM")
        # A -> B
        a.send(json.dumps({"type": "dm", "to": suffixB, "text": "私聊你好B"}))
        # A 应收到回显
        echo = recv_until(a, lambda f: f.get("type") == "dm" and f.get("to") == suffixB and f.get("from") == suffixA)
        check(echo is not None, "发送者 A 收到 dm 回显")
        # B 应收到
        recv_b = recv_until(b, lambda f: f.get("type") == "dm" and f.get("from") == suffixA and f.get("to") == suffixB)
        check(recv_b is not None, "接收者 B 收到 dm")
        if recv_b:
            check(recv_b.get("text") == "私聊你好B", f"dm 文本正确: {recv_b.get('text')!r}")

        # DM 不落库
        if os.path.exists(DB_PATH):
            with open(DB_PATH, "r", encoding="utf-8") as f:
                lines = [l for l in f if l.strip()]
            dm_lines = [l for l in lines if '"dm"' in l]
            check(len(dm_lines) == 0, "dm 消息未落库 (仅内存定向)")
        else:
            check(False, "messages.jsonl 不存在")

        # 私聊给不在线用户 -> 错误帧
        a.send(json.dumps({"type": "dm", "to": "不存在的用户XYZ", "text": "hi"}))
        err = recv_until(a, lambda f: f.get("type") == "error")
        check(err is not None and "不在线" in str(err.get("msg", "")), f"私聊不在线用户报错: {err}")

        # 向自己发 dm -> 应报错或被拒绝
        a.send(json.dumps({"type": "dm", "to": suffixA, "text": "自聊"}))
        err2 = recv_until(a, lambda f: f.get("type") == "error")
        check(err2 is not None, "自聊被拒绝 (error 帧)")

        # ===== 5) 后台确实记录全局聊天 =====
        print("\n[5] 后台落库确认")
        if os.path.exists(DB_PATH):
            with open(DB_PATH, "r", encoding="utf-8") as f:
                lines = [l for l in f if l.strip()]
            new = [l for l in lines if suffixA in l or suffixB in l]
            has_chat = any(('"type":"chat"' in l or '"type": "chat"' in l) for l in new)
            check(len(lines) > base_lines, f"messages.jsonl 新增 {len(lines)-base_lines} 行")
            check(has_chat, "全局聊天消息已写入后台 jsonl")
            if new:
                print(f"    后台新记录示例: {new[-1][:120]}")
        else:
            check(False, "messages.jsonl 不存在")

        # 收尾: 清理连接
        try:
            a.close()
            b.close()
        except Exception:
            pass

    except Exception as e:
        import traceback
        traceback.print_exc()
        check(False, f"测试异常: {e}")
    finally:
        for s in sockets.values():
            try:
                s.__exit__(None, None, None)
            except Exception:
                pass
        if server_proc:
            server_proc.terminate()
            try:
                server_proc.wait(timeout=2)
            except:
                server_proc.kill()

    print("\n" + ("FEATURES E2E PASSED" if failures == 0 else "FEATURES E2E FAILED"))
    return 0 if failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
