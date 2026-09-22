#!/usr/bin/env python3
"""语音功能联调：上传 -> 广播(全局+群) -> 静态访问 -> admin可见。
协议级验证（不含真实麦克风录音，录音由浏览器负责）。"""
import json, os, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SERVER = os.path.join(ROOT, "build", "chatroom_server.exe")
BASE = "http://127.0.0.1:8080"
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

fail = 0
def check(c, m):
    global fail
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
    if not c: fail += 1

def http(path, timeout=3):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return r.read()

def main():
    if not os.path.exists(SERVER): print("缺 exe"); return 2
    proc = subprocess.Popen([SERVER], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        ok = False
        for _ in range(60):
            try: http("/api/status", 0.5); ok = True; break
            except Exception: time.sleep(0.2)
        check(ok, "服务器就绪")
        if not ok: return 1

        # 1. 上传音频
        audio = b"\x1a\x45\xdf\xa3" + os.urandom(2048)  # webm 魔数 + 伪音频数据
        req = urllib.request.Request(BASE + "/api/upload/voice", data=audio,
                                     headers={"Content-Type": "audio/webm"}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as r:
            up = json.loads(r.read().decode())
        check(up.get("url","").startswith("/data/audio/"), f"上传返回 url={up.get('url')}")
        url = up["url"]

        # 2. 回读字节一致
        got = http(url)
        check(got[:4] == b"\x1a\x45\xdf\xa3" and len(got) == len(audio), f"回读字节一致 size={len(got)}")

        # 3. 超限上传 -> 413
        big = os.urandom(6 * 1024 * 1024)
        try:
            urllib.request.urlopen(urllib.request.Request(BASE + "/api/upload/voice", data=big,
                headers={"Content-Type": "audio/webm"}, method="POST"), timeout=8)
            check(False, "大文件应413")
        except urllib.error.HTTPError as e:
            check(e.code == 413, f"大文件 413 (got {e.code})")

        # 4. WS 广播：A上传后广播给A/B，群内语音组内隔离
        import websockets.sync.client as wsc
        with wsc.connect("ws://127.0.0.1:8080/ws") as a, wsc.connect("ws://127.0.0.1:8080/ws") as b, wsc.connect("ws://127.0.0.1:8080/ws") as c:
            for ws, n in ((a,"音甲"),(b,"音乙"),(c,"音丙")):
                ws.send(json.dumps({"type":"login","nickname":n}, ensure_ascii=False))
            for ws in (a,b,c):
                while True:
                    try: ws.recv(timeout=0.4)
                    except Exception: break  # 排空上线广播

            # 全局语音
            a.send(json.dumps({"type":"voice_send","url":url}))
            ma = json.loads(a.recv(timeout=2))
            mb = json.loads(b.recv(timeout=2))
            check(ma.get("type")=="voice" and ma.get("url")==url and ma.get("from")=="音甲", f"甲收到语音广播 {ma.get('type')}")
            check(mb.get("type")=="voice" and mb.get("url")==url, "乙收到全局语音")

            # 群内语音隔离：甲乙入群，丙不入
            # 先排空丙队列里积压的“全局语音”广播（此前甲发的全局语音广播给了包括丙在内的所有人）
            while True:
                try: c.recv(timeout=0.4)
                except Exception: break
            a.send(json.dumps({"type":"join_group","name":"语音组"}, ensure_ascii=False)); json.loads(a.recv(timeout=2)); json.loads(a.recv(timeout=2))
            b.send(json.dumps({"type":"join_group","name":"语音组"}, ensure_ascii=False)); json.loads(b.recv(timeout=2)); json.loads(b.recv(timeout=2))
            json.loads(a.recv(timeout=2))  # a收 乙入群
            a.send(json.dumps({"type":"voice_send","url":url,"group":"语音组"}, ensure_ascii=False))
            gb = json.loads(b.recv(timeout=2))
            check(gb.get("type")=="voice" and gb.get("group")=="语音组", f"乙收到群语音 {gb.get('group')}")
            try:
                c.recv(timeout=0.6); check(False, "丙不应收到群语音")
            except Exception:
                check(True, "丙未收到群语音(隔离正确)")

        # 5. admin/messages 含 voice 记录
        time.sleep(0.3)
        msgs = http("/api/admin/messages?limit=50")
        arr = json.loads(msgs)
        has_voice = any(m.get("type")=="voice" for m in arr)
        check(has_voice, "admin/messages 含 voice 记录")
    finally:
        proc.terminate()
        try: proc.wait(timeout=5)
        except Exception: proc.kill()
    print("\n" + ("VOICE VERIFIED" if fail==0 else "VOICE FAILED"))
    return 0 if fail==0 else 1

if __name__ == "__main__":
    sys.exit(main())
