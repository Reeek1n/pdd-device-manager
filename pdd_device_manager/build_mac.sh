#!/bin/bash

# PDD Device Manager macOS 构建脚本

echo "🚀 开始构建 PDD Device Manager macOS 版本..."

# 清理旧构建
echo "🧹 清理旧构建文件..."
rm -rf build dist

# 安装依赖
echo "📦 安装依赖..."
pip3 install -r requirements.txt
pip3 install pyinstaller

# 构建应用
echo "🔨 构建应用..."
pyinstaller pdd_device_manager.spec --clean

# 检查构建结果
if [ -d "dist/PDD Device Manager.app" ]; then
    echo "✅ 构建成功！"
    echo "📱 应用位置: dist/PDD Device Manager.app"
    
    # 显示应用信息
    echo ""
    echo "📋 应用信息:"
    ls -lh "dist/PDD Device Manager.app/Contents/MacOS/PDD Device Manager"
    
    # 创建启动脚本
    echo ""
    echo "📝 创建启动脚本..."
    cat > "dist/启动应用.command" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")"
open "PDD Device Manager.app"
EOF
    chmod +x "dist/启动应用.command"
    
    echo ""
    echo "🎉 完成！请打开 dist 文件夹查看应用。"
    echo "💡 提示: 首次运行可能需要在 系统偏好设置 -> 安全性与隐私 中允许应用运行"
else
    echo "❌ 构建失败！"
    exit 1
fi
