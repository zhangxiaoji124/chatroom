#!/usr/bin/env python3
"""最终验收：针对当前运行中的服务器，覆盖 文本/表情/群组/语音/管理后台 全功能。
不自己启动服务器（用协调者已启动的 8080）。"""
import json, sys, time, urllib.request
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import websockets.sync.client as wsc

BASE = "http://127.0.0.1:8080"
fail = 0
def check(c, m):
    global fail
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
    if not c: fail += 1

def drain(ws, t=0.5):
    end = time.time() + t
    while time.time() < end:
        try: ws.recv(timeout=0.15)
        except Exception: return

def recv_type(ws, want, t=2.0):
    end = time.time() + t
    while time.time() < end:
        try: m = json.loads(ws.recv(timeout=0.25))
        except Exception: return None
        if m.get("type") == want: return m
    return None

# 前置：服务器在跑
try:
    urllib.request.urlopen(BASE+"/api/status", timeout=2); ok=True
except Exception: ok=False
check(ok, "服务器在跑")
if not ok: sys.exit(1)

a = wsc.connect("ws://127.0.0.1:8080/ws"); b = wsc.connect("ws://127.0.0.1:8080/ws")
try:
    for ws,n in ((a,"甲"),(b,"乙")):
        ws.send(json.dumps({"type":"login","nickname":n}, ensure_ascii=False)); time.sleep(0.2)
    for ws in (a,b): drain(ws)

    print("== 文本 ==")
    a.send(json.dumps({"type":"chat","text":"hi"}, ensure_ascii=False))
    m=recv_type(b,"chat"); check(m and m.get("text")=="hi","乙收到甲文本")
    for ws in (a,b): drain(ws)

    print("== 表情 ==")
    a.send(json.dumps({"type":"chat","text":"","sticker":"happy"}, ensure_ascii=False))
    m=recv_type(b,"chat"); check(m and m.get("sticker")=="happy","乙收到纯表情")
    drain(b)
    time.sleep(0.1)
    b.send(json.dumps({"type":"chat","text":"赞","sticker":"like"}, ensure_ascii=False))
    m=recv_type(a,"chat"); check(m and m.get("sticker")=="like" and m.get("text")=="赞","甲收到文字+表情")
    for ws in (a,b): drain(ws, 0.3)

    print("== 群组 ==")
    a.send(json.dumps({"type":"join_group","name":"验收群"}, ensure_ascii=False))
    m=recv_type(a,"system"); check(m and "你已加入群" in m.get("msg",""),"甲建群确认")
    drain(a)  # 排掉甲可能收到的自我广播
    b.send(json.dumps({"type":"join_group","name":"验收群"}, ensure_ascii=False))
    recv_type(b,"system"); drain(b)
    m=recv_type(a,"system"); check(m and "乙" in m.get("msg","") and "加入" in m.get("msg",""),"甲收到乙入群通知")
    drain(a)
    a.send(json.dumps({"type":"group_chat","group":"验收群","text":"群消息"}, ensure_ascii=False))
    m=recv_type(b,"group_chat"); check(m and m.get("group")=="验收群" and m.get("text")=="群消息","乙收到甲群消息")
    drain(a,0.3); drain(b)
    # 甲发一个甲不存在的群 → 报错
    a.send(json.dumps({"type":"group_chat","group":"不存在的群","text":"x"}, ensure_ascii=False))
    m=recv_type(a,"error"); check(m and "加入群" in m.get("msg",""),"未入群发群消息被拒")

    print("== 语音(协议级: 上传+广播) ==")
    audio = b"\x1a\x45\xdf\xa3" + b"\x00"*1024
    req = urllib.request.Request(BASE+"/api/upload/voice", data=audio, headers={"Content-Type":"audio/webm"}, method="POST")
    up = json.loads(urllib.request.urlopen(req, timeout=5).read())
    check(up.get("url","").startswith("/data/audio/"),"上传成功: "+up.get("url",""))
    a.send(json.dumps({"type":"voice_send","url":up["url"]}, ensure_ascii=False))
    m=recv_type(b,"voice"); check(m and m.get("from")=="甲" and m.get("url")==up["url"],"乙收到甲语音广播")

    print("== 管理后台 ==")
    time.sleep(0.3)
    s=json.loads(urllib.request.urlopen(BASE+"/api/admin/stats", timeout=5).read())
    check(s.get("status")=="ok","admin/stats 含 status=ok(修复生效)")
    nicks=[u.get("nickname") for u in s.get("users",[])]
    check("甲" in nicks and "乙" in nicks,"在线用户含甲乙: "+str(nicks))
    msgs=json.loads(urllib.request.urlopen(BASE+"/api/admin/messages?limit=200", timeout=5).read())
    types={m.get("type") for m in msgs}
    check("chat" in types and "voice" in types,"消息列表含 chat+voice, 实际: "+str(types))
    stickers=[m.get("sticker") for m in msgs if m.get("sticker")]
    check("happy" in stickers and "like" in stickers,"表情已落库: "+str(stickers[-4:]))
finally:
    for ws in (a,b):
        try: ws.close()
        except Exception: pass
print("\n" + ("FINAL ACCEPT PASSED" if fail==0 else "FINAL ACCEPT FAILED"))
sys.exit(0 if fail==0 else 1)
