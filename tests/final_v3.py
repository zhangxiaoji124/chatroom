#!/usr/bin/env python3
"""最终验收 v3：彻底排空 + 严格等待 + 每步独立断言。解决全部竞态。"""
import json, sys, time, urllib.request
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import websockets.sync.client as wsc
BASE="http://127.0.0.1:8080"
fail=0
def check(c,m):
    global fail
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
    if not c: fail+=1

def drain_all(ws_list, t=0.8):
    end=time.time()+t
    while time.time()<end:
        got=False
        for ws in ws_list:
            try:
                if ws.recv(timeout=0.05) is not None: got=True
            except Exception: pass
        if not got: break

def recv_type(ws, want, t=2.5):
    end=time.time()+t
    while time.time()<end:
        try: m=json.loads(ws.recv(timeout=0.25))
        except Exception: return None
        if m.get("type")==want: return m
    return None

try:
    urllib.request.urlopen(BASE+"/api/status", timeout=2)
except Exception:
    print("服务器未运行"); sys.exit(1)

a=wsc.connect("ws://127.0.0.1:8080/ws"); b=wsc.connect("ws://127.0.0.1:8080/ws")
try:
    for ws,n in ((a,"甲"),(b,"乙")):
        ws.send(json.dumps({"type":"login","nickname":n}, ensure_ascii=False))
        time.sleep(0.3)
    drain_all([a,b], 1.0)   # 排掉甲乙上线的所有广播

    print("== 1 文本 ==")
    a.send(json.dumps({"type":"chat","text":"第一条"}, ensure_ascii=False))
    m=recv_type(b,"chat"); check(m and m.get("text")=="第一条","乙收文本: "+str(m))
    drain_all([a,b],0.5)

    print("== 2 表情(纯+文字) ==")
    a.send(json.dumps({"type":"chat","text":"","sticker":"happy"}, ensure_ascii=False))
    m=recv_type(b,"chat"); check(m and m.get("sticker")=="happy","乙收纯表情happy")
    drain_all([a,b],0.6)
    b.send(json.dumps({"type":"chat","text":"很好","sticker":"like"}, ensure_ascii=False))
    m=recv_type(a,"chat"); check(m and m.get("sticker")=="like" and m.get("text")=="很好","甲收文字+表情: "+str(m))
    drain_all([a,b],0.6)

    print("== 3 群组(双向+隔离) ==")
    a.send(json.dumps({"type":"join_group","name":"项目组"}, ensure_ascii=False))
    recv_type(a,"system"); drain_all([a],0.4)
    b.send(json.dumps({"type":"join_group","name":"项目组"}, ensure_ascii=False))
    recv_type(b,"system"); drain_all([a,b],0.6)
    a.send(json.dumps({"type":"group_chat","group":"项目组","text":"甲发言"}, ensure_ascii=False))
    m=recv_type(b,"group_chat"); check(m and m.get("group")=="项目组" and m.get("text")=="甲发言","乙收甲群消息")
    drain_all([a,b],0.6)
    b.send(json.dumps({"type":"group_chat","group":"项目组","text":"乙回应"}, ensure_ascii=False))
    m=recv_type(a,"group_chat"); check(m and m.get("from")=="乙" and m.get("text")=="乙回应","甲收乙群消息")
    drain_all([a,b],0.6)
    # 未入群隔离：再开一个丙
    c=wsc.connect("ws://127.0.0.1:8080/ws"); c.send(json.dumps({"type":"login","nickname":"丙"},ensure_ascii=False)); time.sleep(0.3)
    drain_all([a,b,c],0.8)
    a.send(json.dumps({"type":"group_chat","group":"项目组","text":"仅成员"}, ensure_ascii=False))
    m=recv_type(b,"group_chat"); check(m,"乙收'仅成员'")
    m=recv_type(c,"group_chat",t=0.6); check(m is None,"丙(未入群)收不到")
    c.close()

    print("== 4 语音(上传+广播) ==")
    audio=b"\x1a\x45\xdf\xa3"+b"\x00"*1500
    req=urllib.request.Request(BASE+"/api/upload/voice",data=audio,headers={"Content-Type":"audio/webm"},method="POST")
    up=json.loads(urllib.request.urlopen(req,timeout=5).read())
    check(up.get("url","").startswith("/data/audio/"),"上传成功: "+up.get("url",""))
    a.send(json.dumps({"type":"voice_send","url":up["url"]},ensure_ascii=False))
    m=recv_type(b,"voice"); check(m and m.get("from")=="甲" and m.get("url")==up["url"],"乙收语音广播")

    print("== 5 管理后台 ==")
    time.sleep(0.4)
    s=json.loads(urllib.request.urlopen(BASE+"/api/admin/stats",timeout=5).read())
    check(s.get("status")=="ok","stats含status=ok")
    nicks=[u.get("nickname") for u in s.get("users",[])]
    check("甲" in nicks and "乙" in nicks,"在线含甲乙: "+str(nicks))
    msgs=json.loads(urllib.request.urlopen(BASE+"/api/admin/messages?limit=200",timeout=5).read())
    types={m.get("type") for m in msgs}
    check("chat" in types and "voice" in types,"含chat+voice: "+str(types))
    stickers=[m.get("sticker") for m in msgs if m.get("sticker")]
    check("happy" in stickers and "like" in stickers,"表情落库: "+str(stickers[-4:]))
finally:
    for ws in (a,b):
        try: ws.close()
        except Exception: pass
print("\n" + ("FINAL v3 ALL PASSED" if fail==0 else f"FINAL v3 FAILED ({fail})"))
sys.exit(0 if fail==0 else 1)
