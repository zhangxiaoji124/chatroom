#!/usr/bin/env python3
"""End-to-end checks for global chat and the DeepSeek-backed AI bot."""

import json
import os
import subprocess
import time
import urllib.request
import uuid

from websockets.sync.client import connect


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "build", "chatroom_server.exe")
MESSAGES = os.path.join(ROOT, "data", "messages.jsonl")
WS_URL = "ws://127.0.0.1:8080/ws"


def wait_until_ready():
    for _ in range(50):
        try:
            with urllib.request.urlopen(
                "http://127.0.0.1:8080/api/status", timeout=0.5
            ) as response:
                if json.load(response).get("status") == "ok":
                    return
        except Exception:
            time.sleep(0.2)
    raise AssertionError("server did not become ready")


def receive_json(socket, timeout):
    return json.loads(socket.recv(timeout=timeout))


def main():
    nickname = "ai-e2e-" + uuid.uuid4().hex[:8]
    ordinary_text = "ordinary-" + uuid.uuid4().hex
    process = subprocess.Popen(
        [SERVER], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    try:
        wait_until_ready()
        with connect(WS_URL) as socket:
            socket.send(
                json.dumps(
                    {"type": "login", "nickname": nickname}, ensure_ascii=False
                )
            )
            login_notice = receive_json(socket, 5)
            assert login_notice.get("type") == "system", login_notice

            socket.send(
                json.dumps(
                    {"type": "chat", "text": ordinary_text}, ensure_ascii=False
                )
            )
            ordinary = receive_json(socket, 5)
            assert ordinary.get("type") == "chat", ordinary
            assert ordinary.get("from") == nickname, ordinary
            assert ordinary.get("text") == ordinary_text, ordinary
            try:
                unexpected = receive_json(socket, 1)
            except TimeoutError:
                unexpected = None
            assert unexpected is None, (
                "ordinary chat unexpectedly caused an extra frame", unexpected
            )

            socket.send(
                json.dumps(
                    {"type": "chat", "text": "  @机器人 你好  "},
                    ensure_ascii=False,
                )
            )

            ai_reply = None
            friendly_failure = None
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                frame = receive_json(socket, max(0.1, deadline - time.monotonic()))
                if frame.get("type") == "chat" and frame.get("from") == "AI助手":
                    ai_reply = frame
                    break
                if frame.get("type") == "system" and (
                    "暂时无法响应" in frame.get("msg", "")
                    or "AI 未配置" in frame.get("msg", "")
                ):
                    friendly_failure = frame
                    break

            assert ai_reply is not None or friendly_failure is not None, (
                "AI produced neither a reply nor a friendly failure"
            )

        if ai_reply is not None:
            with open(MESSAGES, encoding="utf-8") as message_file:
                stored = [json.loads(line) for line in message_file if line.strip()]
            assert any(
                item.get("type") == "chat"
                and item.get("from") == "AI助手"
                and item.get("text") == ai_reply.get("text")
                for item in stored
            ), "AI reply was not persisted in messages.jsonl"

            with urllib.request.urlopen(
                "http://127.0.0.1:8080/api/admin/messages?limit=10000", timeout=3
            ) as response:
                admin_messages = json.load(response)
            assert any(
                item.get("from") == "AI助手"
                and item.get("text") == ai_reply.get("text")
                for item in admin_messages
            ), "AI reply was not visible through /api/admin/messages"
            print("PASS: DeepSeek returned and persisted an AI assistant reply")
        else:
            print("PASS: AI request followed the friendly failure path")
        print("PASS: ordinary chat did not trigger an AI frame")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
