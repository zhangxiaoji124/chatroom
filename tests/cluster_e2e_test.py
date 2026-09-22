#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分布式集群集成与端到端自动化测试 (Cluster E2E Test)
测试内容：
1. 集群节点自发现与心跳保活监测 (/api/cluster/ping, /api/cluster/nodes)
2. 跨节点公聊广播同步 (Node 1 -> Node 2)
3. 跨节点私聊定向推送 (Node 2 -> Node 1)
4. 跨节点消息撤回同步 (Node 1 -> Node 2)
5. 分布式反向代理负载均衡网关 (Gateway 8001) 的 HTTP 与 WebSocket 代理转发
"""

import os
import sys
import time
import json
import socket
import subprocess
import urllib.request

try:
    import websocket
except ImportError:
    print("[FAIL] 需要安装 websocket-client: pip install websocket-client")
    sys.exit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER_BIN = os.path.join(ROOT, "build", "chatroom_server_v2.exe")
GATEWAY_PY = os.path.join(ROOT, "scripts", "gateway.py")

failures = 0

def check(condition: bool, msg: str):
    global failures
    if condition:
        print(f"  [PASS] {msg}")
    else:
        print(f"  [FAIL] {msg}")
        failures += 1

def wait_for_port(port: int, timeout: float = 10.0) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False

def http_get(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": "ClusterTest/1.0"})
    with urllib.request.urlopen(req, timeout=3.0) as resp:
        return json.loads(resp.read().decode("utf-8"))

def ws_login(port: int, nickname: str):
    ws = websocket.WebSocket()
    ws.settimeout(5.0)
    ws.connect(f"ws://127.0.0.1:{port}/ws")
    ws.send(json.dumps({"type": "login", "nickname": nickname}))
    # 读第一帧响应
    resp = json.loads(ws.recv())
    return ws, resp

def recv_until(ws, predicate, timeout: float = 4.0):
    t0 = time.time()
    ws.settimeout(timeout)
    while time.time() - t0 < timeout:
        try:
            raw = ws.recv()
            data = json.loads(raw)
            if predicate(data):
                return data
        except Exception:
            break
    return None

def main():
    global failures
    print("=" * 60)
    print("  开始运行分布式集群多节点集成测试 (Cluster E2E Test)")
    print("=" * 60)

    if not os.path.exists(SERVER_BIN):
        print(f"[FAIL] 找不到服务端执行文件: {SERVER_BIN}")
        return 1

    procs = []

    try:
        # 1. 启动节点 1 (端口 8081, 对等节点指向 8082)
        p1 = subprocess.Popen(
            [SERVER_BIN, "8081", "--node-id", "test_node_8081", "--peers", "127.0.0.1:8082"],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        procs.append(("Node1", p1))

        # 2. 启动节点 2 (端口 8082, 对等节点指向 8081)
        p2 = subprocess.Popen(
            [SERVER_BIN, "8082", "--node-id", "test_node_8082", "--peers", "127.0.0.1:8081"],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        procs.append(("Node2", p2))

        # 3. 启动网关 (端口 8001, 后端指向 8081 和 8082)
        p_gw = subprocess.Popen(
            [sys.executable, GATEWAY_PY, "--port", "8001", "--backends", "127.0.0.1:8081,127.0.0.1:8082"],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        procs.append(("Gateway", p_gw))

        print("[Init] 等待节点 8081, 8082 与网关 8001 启动完成...")
        check(wait_for_port(8081), "Node 1 (8081) 端口启动成功")
        check(wait_for_port(8082), "Node 2 (8082) 端口启动成功")
        check(wait_for_port(8001), "Gateway (8001) 端口启动成功")

        # 等待节点心跳同步一次 (心跳周期 2 秒)
        time.sleep(3.0)

        # ===== 测试 1: 集群节点状态接口 =====
        print("\n--- 测试 1: 集群节点状态与健康保活 ---")
        n1_ping = http_get("http://127.0.0.1:8081/api/cluster/ping")
        check(n1_ping.get("status") == "ok" and n1_ping.get("node_id") == "test_node_8081",
              "Node 1 /api/cluster/ping 正常响应")

        n2_nodes = http_get("http://127.0.0.1:8082/api/cluster/nodes")
        check(n2_nodes.get("status") == "ok" and n2_nodes.get("clustered") is True,
              "Node 2 /api/cluster/nodes 识别为集群模式")
        nodes_list = n2_nodes.get("nodes", [])
        check(len(nodes_list) >= 2, f"Node 2 成功自发现包含对等节点 (共 {len(nodes_list)} 个节点)")

        peer_8081 = next((n for n in nodes_list if n.get("port") == 8081), None)
        check(peer_8081 is not None and peer_8081.get("alive") is True,
              "Node 2 成功心跳连通 Node 1 (alive == True)")

        # ===== 测试 2: 网关健康检查状态 =====
        print("\n--- 测试 2: 网关状态与负载均衡监控 ---")
        gw_status = http_get("http://127.0.0.1:8001/api/gateway/status")
        check(gw_status.get("status") == "ok", "网关 /api/gateway/status 正常响应")
        gw_nodes = gw_status.get("nodes", [])
        alive_count = sum(1 for n in gw_nodes if n.get("alive"))
        check(alive_count == 2, f"网关已检测到 2 个健康后端节点 (当前健康: {alive_count})")

        # ===== 测试 3: 跨节点公共聊天广播同步 =====
        print("\n--- 测试 3: 跨节点公共聊天广播 (Node 1 -> Node 2) ---")
        ws_alice, r_alice = ws_login(8081, "ClusterAlice")
        check(r_alice.get("type") == "login_success", "用户 Alice 连接 Node 1 登录成功")

        ws_bob, r_bob = ws_login(8082, "ClusterBob")
        check(r_bob.get("type") == "login_success", "用户 Bob 连接 Node 2 登录成功")

        # Alice 在 Node 1 发送公聊消息
        msg_text = "Hello from Node 1 to the entire cluster mesh!"
        ws_alice.send(json.dumps({
            "type": "chat",
            "text": msg_text
        }))

        # Bob 在 Node 2 应当实时接收到
        received_by_bob = recv_until(ws_bob, lambda f: f.get("type") == "chat" and f.get("text") == msg_text, timeout=3.0)
        check(received_by_bob is not None, "Node 2 上的 Bob 成功接收到来自 Node 1 的 Alice 广播消息")
        if received_by_bob:
            check(received_by_bob.get("from") == "ClusterAlice", "消息发送者正确显示为 ClusterAlice")

        # ===== 测试 4: 跨节点私聊 (DM) 定向投递 =====
        print("\n--- 测试 4: 跨节点私聊 (DM) 定向投递 (Node 2 -> Node 1) ---")
        dm_text = "Secret private message across cluster nodes"
        ws_bob.send(json.dumps({
            "type": "dm",
            "to": "ClusterAlice",
            "text": dm_text
        }))

        # Alice 在 Node 1 应当接收到定向私聊
        dm_to_alice = recv_until(ws_alice, lambda f: f.get("type") == "dm" and f.get("text") == dm_text, timeout=3.0)
        check(dm_to_alice is not None, "Node 1 上的 Alice 成功接收到 Node 2 上 Bob 发来的跨节点私聊")
        if dm_to_alice:
            check(dm_to_alice.get("from") == "ClusterBob" and dm_to_alice.get("to") == "ClusterAlice",
                  "私聊元信息 (from/to) 保持完整准确")

        # ===== 测试 5: 跨节点消息撤回同步 =====
        print("\n--- 测试 5: 跨节点消息撤回同步 ---")
        recall_target_id = f"msg_{int(time.time()*1000)}_test_recall"
        # Alice 发送带 msg_id 的富文本消息
        ws_alice.send(json.dumps({
            "type": "chat",
            "text": "This message will be recalled across cluster",
            "msg_id": recall_target_id,
            "sticker": "happy"
        }))
        # 等待 Bob 收到
        recv_until(ws_bob, lambda f: f.get("msg_id") == recall_target_id, timeout=3.0)

        # Alice 触发撤回
        ws_alice.send(json.dumps({
            "type": "recall",
            "msg_id": recall_target_id
        }))

        # Bob 应当收到 recall 事件帧
        recall_on_bob = recv_until(ws_bob, lambda f: f.get("type") == "recall" and f.get("msg_id") == recall_target_id, timeout=3.0)
        check(recall_on_bob is not None, "Node 2 上的 Bob 成功同步收到跨节点消息撤回事件")

        # ===== 测试 6: 网关负载均衡转发与 WebSocket 透明通道 =====
        print("\n--- 测试 6: 网关反向代理 WebSocket 接入转发 ---")
        ws_carol, r_carol = ws_login(8001, "ClusterCarol")
        check(r_carol.get("type") == "login_success", "Carol 经由网关 8001 成功完成 WebSocket 握手并登录")

        gateway_msg = "Message sent through Gateway port 8001"
        ws_carol.send(json.dumps({"type": "chat", "text": gateway_msg}))

        # Alice (Node 1) 和 Bob (Node 2) 均应收到
        gw_on_alice = recv_until(ws_alice, lambda f: f.get("type") == "chat" and f.get("text") == gateway_msg, timeout=3.0)
        check(gw_on_alice is not None, "Node 1 用户 Alice 成功收到网关转发的用户 Carol 消息")

        gw_on_bob = recv_until(ws_bob, lambda f: f.get("type") == "chat" and f.get("text") == gateway_msg, timeout=3.0)
        check(gw_on_bob is not None, "Node 2 用户 Bob 成功收到网关转发的用户 Carol 消息")

        # 关闭测试连接
        try: ws_alice.close()
        except: pass
        try: ws_bob.close()
        except: pass
        try: ws_carol.close()
        except: pass

    finally:
        print("\n[Cleanup] 停止测试进程...")
        for name, p in procs:
            try:
                p.terminate()
                p.wait(timeout=2.0)
            except Exception:
                try: p.kill()
                except Exception: pass

    print("\n" + "=" * 60)
    if failures == 0:
        print("  🎉 全部分布式集群集成测试用例通过 (0 failures)！")
        print("=" * 60)
        return 0
    else:
        print(f"  ❌ 测试失败：共有 {failures} 项断言未通过")
        print("=" * 60)
        return 1

if __name__ == "__main__":
    sys.exit(main())
