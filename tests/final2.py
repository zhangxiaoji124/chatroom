#!/usr/bin/env python3
"""无竞态最终验收：发一条 -> 等待 -> 断言 -> 排空(短)。避免 drain 误伤。"""
import json, sys, time, urllib.request
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import websockets.sync.client as wsc
BASE="http://127.0.0.1:8080"
fail=0
def check(c,m):
    global fail
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
    if not c: fail+=1

def get(ws, t=1.5):
    """读一条, 超时返回 None"""
    try: return json.loads(ws.recv(timeout=t))
    except Exception: return None

a=wsc.connect("ws://127.0.0.1:8080/ws"); b=wsc.connect("ws://127.0.0.1:8080/ws")
try:
    for ws,n in ((a,"王五"),(b,"赵六")):
        ws.send(json.dumps({"type":"login","nickname":n}, ensure_ascii=False))
        time.sleep(0.3)
    try:
        urllib.request.urlopen(BASE+"/api/status", timeout=2)
    except Exception: pass

    print("== 表情: 甲->乙 纯表情 ==")
    a.send(json.dumps({"type":"chat","text":"","sticker":"wow"}, ensure_ascii=False))
    m=get(b); check(m and m.get("sticker")=="wow","乙收纯表情wow")

    print("== 表情: 乙->甲 文字+表情 ==")
    b.send(json.dumps({"type":"chat","text":"赞","sticker":"like"}, ensure_ascii=False))
    m=get(a); check(m and m.get("text")=="赞" and m.get("sticker")=="like", f"甲收文字+表情: {m}")

    print("== 群组双向 ==")
    # 甲乙都入 群Y
    a.send(json.dumps({"type":"join_group","name":"群Y"},ensure_ascii=False)); get(a)
    b.send(json.dumps({"type":"join_group","name":"群Y"},ensure_ascii=False)); get(b)
    time.sleep(0.3)
    # 清队列(短排)
    for ws in (a,b):
        while True:
            if get(ws,0.2) is None: break
    a.send(json.dumps({"type":"group_chat","group":"群Y","text":"甲到"},ensure_ascii=False))
    m=get(b); check(m and m.get("text")=="甲到" and m.get("group")=="群Y","乙收甲群消息: "+str(m))
    b.send(json.dumps({"type":"group_chat","group":"群Y","text":"乙到"},ensure_ascii=False))
    m=get(a); check(m and m.get("from")=="赵六" and m.get("text")=="乙到","甲收乙群消息: "+str(m))

    print("== 加群成员隔离(丙不在) ==")
    c=wsc.connect("ws://127.0.0.1:8080/ws")
    c.send(json.dumps({"type":"login","nickname":"钱七"},ensure_ascii=False)); time.sleep(0.3)
    while get(c,0.2) is not None: pass
    a.send(json.dumps({"type":"group_chat","group":"群Y","text":"仅甲队"},ensure_ascii=False))
    m=get(b); check(m and m.get("text")=="仅甲队","乙收")
    m=get(c); check(m is None,"丙(未入群)收不到")
    c.close()
finally:
    for ws in (a,b):
        try: ws.close()
        except Exception: pass
print("\n" + ("FINAL2 PASSED" if fail==0 else "FINAL2 FAILED"))
sys.exit(0 if fail==0 else 1)
