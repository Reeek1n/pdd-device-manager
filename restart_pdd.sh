#!/bin/bash
# PDD 数据采集控制台 - 重启脚本

# 停止占用 8080 端口的进程
lsof -ti :8080 | xargs kill -9 2>/dev/null
echo "已停止 8080 端口进程"

# 等待 1 秒
sleep 1

# 启动服务
/Library/Frameworks/Python.framework/Versions/3.10/bin/python3 /Users/leeekin/Desktop/device/pdd_controller.py --port 8080 &
echo "服务已启动: http://localhost:8080"
