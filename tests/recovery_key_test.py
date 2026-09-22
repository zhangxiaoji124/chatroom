#!/usr/bin/env python3
"""
端到端自动化测试：密保恢复码（Recovery Key）自助找回与重置密码测试
"""

import time
import json
import urllib.request
import urllib.error
import websocket

SERVER_HTTP = "http://127.0.0.1:8080"
SERVER_WS = "ws://127.0.0.1:8080/ws"

def test_recovery_key_flow():
    print(">>> 1. 验证已有账号获取接口包含 recovery_key...")
    req = urllib.request.Request(f"{SERVER_HTTP}/api/users")
    with urllib.request.urlopen(req, timeout=3) as resp:
        users = json.loads(resp.read().decode("utf-8"))
        assert len(users) > 0, "用户列表不能为空"
        assert "recovery_key" in users[0], "用户必须包含 recovery_key 字段"
        print(f"    [OK] 成功获取用户列表，首个用户密保码: {users[0].get('recovery_key')}")

    test_user = f"RecUser_{int(time.time()*1000)}"
    orig_pass = "orig_password_123"
    new_pass = "new_secret_888"

    print(f"\n>>> 2. WebSocket 注册新用户: {test_user}...")
    ws = websocket.create_connection(SERVER_WS, timeout=5)
    ws.send(json.dumps({
        "type": "register",
        "username": test_user,
        "password": orig_pass
    }))
    resp = json.loads(ws.recv())
    assert resp.get("type") == "register_success", f"注册失败: {resp}"
    recovery_key = resp.get("recovery_key")
    assert recovery_key and recovery_key.startswith("REC-"), f"未返回有效恢复码: {recovery_key}"
    print(f"    [OK] 注册成功，获取到专属恢复码: {recovery_key}")
    ws.close()

    print("\n>>> 3. 测试错误恢复码请求 POST /api/auth/reset_password...")
    bad_payload = json.dumps({
        "username": test_user,
        "recovery_key": "REC-WRONG-KEY",
        "new_password": new_pass
    }).encode("utf-8")
    req_bad = urllib.request.Request(
        f"{SERVER_HTTP}/api/auth/reset_password",
        data=bad_payload,
        headers={"Content-Type": "application/json"}
    )
    try:
        urllib.request.urlopen(req_bad, timeout=3)
        assert False, "错误恢复码应该被拒绝"
    except urllib.error.HTTPError as e:
        assert e.code == 400, f"期望 400 状态码，实际: {e.code}"
        err_data = json.loads(e.read().decode("utf-8"))
        print(f"    [OK] 错误恢复码如期被拒绝: {err_data.get('error')}")

    print("\n>>> 4. 测试正确恢复码重置密码 (大小写不敏感与去横线容错)...")
    # 测试容错输入：小写且去除横杠
    flex_key = recovery_key.lower().replace("-", "")
    good_payload = json.dumps({
        "username": test_user,
        "recovery_key": flex_key,
        "new_password": new_pass
    }).encode("utf-8")
    req_good = urllib.request.Request(
        f"{SERVER_HTTP}/api/auth/reset_password",
        data=good_payload,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req_good, timeout=3) as resp:
        assert resp.code == 200, f"重置返回非 200: {resp.code}"
        res_data = json.loads(resp.read().decode("utf-8"))
        assert res_data.get("status") == "ok", f"状态非 ok: {res_data}"
        print(f"    [OK] 密保恢复码匹配成功，返回: {res_data.get('msg')}")

    print("\n>>> 5. 验证旧密码登录被拒绝...")
    ws = websocket.create_connection(SERVER_WS, timeout=5)
    ws.send(json.dumps({
        "type": "login",
        "nickname": test_user,
        "password": orig_pass
    }))
    resp = json.loads(ws.recv())
    assert resp.get("type") == "error", f"旧密码本应失败，实际返回: {resp}"
    print(f"    [OK] 旧密码已失效: {resp.get('msg')}")
    ws.close()

    print("\n>>> 6. 验证新密码登录成功...")
    ws = websocket.create_connection(SERVER_WS, timeout=5)
    ws.send(json.dumps({
        "type": "login",
        "nickname": test_user,
        "password": new_pass
    }))
    resp = json.loads(ws.recv())
    assert resp.get("type") == "login_success", f"新密码登录失败: {resp}"
    assert resp.get("nickname") == test_user
    print(f"    [OK] 新密码成功登录系统！Token 已签发: {resp.get('token')[:16]}...")
    ws.close()

    print("\n==================================================")
    print("🎉 ALL RECOVERY KEY TESTS PASSED! 密保功能全部通过！")
    print("==================================================")

if __name__ == "__main__":
    test_recovery_key_flow()
