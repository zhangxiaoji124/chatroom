#!/usr/bin/env python3
"""全功能联调：多用户并发测试 文本/表情/群组/全局广播 + 管理后台数据准确性。
模拟 3 个真实用户同时在线相互发消息。"""
import json, os, subprocess, sys, time, urllib.request, threading

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASE = "http://127.0.0.1:8080"
fail = 0

def check(c, m):
    global fail
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
    if not c: fail += 1

def http(path, timeout=4):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return r.read()

# 检查服务器是否已在跑（协调者启动的 PID 28488）
running = False
try:
    http("/api/status", 1); running = True
except Exception:
    pass

proc = None
if not running:
    SERVER = os.path.join(ROOT, "build", "chatroom_server.exe")
    proc = subprocess.Popen([SERVER], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    ok = False
    for _ in range(60):
        try: http("/api/status", 0.5); ok = True; break
        except Exception: time.sleep(0.2)
    check(ok, "自起服务器就绪")

import websockets.sync.client as wsc

def recv_until(ws, want_type, timeout=3, drain=False):
    """读到指定 type 的消息；drain=True 时先跳过非目标类型或超时清空"""
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            raw = ws.recv(timeout=0.3)
        except Exception:
            if last is not None and last.get("type")==want_type: return last
            continue
        m = json.loads(raw)
        if m.get("type") == want_type:
            if drain:  # 还想再排一个同类型
                return m
            return m
    return None

def main():
    N = 3
    conns = []
    names = ["用户A", "用户B", "用户C"]
    try:
        for i in range(N):
            c = wsc.connect("ws://127.0.0.1:8080/ws")
            c.send(json.dumps({"type":"login","nickname":names[i]}, ensure_ascii=False))
            conns.append(c)
        # 排空各连接的上线广播
        for c in conns:
            while True:
                try:
                    raw = c.recv(timeout=0.25); m=json.loads(raw)
                    if m.get("type")=="system" and "上线" in m.get("msg",""): 
                        pass
                except Exception: break

        print("== 1) 全局文本聊天 ==")
        conns[0].send(json.dumps({"type":"chat","text":"大家好"}, ensure_ascii=False))
        got = [json.loads(c.recv(timeout=2)) for c in conns]
        check(all(g.get("type")=="chat" and g.get("from")=="用户A" and g.get("text")=="大家好" for g in got), "三人都收到A的全局消息")

        print("== 2) 表情包 ==")
        # A 发纯表情
        conns[0].send(json.dumps({"type":"chat","text":"","sticker":"happy"}, ensure_ascii=False))
        g = json.loads(conns[1].recv(timeout=2))
        check(g.get("sticker")=="happy" and g.get("type")=="chat", "B收到带sticker的消息(纯表情)")
        # B 发文字+表情
        conns[1].send(json.dumps({"type":"chat","text":"加油","sticker":"like"}, ensure_ascii=False))
        g = json.loads(conns[2].recv(timeout=2))
        check(g.get("sticker")=="like" and g.get("text")=="加油", "C收到文字+表情消息")
        # 非法 sticker key 应被后端拒绝或忽略(不广播)
        conns[2].send(json.dumps({"type":"chat","text":"x","sticker":"hack"}, ensure_ascii=False))
        rejected = True
        try:
            m = json.loads(conns[2].recv(timeout=1.2))
            if m.get("type")=="error":
                rejected = True
            elif m.get("type")=="chat":
                rejected = False
        except Exception:
            rejected = True
        check(rejected, "非法sticker被拒绝/未广播")

        print("== 3) 群组：建群 + 群聊隔离 ==")
        conns[0].send(json.dumps({"type":"join_group","name":"测试群"}, ensure_ascii=False))
        recv_until(conns[0], "system", drain=True)  # 确认消息
        time.sleep(0.3)
        for c in (conns[1], conns[2]):
            c.send(json.dumps({"type":"join_group","name":"测试群"}, ensure_ascii=False))
            recv_until(c, "system", drain=True)
        time.sleep(0.4)
        # A 群内发消息
        conns[0].send(json.dumps({"type":"group_chat","group":"测试群","text":"群内第一句话"}, ensure_ascii=False))
        got_b = json.loads(conns[1].recv(timeout=2))
        got_c = json.loads(conns[2].recv(timeout=2))
        check(got_b.get("type")=="group_chat" and got_b.get("group")=="测试群" and got_b.get("text")=="群内第一句话", "B收到群消息")
        check(got_c.get("type")=="group_chat" and got_c.get("text")=="群内第一句话", "C收到群消息")
        # 再建一个只有 A 在的群，验证隔离：A发"私群"，B/C收不到
        conns[0].send(json.dumps({"type":"join_group","name":"A私群"}, ensure_ascii=False))
        recv_until(conns[0], "system", drain=True)
        conns[0].send(json.dumps({"type":"group_chat","group":"A私群","text":"只有A的群"}, ensure_ascii=False))
        json.loads(conns[0].recv(timeout=2))  # A自己收到
        b_priv = None; c_priv = None
        try:
            b_priv = json.loads(conns[1].recv(timeout=0.8))
        except Exception: pass
        try:
            c_priv = json.loads(conns[2].recv(timeout=0.5))
        except Exception: pass
        check(b_priv is None and c_priv is None, "B/C收不到A私群消息(群隔离正确)")

        print("== 4) 管理后台数据准确性 ==")
        time.sleep(0.4)
        stats = json.loads(http("/api/admin/stats"))
        check(stats.get("message_count",0) >= 37, f"消息总数>=37 (实际 {stats.get('message_count')})")
        users = stats.get("users", [])
        nicknames = [u.get("nickname") for u in users]
        check(all(n in nicknames for n in names), f"在线用户含A/B/C (实际 {nicknames})")
        check(users and all(u.get("online")==True for u in users), "用户均为在线")

        msgs = json.loads(http("/api/admin/messages?limit=200"))
        types = {m.get("type") for m in msgs}
        stickers = [m.get("sticker") for m in msgs if m.get("sticker")]
        check("chat" in types, "消息列表含chat")
        check("group_chat" in types, f"消息列表含group_chat (实际类型: {types})")
        check("happy" in stickers and "like" in stickers, f"表情已落库 (实际: {stickers[-4:]})")
        # 最后一条应为 A 的私群消息
        check(msgs and msgs[-1].get("type")=="group_chat" and msgs[-1].get("text")=="只有A的群", "最新消息=A私群消息")
    except Exception as e:
        import traceback; traceback.print_exc(); check(False, f"异常: {e}")
    finally:
        for c in conns:
            try: c.close()
            except Exception: pass
        if proc:
            proc.terminate()
            try: proc.wait(timeout=5)
            except Exception: proc.kill()
    print("\n" + ("FULL-SMOKE PASSED" if fail==0 else "FULL-SMOKE FAILED"))
    return 0 if fail==0 else 1

if __name__ == "__main__":
    sys.exit(main())
