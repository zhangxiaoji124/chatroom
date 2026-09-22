@echo off
setlocal
cd /d "%~dp0.."
python scripts\create_shortcut.py
pause
