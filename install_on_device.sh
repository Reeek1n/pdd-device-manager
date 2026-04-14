#!/bin/bash
# 在 iOS 设备上运行此脚本安装插件

echo "正在安装 PDD 插件..."

# 检查是否以 root 运行
if [ "$EUID" -ne 0 ]; then 
    echo "请以 root 权限运行此脚本"
    echo "使用方法: su -c 'sh install_on_device.sh'"
    exit 1
fi

# 安装 deb 包
dpkg -i /tmp/pdd_install/com.pdd.dataparser_1.1.0_iphoneos-arm.deb

# 修复依赖
apt-get install -f -y

# 重启 SpringBoard
echo "安装完成，正在重启 SpringBoard..."
killall -9 SpringBoard

echo "✅ 插件安装成功！"
