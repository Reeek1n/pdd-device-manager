#!/bin/bash
# PDD Device Manager 启动脚本 (macOS)

# 使用系统 Python3 (arm64 架构)
PYTHON=/usr/bin/python3

# 检查 PyQt6
$PYTHON -c "import PyQt6" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "正在安装 PyQt6..."
    $PYTHON -m pip install PyQt6 --user
fi

# 启动应用
cd "$(dirname "$0")"
$PYTHON main.py "$@"
