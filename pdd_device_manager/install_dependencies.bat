@echo off
echo 正在安装 PDD Device Manager 依赖...

:: 检查 Python 是否安装
python --version >nul 2>&1
if errorlevel 1 (
    echo Python 未安装，请先安装 Python 3.10+
    exit /b 1
)

:: 升级 pip
echo 升级 pip...
python -m pip install --upgrade pip

:: 安装依赖
echo 安装 PyQt6...
pip install PyQt6==6.4.2

echo 安装 paramiko...
pip install paramiko==3.3.1

echo 安装其他依赖...
pip install bcrypt==4.0.1
pip install cryptography==41.0.3
pip install pynacl==1.5.0

echo 依赖安装完成！
