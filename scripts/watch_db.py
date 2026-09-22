#!/usr/bin/env python3
"""
Chatroom Database Console Live Watcher
实时控制台监控 SQLite 数据库消息流变动工具

支持：
1. HTTP 接口监听 (默认: http://127.0.0.1:8080/api/db/changes)
2. 本地 SQLite 文件直接监听 (--db data/chatroom.db)
3. 历史记录回溯 (--tail N)
4. ANSI 彩色高亮打印 (INSERT / RECALL / READ 状态流)

用法：
    python scripts/watch_db.py
    python scripts/watch_db.py --tail 10
    python scripts/watch_db.py --port 8080
    python scripts/watch_db.py --db data/chatroom.db
"""

import sys
import time
import argparse
import urllib.request
import urllib.error
import json
import sqlite3
import os
import functools

print = functools.partial(print, flush=True)

# ANSI 颜色定义
COLOR_RESET = "\033[0m"
COLOR_BOLD = "\033[1m"
COLOR_GREEN = "\033[32m"
COLOR_YELLOW = "\033[33m"
COLOR_BLUE = "\033[34m"
COLOR_MAGENTA = "\033[35m"
COLOR_CYAN = "\033[36m"
COLOR_GRAY = "\033[90m"

def print_banner(mode_str, target_str):
    print(f"{COLOR_BOLD}{COLOR_CYAN}=" * 80)
    print("      💬 CHATROOM DATABASE REALTIME CONSOLE MONITOR (SQLite Watcher)")
    print(f"{COLOR_CYAN}=" * 80 + COLOR_RESET)
    print(f"模式: {COLOR_GREEN}{mode_str}{COLOR_RESET} | 目标: {COLOR_YELLOW}{target_str}{COLOR_RESET}")
    print(f"按 {COLOR_BOLD}Ctrl+C{COLOR_RESET} 退出监控\n")
    print(f"{COLOR_GRAY}{'-' * 80}{COLOR_RESET}")

def format_record(r):
    msg_id = r.get("id")
    time_str = r.get("time", time.strftime("%H:%M:%S"))
    channel = r.get("channel", "global")
    msg_type = r.get("type", "chat")
    from_user = r.get("from", "未知")
    to_user = r.get("to")
    group_name = r.get("group")
    text = r.get("text", "")
    is_read = r.get("is_read", 0)
    is_recalled = r.get("is_recalled", False)

    if group_name:
        dest = f"#{group_name}"
    elif to_user:
        read_badge = f"{COLOR_GREEN}[已读]{COLOR_RESET}" if is_read else f"{COLOR_YELLOW}[未读]{COLOR_RESET}"
        dest = f"@{to_user} {read_badge}"
    else:
        dest = f"{COLOR_GRAY}[全局]{COLOR_RESET}"

    if is_recalled:
        action_tag = f"{COLOR_MAGENTA}[DB:RECALL]{COLOR_RESET}"
        body = f"{COLOR_MAGENTA}🗑️ 消息已被撤回{COLOR_RESET}"
    else:
        action_tag = f"{COLOR_GREEN}[DB:INSERT]{COLOR_RESET}"
        body = text if text else f"{COLOR_GRAY}[{msg_type}]{COLOR_RESET}"

    return f"{COLOR_GRAY}[{time_str}]{COLOR_RESET} {action_tag} #{msg_id} {COLOR_BOLD}{from_user}{COLOR_RESET} ➔ {dest}: {body}"

