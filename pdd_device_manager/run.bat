@echo off
REM PDD Device Manager 启动脚本 (Windows)

echo 正在检查依赖...
python -m pip install PyQt6 -q

echo 启动 PDD Device Manager...
cd /d "%~dp0"
python main.py %*
