$desktop = [Environment]::GetFolderPath("Desktop")
$shortcutPath = Join-Path $desktop "Chatroom 极客聊天室.lnk"
$targetPath = "D:\chatroom\build\ChatroomApp.exe"
$iconPath = "D:\chatroom\web\favicon.ico"
$workDir = "D:\chatroom"

$ws = New-Object -ComObject WScript.Shell
$shortcut = $ws.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $targetPath
$shortcut.WorkingDirectory = $workDir
$shortcut.IconLocation = "$iconPath,0"
$shortcut.Description = "Chatroom 极客实时聊天室桌面客户端"
$shortcut.Save()

if (Test-Path $shortcutPath) {
    Write-Host "[成功] 桌面快捷方式已创建: $shortcutPath"
} else {
    Write-Host "[失败] 快捷方式未生成"
}
