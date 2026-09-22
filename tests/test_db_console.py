import subprocess
import time
import urllib.request
import json

def test():
    subprocess.run(["powershell", "-Command", "Stop-Process -Name chatroom_server_v2 -Force -ErrorAction SilentlyContinue"])
    proc = subprocess.Popen(["build/chatroom_server_v2.exe", "19095"])
    time.sleep(1)

    try:
        # Test /api/db/tail (terminal ASCII view)
        with urllib.request.urlopen("http://127.0.0.1:19095/api/db/tail?limit=5") as r:
            text = r.read().decode("utf-8")
            print("=== /api/db/tail Output ===")
            print(text)
            assert "CHATROOM DATABASE MESSAGES" in text

        # Test /api/db/changes (JSON stream)
        with urllib.request.urlopen("http://127.0.0.1:19095/api/db/changes?limit=5") as r:
            data = json.loads(r.read().decode("utf-8"))
            print("=== /api/db/changes Output ===")
            print("Total messages in DB:", data.get("total_messages"))
            print("Returned records count:", data.get("count"))
            assert "records" in data
            if data["records"]:
                print("First sample record:", data["records"][0])

        print("\n[SUCCESS] All DB Console Interfaces Verified!")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except:
            proc.kill()
        subprocess.run(["powershell", "-Command", "Stop-Process -Name chatroom_server_v2 -Force -ErrorAction SilentlyContinue"])

if __name__ == "__main__":
    test()
