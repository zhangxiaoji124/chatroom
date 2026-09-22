#!/usr/bin/env python3
import os
import subprocess
import sys

def main():
    desktop = os.path.join(os.path.expanduser("~"), "Desktop")
    shortcut_path = os.path.join(desktop, "Chatroom 管理后台.lnk")
    target_exe = r"D:\chatroom\build\ChatroomAdminApp.exe"
    icon_path = r"D:\chatroom\web\admin-favicon.ico"
    work_dir = r"D:\chatroom"

    vbs_code = f"""
Set ws = WScript.CreateObject("WScript.Shell")
Set s = ws.CreateShortcut("{shortcut_path}")
s.TargetPath = "{target_exe}"
s.WorkingDirectory = "{work_dir}"
s.IconLocation = "{icon_path},0"
s.Description = "Chatroom 极客实时聊天室管理与控制台"
s.Save
"""

    temp_vbs = os.path.join(os.environ.get("TEMP", "."), "make_admin_shortcut.vbs")
    with open(temp_vbs, "w", encoding="gbk", errors="replace") as f:
        f.write(vbs_code)

    res = subprocess.run(["cscript", "//Nologo", temp_vbs], capture_output=True)
    if os.path.exists(shortcut_path):
        print(f"[成功] 桌面管理后台快捷方式已创建: {shortcut_path}")
        return 0
    else:
        print("[错误] 快捷方式未生成")
        return 1

if __name__ == "__main__":
    sys.exit(main())
