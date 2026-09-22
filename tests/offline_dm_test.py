import asyncio
import json
import os
import subprocess
import sys
import time
import urllib.request
import websockets

PORT = 8092
SERVER_EXE = os.path.abspath("build/chatroom_server_v2.exe")

async def test_offline_dm_and_storage():
    print(f"[*] Starting server on port {PORT}...")
    proc = subprocess.Popen([SERVER_EXE, str(PORT)])
    time.sleep(2.0)

    ws_url = f"ws://127.0.0.1:{PORT}/ws"
    http_url = f"http://127.0.0.1:{PORT}"

    try:
        # 1. Health check
        with urllib.request.urlopen(f"{http_url}/api/health", timeout=3) as res:
            health = json.loads(res.read().decode("utf-8"))
            print("[+] Initial health:", health)
            assert health["status"] == "ok"

        async def wait_for_type(ws, target_type, timeout=4.0):
            deadline = time.time() + timeout
            while time.time() < deadline:
                raw = await asyncio.wait_for(ws.recv(), timeout=deadline - time.time())
                f = json.loads(raw)
                if f.get("type") == target_type:
                    return f
            raise TimeoutError(f"Timed out waiting for frame type: {target_type}")

        suffix = str(int(time.time() * 1000))
        alice_name = f"Alice_{suffix}"
        bob_name = f"Bob_{suffix}"

        # 2. Register Alice
        print(f"[*] Registering {alice_name}...")
        async with websockets.connect(ws_url) as ws_alice:
            await ws_alice.send(json.dumps({"type": "register", "username": alice_name, "password": "password123"}))
            resp = await wait_for_type(ws_alice, "register_success")
            print("[+] Alice register resp:", resp)
            alice_token = resp.get("token")

            # Login Alice
            await ws_alice.send(json.dumps({"type": "auth_token", "token": alice_token}))
            login_resp = await wait_for_type(ws_alice, "login_success")
            assert login_resp.get("type") == "login_success"

            # 3. Register Bob
            print(f"[*] Registering {bob_name}...")
            async with websockets.connect(ws_url) as ws_bob:
                await ws_bob.send(json.dumps({"type": "register", "username": bob_name, "password": "password456"}))
                resp_b = await wait_for_type(ws_bob, "register_success")
                bob_token = resp_b.get("token")
                # Bob closes connection (goes offline)
            print(f"[+] {bob_name} registered and offline.")

            # Check /api/users endpoint
            with urllib.request.urlopen(f"{http_url}/api/users", timeout=3) as res:
                users_list = json.loads(res.read().decode("utf-8"))
                print("[+] Users list from /api/users:", users_list)
                alice_info = next(u for u in users_list if u["username"] == alice_name)
                bob_info = next(u for u in users_list if u["username"] == bob_name)
                assert alice_info["online"] is True, "Alice should be online"
                assert bob_info["online"] is False, "Bob should be offline"

            # 4. Alice sends offline DM to Bob
            print(f"[*] Alice sends DM to offline {bob_name}...")
            msg_id = f"msg_offline_{suffix}"
            await ws_alice.send(json.dumps({
                "type": "dm",
                "to": bob_name,
                "text": "Hello Bob! Please check this when you are back.",
                "msg_id": msg_id
            }))

            ack_frame = await wait_for_type(ws_alice, "ack")
            print("[+] Alice received offline ACK:", ack_frame)
            assert ack_frame.get("status") == "offline_queued", f"Expected offline_queued, got {ack_frame}"
            assert "离线" in ack_frame.get("info", ""), "ACK should contain offline indication"

        # 5. Check unread endpoint for Bob
        with urllib.request.urlopen(f"{http_url}/api/unread?username={bob_name}", timeout=3) as res:
            unread = json.loads(res.read().decode("utf-8"))
            print("[+] Bob unread status:", unread)
            assert unread["total"] == 1
            assert unread["counts"][alice_name] == 1

        # 6. Bob comes online and receives unread_sync
        print("[*] Bob reconnecting...")
        async with websockets.connect(ws_url) as ws_bob:
            await ws_bob.send(json.dumps({"type": "auth_token", "token": bob_token}))
            
            sync_frame = await wait_for_type(ws_bob, "unread_sync")
            print("[+] Bob received upon login:", sync_frame)
            assert sync_frame["total"] == 1
            assert sync_frame["counts"][alice_name] == 1
            assert len(sync_frame["messages"]) == 1
            assert "Hello Bob" in sync_frame["messages"][0]["text"]

            # 7. Bob marks messages as read
            print(f"[*] Bob marks DMs from {alice_name} as read...")
            await ws_bob.send(json.dumps({"type": "mark_read", "from": alice_name}))
            read_ack = await wait_for_type(ws_bob, "mark_read_ack")
            print("[+] Mark read ack:", read_ack)
            assert read_ack.get("type") == "mark_read_ack"
            assert read_ack.get("from") == alice_name

        # 8. Verify unread count is now 0
        with urllib.request.urlopen(f"{http_url}/api/unread?username={bob_name}", timeout=3) as res:
            unread_after = json.loads(res.read().decode("utf-8"))
            print("[+] Bob unread after mark_read:", unread_after)
            assert unread_after["total"] == 0

        # 9. Check final health stats showing stored messages and accounts
        with urllib.request.urlopen(f"{http_url}/api/health", timeout=3) as res:
            final_health = json.loads(res.read().decode("utf-8"))
            print("[+] Final health check:", final_health)
            assert final_health["stored_messages"] >= 1
            assert final_health["registered_accounts"] >= 2

        print("\n==================================================")
        print(">>> ALL PHASE 2 MATURITY TESTS PASSED 100%! <<<")
        print("==================================================\n")

    finally:
        print("[*] Terminating test server...")
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except Exception:
            proc.kill()

if __name__ == "__main__":
    asyncio.run(test_offline_dm_and_storage())