def watch_via_api(host, port, tail_count):
    base_url = f"http://{host}:{port}"
    changes_url = f"{base_url}/api/db/changes"
    print_banner("HTTP API 实时轮询", changes_url)

    last_id = 0
    # 首先获取当前最大 ID 或初始记录
    try:
        url = f"{changes_url}?limit={tail_count}" if tail_count > 0 else f"{changes_url}?limit=1"
        req = urllib.request.Request(url, headers={"User-Agent": "DbWatcher/1.0"})
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            records = data.get("records", [])
            if tail_count > 0:
                print(f"{COLOR_YELLOW}--- 最近 {len(records)} 条历史记录 ---{COLOR_RESET}")
                for r in records:
                    print(format_record(r))
                    last_id = max(last_id, r.get("id", 0))
                print(f"{COLOR_YELLOW}--- 开始监听实时变动 ---{COLOR_RESET}\n")
            elif records:
                last_id = max(r.get("id", 0) for r in records)
    except Exception as e:
        print(f"{COLOR_YELLOW}[提示] 初始连接服务器 ({base_url}) 未就绪: {e}，将持续重试...{COLOR_RESET}")

    while True:
        try:
            req_url = f"{changes_url}?since_id={last_id}&limit=50"
            req = urllib.request.Request(req_url, headers={"User-Agent": "DbWatcher/1.0"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                new_records = data.get("records", [])
                for r in new_records:
                    print(format_record(r))
                    last_id = max(last_id, r.get("id", 0))
        except urllib.error.URLError:
            pass
        except Exception as e:
            pass
        time.sleep(0.5)

def watch_via_sqlite(db_path, tail_count):
    if not os.path.exists(db_path):
        print(f"错误: 数据库文件不存在: {db_path}")
        sys.exit(1)

    print_banner("本地 SQLite 直连监听", db_path)
    conn = sqlite3.connect(db_path, timeout=5)
    conn.execute("PRAGMA journal_mode = WAL;")

    cursor = conn.cursor()
    last_id = 0

    if tail_count > 0:
        cursor.execute("""
            SELECT id, msg_id, msg_type, from_user, to_user, group_name, channel, text, timestamp, is_read
            FROM messages ORDER BY id DESC LIMIT ?;
        """, (tail_count,))
        rows = cursor.fetchall()
        rows.reverse()
        print(f"{COLOR_YELLOW}--- 最近 {len(rows)} 条历史记录 ---{COLOR_RESET}")
        for row in rows:
            r = {
                "id": row[0], "msg_id": row[1], "type": row[2], "from": row[3],
                "to": row[4], "group": row[5], "channel": row[6], "text": row[7],
                "time": time.strftime("%H:%M:%S", time.localtime(row[8]/1000)),
                "is_read": row[9], "is_recalled": (row[7] == "[消息已撤回]")
            }
            print(format_record(r))
            last_id = max(last_id, r["id"])
        print(f"{COLOR_YELLOW}--- 开始监听实时变动 ---{COLOR_RESET}\n")
    else:
        cursor.execute("SELECT IFNULL(MAX(id), 0) FROM messages;")
        last_id = cursor.fetchone()[0]

    while True:
        cursor.execute("""
            SELECT id, msg_id, msg_type, from_user, to_user, group_name, channel, text, timestamp, is_read
            FROM messages WHERE id > ? ORDER BY id ASC;
        """, (last_id,))
        rows = cursor.fetchall()
        for row in rows:
            r = {
                "id": row[0], "msg_id": row[1], "type": row[2], "from": row[3],
                "to": row[4], "group": row[5], "channel": row[6], "text": row[7],
                "time": time.strftime("%H:%M:%S", time.localtime(row[8]/1000)),
                "is_read": row[9], "is_recalled": (row[7] == "[消息已撤回]")
            }
            print(format_record(r))
            last_id = max(last_id, r["id"])
        time.sleep(0.5)

def main():
    parser = argparse.ArgumentParser(description="Chatroom 控制台数据库消息变动监控")
    parser.add_argument("--host", default="127.0.0.1", help="服务器地址 (默认 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="服务器端口 (默认 8080)")
    parser.add_argument("--tail", type=int, default=5, help="启动时先输出最近 N 条记录 (默认 5)")
    parser.add_argument("--db", type=str, default="", help="直接监听本地 SQLite 数据库文件路径")
    args = parser.parse_args()

    try:
        if args.db:
            watch_via_sqlite(args.db, args.tail)
        else:
            watch_via_api(args.host, args.port, args.tail)
    except KeyboardInterrupt:
        print(f"\n{COLOR_GRAY}监控已停止。{COLOR_RESET}")

if __name__ == "__main__":
    main()
