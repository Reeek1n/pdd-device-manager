#!/bin/bash

# PDD Device Manager 启动脚本

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 启动应用
cd "$SCRIPT_DIR"
./"PDD Device Manager"
