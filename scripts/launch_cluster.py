#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分布式集群一键启动与进程编排管理器 (Cluster Orchestrator)
功能：
1. 启动 3 个 C++ 聊天室节点（8080, 8081, 8082）并互相配置集群 Peers 网格
2. 启动分布式统一入口负载均衡网关 (8000)
3. 实时输出各节点运行状态
4. 支持 Ctrl+C 一键优雅停止全部子进程
"""

import sys
import os
import subprocess
import time
import signal

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    server_bin = os.path.join(base_dir, "build", "chatroom_server_v2.exe")
    gateway_script = os.path.join(base_dir, "scripts", "gateway.py")

    if not os.path.exists(server_bin):
        print(f"[Error] 找不到编译产物: {server_bin}")
        print("请先执行 mingw32-make 编译服务端。")
        sys.exit(1)

    nodes = [
        {"id": "node_8080", "port": 8080, "peers": "127.0.0.1:8081,127.0.0.1:8082"},
        {"id": "node_8081", "port": 8081, "peers": "127.0.0.1:8080,127.0.0.1:8082"},
        {"id": "node_8082", "port": 8082, "peers": "127.0.0.1:8080,127.0.0.1:8081"},
    ]

    processes = []

    print("=" * 60)
    print("  启动分布式聊天室集群 (Distributed Chatroom Cluster)")
    print("=" * 60)

    try:
        # 1. 依次启动各节点
        for n in nodes:
            cmd = [
                server_bin,
                str(n["port"]),
                "--node-id", n["id"],
                "--peers", n["peers"]
            ]
            print(f"[Cluster] 启动节点 {n['id']} (端口 {n['port']})...")
            p = subprocess.Popen(cmd, cwd=base_dir)
            processes.append((n["id"], p))
            time.sleep(0.4)

        # 2. 启动网关
        print(f"[Gateway] 启动网关负载均衡器 (端口 8000)...")
        gw_cmd = [
            sys.executable,
            gateway_script,
            "--port", "8000",
            "--backends", "127.0.0.1:8080,127.0.0.1:8081,127.0.0.1:8082"
        ]
        gw_proc = subprocess.Popen(gw_cmd, cwd=base_dir)
        processes.append(("Gateway_8000", gw_proc))

        print("\n" + "=" * 60)
        print("  所有节点与网关已启动完毕！")
        print("  - 统一入口/网关: http://127.0.0.1:8000")
        print("  - 节点 1 (8080): http://127.0.0.1:8080")
        print("  - 节点 2 (8081): http://127.0.0.1:8081")
        print("  - 节点 3 (8082): http://127.0.0.1:8082")
        print("  - 管理后台    : http://127.0.0.1:8000/admin.html")
        print("  按 Ctrl+C 停止集群全部服务")
        print("=" * 60 + "\n")

        # 守护等待
        while True:
            time.sleep(1)
            for name, p in processes:
                if p.poll() is not None:
                    print(f"[Warning] 进程 {name} 异常退出 (code: {p.returncode})")

    except KeyboardInterrupt:
        print("\n[Cluster] 正在关闭分布式集群所有进程...")
    finally:
        for name, p in processes:
            try:
                p.terminate()
                p.wait(timeout=2)
            except Exception:
                try:
                    p.kill()
                except Exception:
                    pass
        print("[Cluster] 集群已安全停止。")

if __name__ == "__main__":
    main()
