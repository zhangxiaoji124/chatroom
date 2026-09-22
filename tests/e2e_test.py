#!/usr/bin/env python3
"""Chatroom 端到端测试。

启动 build/chatroom_server.exe，用两个 WebSocket 客户端验证:
  1. / 与 /api/status HTTP
  2. 登录（login）-> 上线通知广播
  3. 聊天消息广播 + 落库 data/messages.jsonl
  4. 断开（close）-> 离线通知广播
用法: python tests/e2e_test.py
依赖: websockets (pip install websockets) 或 python websocket-client
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SERVER = os.path.join(ROOT, "build", "chatroom_server.exe")
DB_PATH = os.path.join(ROOT, "data", "messages.jsonl")
WS_URL = "ws://127.0.0.1:8080/ws"
HTTP_URL = "http://127.0.0.1:8080"

failures = 0
backend = None
def check(cond, msg):
    global failures
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {msg}")
    if not cond:
        failures += 1

def main():
    if not os.path.exists(SERVER):
        print(f"未找到服务器可执行文件: {SERVER}")
        print("请先运行: cd D:\\chatroom && mingw32-make -j4")
        return 2

    # 清理旧日志
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)

    # 挑选可用的 websocket 库
    import importlib
    global backend
    try:
        ws_mod = importlib.import_module("websockets")
        backend = "websockets"
    except ImportError:
        try:
            ws_mod = importlib.import_module("websocket")  # websocket-client
            backend = "websocket-client"
        except ImportError:
            print("需要安装 websockets 或 websocket-client 之一: pip install websockets")
            return 2

    print(f"使用 websocket 后端: {backend}")

    # 启动服务器
    proc = subprocess.Popen([SERVER], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        # 等待就绪（探测 HTTP /）
        import urllib.request
        ready = False
        for _ in range(50):
            try:
                urllib.request.urlopen(HTTP_URL + "/", timeout=0.5)
                ready = True
                break
            except Exception:
                time.sleep(0.2)
        check(ready, "服务器 HTTP 就绪")
        if not ready:
            return 1

        # 健康检查
        try:
            with urllib.request.urlopen(HTTP_URL + "/api/status", timeout=1) as r:
                st = json.loads(r.read().decode())
                check(st.get("status") == "ok", f"/api/status ok, online={st.get('online_count')}")
        except Exception as e:
            check(False, f"/api/status 请求失败: {e}")

        # ---- WebSocket 测试 ----
        if backend == "websockets":
            import websockets.sync.client as wsc
            with wsc.connect(WS_URL) as alice, wsc.connect(WS_URL) as bob:
                run_ws_tests(alice, bob)
        else:
            import websocket
            alice = websocket.create_connection(WS_URL, timeout=5)
            bob = websocket.create_connection(WS_URL, timeout=5)
            try:
                run_ws_tests(alice, bob)
            finally:
                alice.close()
                bob.close()

        # ---- 落库检查 ----
        if os.path.exists(DB_PATH):
            with open(DB_PATH, "r", encoding="utf-8") as f:
                lines = [l for l in f if l.strip()]
            check(len(lines) >= 1, f"messages.jsonl 非空, 共 {len(lines)} 条")
        else:
            check(False, "messages.jsonl 未生成")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print("\n" + ("E2E PASSED" if failures == 0 else "E2E FAILED"))
    return 0 if failures == 0 else 1


def recv_json(ws, timeout=5):
    """按后端差异接收一条 JSON 消息。"""
    global backend
    if backend == "websockets":
        raw = ws.recv(timeout=timeout)
    else:
        ws.settimeout(timeout)
        raw = ws.recv()
    return json.loads(raw)


def run_ws_tests(alice, bob):
    """alice/bob 是两个已连接的 WebSocket 客户端。"""
    global backend

    # Alice 登录
    alice.send(json.dumps({"type": "login", "nickname": "张三"}, ensure_ascii=False))
    got = recv_json(alice)   # 上线通知广播（发给 alice 自己）
    check(got.get("type") == "system" and "上线" in got.get("msg", ""),
          f"Alice 上线通知: {got}")

    # Bob 登录，应收到系统通知；Alice 也应收到 Bob 上线广播
    bob.send(json.dumps({"type": "login", "nickname": "李四"}, ensure_ascii=False))
    got_bob = recv_json(bob)      # 通常 bob 收到自己上线通知
    got_alice = recv_json(alice)  # alice 收到 bob 上线广播
    check(got_bob.get("type") == "system", f"Bob 上线通知: {got_bob}")
    check(got_alice.get("type") == "system" and "上线" in got_alice.get("msg", ""),
          f"Alice 收到 Bob 上线广播: {got_alice}")

    # 聊天：Alice 发消息，Alice 与 Bob 都应收到
    alice.send(json.dumps({"type": "chat", "text": "大家好"}, ensure_ascii=False))
    m_alice = recv_json(alice)
    m_bob = recv_json(bob)
    check(m_alice.get("type") == "chat" and m_alice.get("from") == "张三"
          and m_alice.get("text") == "大家好", f"Alice 收到自己消息广播: {m_alice}")
    check(m_bob.get("type") == "chat" and m_bob.get("from") == "张三"
          and m_bob.get("text") == "大家好", f"Bob 收到 Alice 消息广播: {m_bob}")

    # 昵称重复登录：新连接应收到 error
    if backend == "websockets":
        import websockets.sync.client as wsc
        dup = wsc.connect(WS_URL)
        try:
            dup.send(json.dumps({"type": "login", "nickname": "张三"}, ensure_ascii=False))
            err = recv_json(dup)
            check(err.get("type") == "error" and "占用" in err.get("msg", ""),
                  f"重复登录应报错: {err}")
        finally:
            dup.close()
    else:
        import websocket
        dup = websocket.create_connection(WS_URL, timeout=5)
        try:
            dup.send(json.dumps({"type": "login", "nickname": "张三"}, ensure_ascii=False))
            err = recv_json(dup)
            check(err.get("type") == "error" and "占用" in err.get("msg", ""),
                  f"重复登录应报错: {err}")
        finally:
            dup.close()

    # 断开：Bob 关闭，Alice 应收到离线广播
    bob.close()
    got_alice2 = recv_json(alice)
    check(got_alice2.get("type") == "system" and "离线" in got_alice2.get("msg", ""),
          f"Alice 收到 Bob 离线广播: {got_alice2}")

    alice.close()


if __name__ == "__main__":
    sys.exit(main())
