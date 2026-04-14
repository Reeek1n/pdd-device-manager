@echo off
chcp 65001 >nul
echo 🚀 开始构建 PDD Device Manager Windows 版本...

:: 清理旧构建
echo 🧹 清理旧构建文件...
if exist build rmdir /s /q build
if exist dist rmdir /s /q dist

:: 安装依赖
echo 📦 安装依赖...
pip install -r requirements.txt
pip install pyinstaller

:: 构建应用
echo 🔨 构建应用...
pyinstaller pdd_device_manager_windows.spec --clean

:: 检查构建结果
if exist "dist\PDD Device Manager\PDD Device Manager.exe" (
    echo ✅ 构建成功！
    echo 📱 应用位置: dist\PDD Device Manager\
    
    :: 创建 ZIP 包
    echo 📦 创建 ZIP 分发包...
    cd dist
    tar -czf "PDD-Device-Manager-Windows.zip" "PDD Device Manager"
    cd ..
    
    echo 🎉 完成！分发包: dist\PDD-Device-Manager-Windows.zip
) else (
    echo ❌ 构建失败！
    exit /b 1
)

pause
