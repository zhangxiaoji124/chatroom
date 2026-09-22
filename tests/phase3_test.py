"""
Phase 3 Features Automated Test Suite:
1. Rate Limiting (Message Flooding & Brute-force Login Lockout)
2. Full-text Search (/api/search)
3. 2-minute Message Recall (Recall frame & Broadcast)
4. Prometheus Metrics (/api/metrics)
"""

import json
import time
import socket
import urllib.request
import urllib.parse
import subprocess
import sys
import websocket

SERVER_PORT = 19093
HTTP_BASE = f"http://127.0.0.1:{SERVER_PORT}"
WS_URL = f"ws://127.0.0.1:{SERVER_PORT}/ws"

def wait_for_server(port, timeout=10):
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=1)
            s.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False

def http_get(path):
    req = urllib.request.Request(f"{HTTP_BASE}{path}")
    with urllib.request.urlopen(req, timeout=5) as res:
        return res.getcode(), res.read().decode("utf-8"), res.headers

def test_phase3():
    print("=" * 60)
    print("Starting Phase 3 Test Suite...")
    print("=" * 60)

    # 1. Start Server
    subprocess.run(["powershell", "-Command", "Stop-Process -Name chatroom_server_v2 -Force -ErrorAction SilentlyContinue"], capture_output=True)
    server_proc = subprocess.Popen(["build/chatroom_server_v2.exe", str(SERVER_PORT)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    try:
        if not wait_for_server(SERVER_PORT):
            print("[FAIL] Server failed to start on port", SERVER_PORT)
            sys.exit(1)
        print("[PASS] Server started successfully.")

        # Test 1: Metrics endpoint
        code, body, headers = http_get("/api/metrics")
        assert code == 200, f"Metrics failed with code {code}"
        assert "chatroom_uptime_seconds" in body, "Missing uptime metric"
        assert "chatroom_online_users" in body, "Missing online users metric"
        assert "chatroom_stored_messages" in body, "Missing stored messages metric"
        print("[PASS] Prometheus /api/metrics endpoint returns valid OpenMetrics data.")

        # Test 2: Rate Limiting on Message Flooding
        ws1 = websocket.create_connection(WS_URL, timeout=5)
        ws1.send(json.dumps({"type": "login", "nickname": "SpammerSam"}))
        login_resp = json.loads(ws1.recv())
        assert login_resp["type"] == "login_success"

        # Send burst of 15 messages rapidly
        flood_blocked = False
        for i in range(15):
            ws1.send(json.dumps({
                "type": "chat",
                "text": f"Spam message {i}",
                "msg_id": f"spam_{i}_{time.time()}"
            }))
            time.sleep(0.01)

        # Read back replies, expect error frame with rate limit notice
        ws1.settimeout(2.0)
        for _ in range(25):
            try:
                frame = json.loads(ws1.recv())
                if frame.get("type") == "error" and "限流" in frame.get("msg", ""):
                    flood_blocked = True
                    break
            except websocket.WebSocketTimeoutException:
                break

        assert flood_blocked, "Expected flood rate limiter to trigger error frame!"
        print("[PASS] Message flooding rate limiter successfully blocked excessive message rate.")
        ws1.close()

        # Test 3: Brute-force Login Protection (5 failed attempts -> lockout)
        # Register a test target account
        target_user = f"Target_{int(time.time()*1000)}"
        ws_reg = websocket.create_connection(WS_URL, timeout=5)
        ws_reg.send(json.dumps({
            "type": "register",
            "username": target_user,
            "password": "correct_password_123"
        }))
        reg_resp = json.loads(ws_reg.recv())
        assert reg_resp.get("type") == "register_success", f"Registration failed: {reg_resp}"
        ws_reg.close()

        # Attempt 5 wrong password logins
        locked_out = False
        for attempt in range(5):
            ws_brute = websocket.create_connection(WS_URL, timeout=5)
            ws_brute.send(json.dumps({
                "type": "login",
                "nickname": target_user,
                "password": "wrong_password"
            }))
            resp = json.loads(ws_brute.recv())
            if "锁定" in resp.get("msg", ""):
                locked_out = True
            ws_brute.close()

        # The 6th attempt MUST be blocked by IP lockout
        ws_locked = websocket.create_connection(WS_URL, timeout=5)
        ws_locked.send(json.dumps({
            "type": "login",
            "nickname": target_user,
            "password": "even_with_correct_now"
        }))
        resp = json.loads(ws_locked.recv())
        assert resp.get("type") == "error" and ("锁定" in resp.get("msg", "") or "频繁" in resp.get("msg", "")), f"Expected lockout error, got {resp}"
        print("[PASS] Brute-force login attack successfully locked out client IP.")
        ws_locked.close()

        # Test 4: Search and Recall
        server_proc.terminate()
        try:
            server_proc.wait(timeout=2)
        except:
            server_proc.kill()
        subprocess.run(["powershell", "-Command", "Stop-Process -Name chatroom_server_v2 -Force -ErrorAction SilentlyContinue"], capture_output=True)
        time.sleep(0.5)

        PORT_2 = 19094
        server_proc = subprocess.Popen(["build/chatroom_server_v2.exe", str(PORT_2)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert wait_for_server(PORT_2), "Server restart failed on PORT_2"

        WS_URL_2 = f"ws://127.0.0.1:{PORT_2}/ws"
        HTTP_BASE_2 = f"http://127.0.0.1:{PORT_2}"

        def http_get_2(path):
            req = urllib.request.Request(f"{HTTP_BASE_2}{path}")
            with urllib.request.urlopen(req, timeout=5) as res:
                return res.getcode(), res.read().decode("utf-8"), res.headers

        ws_alice = websocket.create_connection(WS_URL_2, timeout=5)
        ws_alice.send(json.dumps({"type": "login", "nickname": "AliceSearcher"}))
        assert json.loads(ws_alice.recv())["type"] == "login_success"

        ws_bob = websocket.create_connection(WS_URL_2, timeout=5)
        ws_bob.send(json.dumps({"type": "login", "nickname": "BobListener"}))
        assert json.loads(ws_bob.recv())["type"] == "login_success"

        # Alice sends unique message
        unique_secret = f"QuantumAntigravity_{int(time.time())}"
        test_msg_id = f"recall_test_{int(time.time())}"
        ws_alice.send(json.dumps({
            "type": "chat",
            "text": f"Found the secret key: {unique_secret}",
            "msg_id": test_msg_id
        }))
        time.sleep(0.2)

        # Bob receives it
        msg_received = False
        ws_bob.settimeout(2.0)
        while True:
            try:
                frame = json.loads(ws_bob.recv())
                if frame.get("type") == "chat" and unique_secret in frame.get("text", ""):
                    msg_received = True
                    break
            except websocket.WebSocketTimeoutException:
                break
        assert msg_received, "Bob did not receive Alice's chat message"

        # Test Search before recall
        q_url = f"/api/search?q={urllib.parse.quote(unique_secret)}"
        code, body, _ = http_get_2(q_url)
        search_data = json.loads(body)
        assert search_data["count"] >= 1, f"Search failed to find {unique_secret}: {search_data}"
        assert unique_secret in search_data["results"][0]["text"]
        print(f"[PASS] Full-text search successfully found message with query '{unique_secret}'.")

        # Bob attempts to recall Alice's message (MUST fail)
        ws_bob.send(json.dumps({
            "type": "recall",
            "msg_id": test_msg_id
        }))
        bob_err = json.loads(ws_bob.recv())
        assert bob_err.get("type") == "error" and ("自己" in bob_err.get("msg", "") or "无权" in bob_err.get("msg", "")), f"Expected unauthorized error, got {bob_err}"
        print("[PASS] Unauthorized message recall rejected.")

        # Alice recalls her own message (MUST succeed and broadcast recall frame)
        ws_alice.send(json.dumps({
            "type": "recall",
            "msg_id": test_msg_id
        }))
        time.sleep(0.2)

        bob_got_recall = False
        while True:
            try:
                frame = json.loads(ws_bob.recv())
                if frame.get("type") == "recall" and frame.get("msg_id") == test_msg_id:
                    bob_got_recall = True
                    break
            except websocket.WebSocketTimeoutException:
                break
        assert bob_got_recall, "Bob did not receive the broadcasted recall frame!"
        print("[PASS] Message recall broadcasted successfully to WebSocket peers.")

        # Test Search after recall (MUST be excluded)
        code, body, _ = http_get_2(q_url)
        search_data_after = json.loads(body)
        assert search_data_after["count"] == 0, f"Recalled message still appeared in search! {search_data_after}"
        print("[PASS] Recalled message is cleanly excluded from search results.")

        ws_alice.close()
        ws_bob.close()

    finally:
        server_proc.terminate()
        try:
            server_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server_proc.kill()
        subprocess.run(["powershell", "-Command", "Stop-Process -Name chatroom_server_v2 -Force -ErrorAction SilentlyContinue"], capture_output=True)

    print("\n" + "=" * 60)
    print("ALL PHASE 3 INTEGRATION TESTS PASSED (100%)")
    print("=" * 60)

if __name__ == "__main__":
    test_phase3()
