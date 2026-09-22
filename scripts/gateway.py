#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分布式集群网关与反向代理负载均衡器 (Gateway & Load Balancer)
功能：
1. 统一接入前端客户端 (HTTP + WebSocket 统一入口，默认端口 8000)
2. 对后端 C++ 聊天室节点进行定时健康检查与心跳监测 (/api/cluster/ping)
3. 动态维护健康节点池，支持平滑轮询 (Round-Robin) 与故障转移 (Failover)
4. 支持 WebSocket 握手协议双向透明双工通道转发 (Socket Pipe)
5. 零第三方外部依赖，纯 Python 3 标准库实现
"""

import sys
import os
import time
import socket
import select
import threading
import argparse
import urllib.request
import json
from http.server import HTTPServer, BaseHTTPRequestHandler

class BackendNode:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.base_url = f"http://{host}:{port}"
        self.alive = False
        self.online_users = 0
        self.latency_ms = 0
        self.last_check = 0
        self.active_conns = 0

class GatewayState:
    def __init__(self, backends):
        self.nodes = [BackendNode(h, p) for h, p in backends]
        self.lock = threading.Lock()
        self.rr_index = 0
        self.running = True

    def get_healthy_node(self) -> BackendNode:
        with self.lock:
            healthy = [n for n in self.nodes if n.alive]
            if not healthy:
                # 降级：如果全部标记不健康，仍尝试轮询第一个以避免彻底黑洞
                return self.nodes[self.rr_index % len(self.nodes)]
            node = healthy[self.rr_index % len(healthy)]
            self.rr_index = (self.rr_index + 1) % len(healthy)
            return node

    def health_check_loop(self):
        while self.running:
            for node in self.nodes:
                t0 = time.time()
                alive = False
                online = 0
                try:
                    req = urllib.request.Request(f"{node.base_url}/api/cluster/ping")
                    with urllib.request.urlopen(req, timeout=1.5) as resp:
                        if resp.status == 200:
                            data = json.loads(resp.read().decode('utf-8'))
                            online = data.get("online_users", 0)
                            alive = True
                except Exception:
                    alive = False
                latency = int((time.time() - t0) * 1000) if alive else 9999
                with self.lock:
                    node.alive = alive
                    node.online_users = online
                    node.latency_ms = latency
                    node.last_check = time.time()
            time.sleep(2)

def pipe_sockets(s1, s2):
    """在两个原始 socket 之间双向并发搬运数据流（用于 WebSocket 透明传输）"""
    sockets = [s1, s2]
    try:
        while True:
            r, _, _ = select.select(sockets, [], sockets, 60.0)
            if not r:
                break
            for s in r:
                other = s2 if s is s1 else s1
                data = s.recv(65536)
                if not data:
                    return
                other.sendall(data)
    except Exception:
        pass
    finally:
        try: s1.close()
        except: pass
        try: s2.close()
        except: pass

class GatewayHTTPHandler(BaseHTTPRequestHandler):
    gateway_state = None

    def log_message(self, format, *args):
        # 简化日志输出
        pass

    def do_GET(self):
        self._proxy_request()

    def do_POST(self):
        self._proxy_request()

    def do_PUT(self):
        self._proxy_request()

    def do_DELETE(self):
        self._proxy_request()

    def _proxy_request(self):
        # 判断是否为网关自身的监控接口
        if self.path == "/api/gateway/status":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            with self.gateway_state.lock:
                node_list = [{
                    "host": n.host,
                    "port": n.port,
                    "base_url": n.base_url,
                    "alive": n.alive,
                    "online_users": n.online_users,
                    "latency_ms": n.latency_ms
                } for n in self.gateway_state.nodes]
            resp = json.dumps({"status": "ok", "gateway_port": self.server.server_port, "nodes": node_list})
            self.wfile.write(resp.encode('utf-8'))
            return

        # 判断是否为 WebSocket 升级请求
        upgrade = self.headers.get("Upgrade", "").lower()
        if "websocket" in upgrade:
            self._proxy_websocket()
            return

        # 普通 HTTP 请求代理
        target = self.gateway_state.get_healthy_node()
        url = f"{target.base_url}{self.path}"
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else None

        req = urllib.request.Request(url, data=body, method=self.command)
        for k, v in self.headers.items():
            if k.lower() not in ('host', 'content-length', 'connection'):
                req.add_header(k, v)
        req.add_header('Host', f"{target.host}:{target.port}")
        req.add_header('X-Forwarded-For', self.client_address[0])

        try:
            with urllib.request.urlopen(req, timeout=10.0) as upstream:
                self.send_response(upstream.status)
                for k, v in upstream.getheaders():
                    if k.lower() not in ('transfer-encoding', 'connection'):
                        self.send_header(k, v)
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(upstream.read())
        except urllib.error.HTTPError as e:
            self.send_response(e.code)
            for k, v in e.headers.items():
                if k.lower() not in ('transfer-encoding', 'connection'):
                    self.send_header(k, v)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(e.read())
        except Exception as e:
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": "Bad Gateway", "detail": str(e)}).encode('utf-8'))

    def _proxy_websocket(self):
        target = self.gateway_state.get_healthy_node()
        # 建立到后端节点的原生 TCP 连接
        try:
            upstream_sock = socket.create_connection((target.host, target.port), timeout=5.0)
        except Exception as e:
            self.send_response(502)
            self.end_headers()
            self.wfile.write(b"Failed to connect to upstream node")
            return

        # 组装客户端发来的初始握手 HTTP 请求
        headers_lines = [f"{self.command} {self.path} HTTP/1.1"]
        for k, v in self.headers.items():
            if k.lower() == 'host':
                headers_lines.append(f"Host: {target.host}:{target.port}")
            else:
                headers_lines.append(f"{k}: {v}")
        headers_lines.append(f"X-Forwarded-For: {self.client_address[0]}")
        headers_lines.append("\r\n")
        raw_headers = "\r\n".join(headers_lines).encode('utf-8')

        try:
            upstream_sock.sendall(raw_headers)
        except Exception:
            upstream_sock.close()
            self.send_response(502)
            self.end_headers()
            return

        # 接管 client socket，进入双向原始管道转发
        client_sock = self.connection
        pipe_sockets(client_sock, upstream_sock)

def run_gateway(port=8000, backends=None):
    if not backends:
        backends = [("127.0.0.1", 8080), ("127.0.0.1", 8081), ("127.0.0.1", 8082)]

    state = GatewayState(backends)
    GatewayHTTPHandler.gateway_state = state

    # 启动健康检查线程
    hc_thread = threading.Thread(target=state.health_check_loop, daemon=True)
    hc_thread.start()

    print("=" * 50)
    print(f"  分布式网关负载均衡器 (Gateway) 启动成功")
    print(f"  监听入口 : http://127.0.0.1:{port} (含 WebSocket 统一接入)")
    print(f"  后端节点 : {', '.join(f'{h}:{p}' for h, p in backends)}")
    print(f"  网关状态 : http://127.0.0.1:{port}/api/gateway/status")
    print("=" * 50)

    server = HTTPServer(("0.0.0.0", port), GatewayHTTPHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[Gateway] 网关正在停止...")
    finally:
        state.running = False
        server.server_close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Distributed Chatroom Gateway")
    parser.add_argument("--port", type=int, default=8000, help="Gateway listen port (default 8000)")
    parser.add_argument("--backends", type=str, default="127.0.0.1:8080,127.0.0.1:8081,127.0.0.1:8082",
                        help="Comma-separated upstream backends (e.g. 127.0.0.1:8080,127.0.0.1:8081)")
    args = parser.parse_args()

    backends_list = []
    for item in args.backends.split(","):
        item = item.strip()
        if not item: continue
        parts = item.split(":")
        host = parts[0] if parts[0] else "127.0.0.1"
        p = int(parts[1]) if len(parts) > 1 else 8080
        backends_list.append((host, p))

    run_gateway(port=args.port, backends=backends_list)
