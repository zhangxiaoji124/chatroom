#!/usr/bin/env python3
"""群组+表情专项严谨测试：每步彻底排空队列再断言。"""
import json, sys, time
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import websockets.sync.client as wsc

fail = 0
def check(c, m):
    global fail
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
    if not c: fail += 1

def drain(ws, t=0.4):
    """彻底排空当前队列"""
    end = time.time() + t
    out = []
    while time.time() < end:
        try:
            out.append(json.loads(ws.recv(timeout=0.15)))
        except Exception:
            return out
    return out

def recv_type(ws, want, t=2.0):
    end = time.time() + t
    while time.time() < end:
        try:
            m = json.loads(ws.recv(timeout=0.25))
        except Exception:
            return None
        if m.get("type") == want:
            return m
    return None

a = wsc.connect("ws://127.0.0.1:8080/ws")
b = wsc.connect("ws://127.0.0.1:8080/ws")
c = wsc.connect("ws://127.0.0.1:8080/ws")
try:
    for ws, n in ((a,"甲"),(b,"乙"),(c,"丙")):
        ws.send(json.dumps({"type":"login","nickname":n}, ensure_ascii=False))
        time.sleep(0.2)
    for ws in (a,b,c): drain(ws)

    print("== 表情：纯表情 + 文字表情 ==")
    a.send(json.dumps({"type":"chat","text":"","sticker":"happy"}, ensure_ascii=False))
    m = recv_type(b, "chat")
    check(m and m.get("sticker")=="happy" and m.get("text")=="", "乙收到纯表情")
    b.send(json.dumps({"type":"chat","text":"加油","sticker":"like"}, ensure_ascii=False))
    m = recv_type(c, "chat")
    check(m and m.get("sticker")=="like" and m.get("text")=="加油", "丙收到文字+表情")
    for ws in (a,b,c): drain(ws)

    print("== 群组 1：甲建群，乙加入 ==")
    a.send(json.dumps({"type":"join_group","name":"研发群"}, ensure_ascii=False))
    m = recv_type(a, "system")
    check(m and "你已加入群" in m.get("msg",""), f"甲收到建群确认: {m.get('msg') if m else None}")
    time.sleep(0.2); 
    b.send(json.dumps({"type":"join_group","name":"研发群"}, ensure_ascii=False))
    m = recv_type(b, "system")
    check(m and "你已加入群" in m.get("msg",""), f"乙收到加入确认: {m.get('msg') if m else None}")
    # 甲应收到「乙 加入了群」系统通知
    m = recv_type(a, "system")
    check(m and "乙" in m.get("msg","") and "加入了群" in m.get("msg",""), f"甲收到乙入群通知: {m.get('msg') if m else None}")
    for ws in (a,b): drain(ws)

    print("== 群组 2：群内聊天，群成员甲乙互收，丙(未入群)收不到 ==")
    a.send(json.dumps({"type":"group_chat","group":"研发群","text":"群内第一条"}, ensure_ascii=False))
    m = recv_type(b, "group_chat")
    check(m and m.get("group")=="研发群" and m.get("from")=="甲" and m.get("text")=="群内第一条", f"乙收到甲群消息: {m.get('text') if m else None}")
    # 丙不在研发群 → 丙收不到 group_chat（丙只收过登录/可能的全局）
    m = recv_type(c, "group_chat", t=0.7)
    check(m is None, "丙(未入群)收不到群消息")
    # 乙回群消息
    b.send(json.dumps({"type":"group_chat","group":"研发群","text":"乙回复"}, ensure_ascii=False))
    m = recv_type(a, "group_chat")
    check(m and m.get("from")=="乙" and m.get("text")=="乙回复", "甲收到乙群消息")
    for ws in (a,b,c): drain(ws)

    print("== 群组 3：丙加入后能收群消息 ==")
    c.send(json.dumps({"type":"join_group","name":"研发群"}, ensure_ascii=False))
    recv_type(c, "system")  # 丙确认
    time.sleep(0.2)
    for ws in (a,b): drain(ws)  # 排掉甲乙的入群通知
    a.send(json.dumps({"type":"group_chat","group":"研发群","text":"三人群"}, ensure_ascii=False))
    mb = recv_type(b, "group_chat"); mc = recv_type(c, "group_chat")
    check(mb and mb.get("text")=="三人群", "乙收到三人群消息")
    check(mc and mc.get("text")=="三人群" and mc.get("from")=="甲", "丙加入后收到群消息")

    print("== 群组 4：未加入就发群消息 → 报错 ==")
    # 丙再建一个群但甲不在 → 甲发该群应报错
    c.send(json.dumps({"type":"join_group","name":"丙专属群"}, ensure_ascii=False))
    recv_type(c, "system"); time.sleep(0.2); drain(c)
    a.send(json.dumps({"type":"group_chat","group":"丙专属群","text":"偷发"}, ensure_ascii=False))
    m = recv_type(a, "error")
    check(m and "加入群" in m.get("msg",""), f"未入群发群消息被拒: {m.get('msg') if m else None}")
finally:
    for ws in (a,b,c):
        try: ws.close()
        except Exception: pass
print("\n" + ("GROUP+STICKER PASSED" if fail==0 else "GROUP+STICKER FAILED"))
sys.exit(0 if fail==0 else 1)
