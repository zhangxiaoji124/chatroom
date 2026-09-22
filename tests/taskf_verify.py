#!/usr/bin/env python3
"""任务F验证：群组 + 表情透传 + 管理后台接口。

验证项：
  1. GET /api/stickers 返回表情列表
  2. GET /api/admin/stats 返回用户/消息统计
  3. GET /api/admin/messages 返回历史消息（含 sticker）
  4. 群组：A/B加入"开发组"，A发 group_chat → B收到；第三客户端C(不加群)收不到
  5. 群成员加入通知广播
  6. 老全局 chat 广播仍工作（回归）
  7. 表情聊天：带 sticker 的 chat 透传广播 + 落库含 sticker
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

# Windows GBK 控制台打印 emoji 会崩，强制 UTF-8 输出
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SERVER = os.path.join(ROOT, "build", "chatroom_server.exe")
BASE = "http://127.0.0.1:8080"
WS = "ws://127.0.0.1:8080/ws"

failures = 0
def check(cond, msg):
    global failures
    print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")
    if not cond:
        failures += 1

def http_json(path, timeout=3):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return json.loads(r.read().decode())

def recv_json(ws, timeout=3):
    import websockets.sync.client as wsc
    # ws.recv returns str
    return json.loads(ws.recv(timeout=timeout))

def main():
    if not os.path.exists(SERVER):
        print("缺少服务器，先 mingw32-make all")
        return 2

    proc = subprocess.Popen([SERVER], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        # 就绪
        ready = False
        for _ in range(60):
            try:
                http_json("/api/status", timeout=0.5)
                ready = True
                break
            except Exception:
                time.sleep(0.2)
        check(ready, "服务器就绪")
        if not ready:
            return 1

        # 1. stickers
        s = http_json("/api/stickers")
        check("stickers" in s and len(s["stickers"]) > 0, f"/api/stickers 返回 {len(s.get('stickers',[]))} 个表情")
        sk = [x["key"] for x in s["stickers"]]
        sample = s["stickers"][0] if s["stickers"] else {}
        check("key" in sample and "emoji" in sample, f"stickers 元素格式正确 {sample}")

        # 2/3. admin
        st = http_json("/api/admin/stats")
        check("users" in st and "message_count" in st and "server_uptime_ms" in st,
              f"/api/admin/stats 字段齐全 (uptime={st.get('server_uptime_ms')}ms, msg={st.get('message_count')})")
        before_count = st.get("message_count", 0)
        msgs = http_json("/api/admin/messages")
        check(isinstance(msgs, list), "/api/admin/messages 返回数组")

        import websockets.sync.client as wsc
        with wsc.connect(WS) as a, wsc.connect(WS) as b, wsc.connect(WS) as c:
            # 登录
            clients = [("群甲", a), ("群乙", b), ("外部丙", c)]
            for nick, ws in clients:
                ws.send(json.dumps({"type": "login", "nickname": nick}, ensure_ascii=False))
            # 排空积压：读到超时为止，把上线广播全部消化，避免后续断言错位
            for nick, ws in clients:
                while True:
                    try:
                        recv_json(ws, timeout=0.5)
                    except Exception:
                        break
            print("  [INFO] 所有上线广播已排空")
            # 甲、乙加群，丙不加（join 后每人收到2条：①确认 ②自己也作为群成员收到加入广播）
            a.send(json.dumps({"type": "join_group", "name": "开发组"}, ensure_ascii=False))
            ma1 = recv_json(a); check(ma1.get("type") == "system" and "加入群" in ma1.get("msg",""), f"甲加入群确认: {ma1.get('msg')}")
            ma2 = recv_json(a); check(ma2.get("type") == "system" and "加入了群" in ma2.get("msg",""), f"甲 join 群广播(含自己): {ma2.get('msg')}")

            b.send(json.dumps({"type": "join_group", "name": "开发组"}, ensure_ascii=False))
            mb1 = recv_json(b); check(mb1.get("type") == "system" and "加入群" in mb1.get("msg",""), f"乙加入群确认: {mb1.get('msg')}")
            mb2 = recv_json(b); check(mb2.get("type") == "system" and "加入了群" in mb2.get("msg",""), f"乙 join 群广播(含自己): {mb2.get('msg')}")
            # 甲收到"乙加入了群"（甲是最早已在群里的成员）
            nm = recv_json(a)
            check(nm.get("type") == "system" and "群乙 加入了群" in nm.get("msg", ""),
                  f"甲收到乙加入群通知: {nm.get('msg')}")

            # 甲在群里发言
            a.send(json.dumps({"type": "group_chat", "group": "开发组", "text": "组内消息"}, ensure_ascii=False))
            mb = recv_json(b)
            check(mb.get("type") == "group_chat" and mb.get("group") == "开发组" and mb.get("from") == "群甲" and mb.get("text") == "组内消息",
                  f"乙收到群消息: {mb}")
            ma = recv_json(a)
            check(ma.get("type") == "group_chat" and ma.get("text") == "组内消息", f"甲收到自己群消息回显")
            # 丙(不加群)应收不到群消息 —— 不阻塞读，用短超时确认无消息
            got = None
            try:
                got = recv_json(c, timeout=0.8)
            except Exception:
                got = None
            check(got is None or got.get("type") not in ("group_chat",),
                  f"外部丙未收到群消息 (got={got})")

            # 未加群者发言应报错
            c.send(json.dumps({"type": "group_chat", "group": "开发组", "text": "偷偷发"}, ensure_ascii=False))
            ec = recv_json(c)
            check(ec.get("type") == "error", f"未加群发言被拒: {ec.get('msg')}")

            # 老全局 chat 仍工作（回归）
            c.send(json.dumps({"type": "chat", "text": "全局消息"}, ensure_ascii=False))
            gc = recv_json(c)
            check(gc.get("type") == "chat" and gc.get("from") == "外部丙" and gc.get("text") == "全局消息",
                  f"全局 chat 仍广播: {gc}")

            # 表情聊天（先排空甲队列里残留的丙"全局消息"，再断言 sticker）
            recv_json(a, timeout=1)  # 排掉丙的全局消息
            a.send(json.dumps({"type": "chat", "text": "哈哈", "sticker": "happy"}, ensure_ascii=False))
            sc = recv_json(a)
            check(sc.get("type") == "chat" and sc.get("sticker") == "happy",
                  f"表情透传广播: {sc}")

        # 4. admin messages 含 sticker + 条数增加
        time.sleep(0.3)
        st2 = http_json("/api/admin/stats")
        check(st2.get("message_count", 0) > before_count, f"消息数增长 {before_count}->{st2.get('message_count')}")
        msgs2 = http_json("/api/admin/messages?limit=50")
        has_sticker = any(m.get("sticker") == "happy" for m in msgs2)
        check(has_sticker, "admin/messages 含 sticker 字段")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print("\n" + ("TASK-F VERIFIED" if failures == 0 else "TASK-F FAILED"))
    return 0 if failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
