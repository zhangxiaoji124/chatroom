#!/usr/bin/env python3
"""
Chatroom User & Password Management Tool
聊天室账号与密码管理工具（查看账号、重置密码、清理测试账号）

用法：
    # 1. 查看所有注册账号
    python scripts/manage_users.py list

    # 2. 为任意账号重置新密码
    python scripts/manage_users.py reset <username> <new_password>
    例: python scripts/manage_users.py reset zhangxiaoji 123456

    # 3. 添加新账号
    python scripts/manage_users.py add <username> <password>

    # 4. 一键清理自动化测试生成的临时账号 (保留自定义账号)
    python scripts/manage_users.py clean-tests
"""

import sys
import os
import json
import time
import secrets
import hashlib
import sqlite3
import urllib.request
import urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
USERS_JSON = os.path.join(ROOT, "data", "users.json")
DB_PATH = os.path.join(ROOT, "data", "chatroom.db")
SERVER_API = "http://127.0.0.1:8080"

def hash_pw(password, salt):
    return hashlib.sha256((password + salt).encode("utf-8")).hexdigest()

def list_users():
    print("=" * 105)
    print("                              CHATROOM 注册用户与密保列表")
    print("=" * 105)
    print(f"{'用户名':<22} | {'注册时间':<19} | {'密保恢复码':<16} | 密码加密状态")
    print("-" * 105)

    users = []
    if os.path.exists(USERS_JSON):
        try:
            with open(USERS_JSON, "r", encoding="utf-8") as f:
                users = json.load(f)
        except Exception:
            pass

    if not users and os.path.exists(DB_PATH):
        try:
            conn = sqlite3.connect(DB_PATH)
            cur = conn.cursor()
            cur.execute("SELECT username, password_hash, salt, created_at, last_login, recovery_key FROM users;")
            for row in cur.fetchall():
                users.append({
                    "username": row[0],
                    "password_hash": row[1],
                    "salt": row[2],
                    "created_at": row[3],
                    "last_login": row[4],
                    "recovery_key": row[5] if len(row) > 5 else ""
                })
            conn.close()
        except Exception:
            pass

    if not users:
        print("暂无注册用户")
        return

    for u in users:
        created = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(u.get("created_at", 0) / 1000)) if u.get("created_at") else "-"
        rec_key = u.get("recovery_key") or "--"
        status = "SHA-256加盐安全保护 (不可逆)"
        print(f"{u.get('username', ''):<22} | {created:<19} | {rec_key:<16} | {status}")
    print("=" * 105)
    print(f"总计账号数: {len(users)}")
    print("\n💡 提示: 密码采用加盐单向哈希加密，数据库管理员亦无法反查明文。")
    print("       如需登录指定账号，可直接运行: python scripts/manage_users.py reset <用户名> <新密码>")

def reset_password(username, new_password):
    if len(new_password) < 3:
        print("错误: 密码长度至少需要 3 位")
        return False

    # 1. 优先尝试通过服务端 API 重置 (如果服务端正在运行)
    try:
        req = urllib.request.Request(
            f"{SERVER_API}/api/admin/reset_password",
            data=json.dumps({"username": username, "new_password": new_password}).encode("utf-8"),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=2) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if data.get("status") == "ok":
                print(f"✅ 成功通过服务端 API 将用户 [{username}] 密码重置为: {new_password}")
                return True
    except Exception:
        pass

    # 2. 如果服务未启动或 API 失败，直接更新 users.json 与 SQLite
    found = False
    salt = secrets.token_hex(8)
    pw_hash = hash_pw(new_password, salt)
    now = int(time.time() * 1000)

    if os.path.exists(USERS_JSON):
        try:
            with open(USERS_JSON, "r", encoding="utf-8") as f:
                users = json.load(f)
            for u in users:
                if u.get("username") == username:
                    u["salt"] = salt
                    u["password_hash"] = pw_hash
                    found = True
                    break
            if found:
                with open(USERS_JSON, "w", encoding="utf-8") as f:
                    json.dump(users, f, indent=2, ensure_ascii=False)
        except Exception as e:
            print(f"更新 users.json 失败: {e}")

    if os.path.exists(DB_PATH):
        try:
            conn = sqlite3.connect(DB_PATH)
            cur = conn.cursor()
            cur.execute("UPDATE users SET password_hash = ?, salt = ? WHERE username = ?;", (pw_hash, salt, username))
            if cur.rowcount > 0:
                found = True
            conn.commit()
            conn.close()
        except Exception as e:
            print(f"更新 SQLite 失败: {e}")

    if found:
        print(f"✅ 成功将用户 [{username}] 的密码重置为: {new_password}")
        print(f"👉 现在可以用账号: {username}，密码: {new_password} 登录聊天室。")
        return True
    else:
        print(f"❌ 未找到用户名为 [{username}] 的账号。")
        return False

def clean_test_accounts():
    """清理自动化测试生成的临时账号，保留用户自己的账号"""
    test_prefixes = ("Target_", "Alice_", "Bob_", "test_", "LockoutTarget", "SpammerSam")
    removed = []

    if os.path.exists(USERS_JSON):
        with open(USERS_JSON, "r", encoding="utf-8") as f:
            users = json.load(f)
        kept = []
        for u in users:
            uname = u.get("username", "")
            if any(uname.startswith(p) or uname == p for p in test_prefixes):
                removed.append(uname)
            else:
                kept.append(u)
        with open(USERS_JSON, "w", encoding="utf-8") as f:
            json.dump(kept, f, indent=2, ensure_ascii=False)

    if os.path.exists(DB_PATH):
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        for uname in removed:
            cur.execute("DELETE FROM users WHERE username = ?;", (uname,))
        conn.commit()
        conn.close()

    print(f"🧹 已清理 {len(removed)} 个测试账号: {', '.join(removed) if removed else '无'}")
    print("当前剩余用户账号：")
    list_users()

def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help", "help"):
        print(__doc__)
        return

    cmd = sys.argv[1].lower()
    if cmd in ("list", "ls"):
        list_users()
    elif cmd in ("reset", "set-password", "passwd"):
        if len(sys.argv) < 4:
            print("用法: python scripts/manage_users.py reset <用户名> <新密码>")
            return
        reset_password(sys.argv[2], sys.argv[3])
    elif cmd in ("clean-tests", "clean"):
        clean_test_accounts()
    else:
        print(f"未知命令: {cmd}")
        print(__doc__)

if __name__ == "__main__":
    main()
