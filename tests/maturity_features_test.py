import json
import socket
import subprocess
import sys
import time
import urllib.request

def wait_for_server(port, timeout=10):
    start = time.time()
    while time.time() - start < timeout:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                return True
        except OSError:
            time.sleep(0.2)
    return False

# Minimal WebSocket client using standard python socket for zero extra dependencies
import base64
import os
import struct

def ws_connect(port):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET /ws HTTP/1.1\r\n"
        f"Host: 127.0.0.1:{port}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n\r\n"
    )
    s.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = s.recv(1024)
        if not chunk:
            raise RuntimeError("Connection closed during handshake")
        resp += chunk
    if b"101 Switching Protocols" not in resp:
        raise RuntimeError("WebSocket handshake failed: " + resp.decode(errors="ignore"))
    return s

def ws_send(s, data):
    msg = data.encode("utf-8")
    length = len(msg)
    header = bytearray([0x81]) # text frame, fin
    mask = os.urandom(4)
    if length <= 125:
        header.append(0x80 | length)
    elif length <= 65535:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack("!Q", length))
    header.extend(mask)
    masked_msg = bytearray(b ^ mask[i % 4] for i, b in enumerate(msg))
    s.sendall(header + masked_msg)

def ws_recv(s, timeout=3):
    s.settimeout(timeout)
    # Read first 2 bytes
    b1, b2 = s.recv(2)
    length = b2 & 0x7F
    if length == 126:
        length = struct.unpack("!H", s.recv(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", s.recv(8))[0]
    data = b""
    while len(data) < length:
        chunk = s.recv(length - len(data))
        if not chunk:
            break
        data += chunk
    return json.loads(data.decode("utf-8"))

def main():
    port = 8089
    server_proc = subprocess.Popen(
        [r"d:\chatroom\build\chatroom_server_v2.exe"],
        cwd=r"d:\chatroom"
    )
    # Wait for server on port 8080 (the default compiled port)
    default_port = 8080
    print(f"Waiting for server on port {default_port}...")
    if not wait_for_server(default_port, timeout=8):
        print("Server failed to start!")
        server_proc.kill()
        sys.exit(1)
    
    failures = 0
    def assert_true(cond, desc):
        nonlocal failures
        if not cond:
            print(f"  [FAIL] {desc}")
            failures += 1
        else:
            print(f"  [OK] {desc}")

    try:
        # 1. Test /api/health
        print("\n--- 1. 测试 /api/health 接口 ---")
        with urllib.request.urlopen(f"http://127.0.0.1:{default_port}/api/health") as res:
            assert_true(res.status == 200, "HTTP 200 on /api/health")
            health_data = json.loads(res.read().decode())
            assert_true(health_data.get("status") == "ok", "health.status == 'ok'")
            assert_true("uptime_seconds" in health_data, "uptime_seconds present")
            assert_true("registered_accounts" in health_data, "registered_accounts present")
            print(f"  Health data: {health_data}")

        # 2. Test WebSocket: 注册账号
        print("\n--- 2. 测试账号注册 (register) ---")
        ws1 = ws_connect(default_port)
        test_user = f"test_{int(time.time())}"
        ws_send(ws1, json.dumps({"type": "register", "username": test_user, "password": "mypassword123"}))
        reg_resp = ws_recv(ws1)
        assert_true(reg_resp.get("type") == "register_success", "Received register_success")
        assert_true(reg_resp.get("username") == test_user, "Username matches")
        issued_token = reg_resp.get("token")
        assert_true(bool(issued_token), "Token was issued on register")

        # 2.1 重复注册报错
        ws_send(ws1, json.dumps({"type": "register", "username": test_user, "password": "anypassword"}))
        dup_resp = ws_recv(ws1)
        assert_true(dup_resp.get("type") == "error", "Duplicate username rejected with error")
        ws1.close()

        # 3. Test WebSocket: 密码登录
        print("\n--- 3. 测试密码登录 (login with password) ---")
        ws2 = ws_connect(default_port)
        # 错误密码
        ws_send(ws2, json.dumps({"type": "login", "nickname": test_user, "password": "wrongpassword"}))
        wrong_resp = ws_recv(ws2)
        assert_true(wrong_resp.get("type") == "error", "Wrong password rejected with error")

        # 免密尝试已注册账号（应被要求密码）
        ws_send(ws2, json.dumps({"type": "login", "nickname": test_user}))
        nopass_resp = ws_recv(ws2)
        assert_true(nopass_resp.get("type") == "error", "No-password login on registered account rejected")

        # 正确密码登录
        ws_send(ws2, json.dumps({"type": "login", "nickname": test_user, "password": "mypassword123"}))
        login_resp = ws_recv(ws2)
        assert_true(login_resp.get("type") == "login_success", "Correct password returns login_success")
        login_token = login_resp.get("token")
        assert_true(bool(login_token), "login_success provides session token")
        
        # 接收上线广播
        bcast_resp = ws_recv(ws2)
        assert_true(bcast_resp.get("type") == "system", "Received online system broadcast")

        # 4. Test WebSocket: 消息 ACK 回执
        print("\n--- 4. 测试消息到达确认 (ACK) ---")
        my_msg_id = f"msg_{int(time.time())}_abc"
        ws_send(ws2, json.dumps({
            "type": "chat",
            "text": "Hello World with ACK",
            "msg_id": my_msg_id
        }))
        
        # ws2 应收到 ack
        ack_resp = ws_recv(ws2)
        assert_true(ack_resp.get("type") == "ack", "Received ACK frame")
        assert_true(ack_resp.get("msg_id") == my_msg_id, "ACK msg_id matches sent msg_id")
        assert_true(ack_resp.get("status") == "ok", "ACK status == 'ok'")

        # 接收广播出来的聊天消息
        chat_bcast = ws_recv(ws2)
        assert_true(chat_bcast.get("type") == "chat", "Received chat broadcast")
        ws2.close()
        time.sleep(0.5)

        # 5. Test WebSocket: Token 恢复会话 (auth_token)
        print("\n--- 5. 测试 Token 恢复会话 (auth_token) ---")
        ws3 = ws_connect(default_port)
        ws_send(ws3, json.dumps({"type": "auth_token", "token": login_token}))
        token_resp = ws_recv(ws3)
        assert_true(token_resp.get("type") == "login_success", "auth_token returns login_success")
        assert_true(token_resp.get("nickname") == test_user, "Token restored correct user session")
        ws3.close()

        # 6. Test WebSocket: 游客快捷登录
        print("\n--- 6. 测试游客快速体验通道 (Guest login) ---")
        ws4 = ws_connect(default_port)
        guest_name = f"guest_{int(time.time())}"
        ws_send(ws4, json.dumps({"type": "login", "nickname": guest_name}))
        guest_resp = ws_recv(ws4)
        assert_true(guest_resp.get("type") == "login_success", "Guest login returns login_success")
        assert_true(guest_resp.get("nickname") == guest_name, "Guest nickname set correctly")
        ws4.close()

        # 7. Test /api/history 过滤
        print("\n--- 7. 测试 /api/history 频道与群组过滤 ---")
        with urllib.request.urlopen(f"http://127.0.0.1:{default_port}/api/history?channel=global&limit=10") as res:
            assert_true(res.status == 200, "HTTP 200 on /api/history?channel=global")
            history_items = json.loads(res.read().decode())
            assert_true(isinstance(history_items, list), "History returns JSON list")
            print(f"  Retrieved {len(history_items)} global history messages.")

    finally:
        server_proc.kill()
        server_proc.wait()

    print(f"\n测试完成: 失败数 {failures}")
    if failures > 0:
        sys.exit(1)
    else:
        print("🎉 全部成熟度进阶测试 100% 通过！")

if __name__ == "__main__":
    main()
