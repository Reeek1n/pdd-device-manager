# PDD Device Manager

跨平台 iOS 设备管理工具，支持 Windows 和 macOS。

## 功能特性

- 🔌 **双连接模式**: 支持 WiFi 和 USB 数据线连接
- 📱 **设备管理**: 添加、编辑、删除多台 iOS 设备
- 📊 **实时监控**: 实时查看设备任务状态和进度
- 📥 **产物导出**: 一键导出采集产物到本地
- 🚀 **任务发布**: 向设备发布采集任务
- 🔔 **系统托盘**: 最小化到系统托盘，后台运行
- ⚡ **实时同步**: 采集一个商品，电脑立即显示一个
- ⏹ **即时停止**: 发送停止命令，0.5秒内立即停止

## 实时通信功能

### 插件端 (iOS)
- 每采集一个商品 → 立即写入 `realtime/goods/` 目录
- 每秒更新 `realtime/status.json` 状态文件
- 每 0.5 秒检查 `commands/stop/global.stop` 停止信号

### 桌面端 (Windows/Mac)
- 每 2 秒轮询设备状态
- 实时显示采集到的商品列表
- 发送停止命令后立即生效

## 安装要求

- Python 3.8+ (macOS 使用系统自带的 /usr/bin/python3)
- PyQt6
- sshpass (用于 SSH 连接)

## 安装步骤

### macOS

```bash
# 安装 sshpass
brew install sshpass

# 运行应用 (使用系统 Python3)
./run.sh
```

### Windows

```cmd
# 运行应用
run.bat
```

## 使用方法

### 1. 添加设备
- 点击"+ 添加设备"按钮
- 选择连接方式 (WiFi/USB)
  - WiFi: 输入设备 IP 地址、用户名、密码
  - USB: 需要安装 libimobiledevice 工具

### 2. 查看实时采集
- 选择设备后，在"实时采集"区域可以看到：
  - 当前采集状态
  - 已采集商品列表（实时更新）
  - 采集进度

### 3. 发布任务
- 点击"🚀 发布新任务"按钮
- 输入关键词（每行一个）
- 设置采集数量、价格范围等参数
- 点击"开始采集"

### 4. 停止任务
- 点击"⏹ 停止任务"按钮
- 设备将在 0.5 秒内停止当前操作

### 5. 导出产物
- 任务完成后，点击"📥 导出产物"
- 选择保存目录，下载所有采集数据

## 项目结构

```
pdd_device_manager/
├── main.py              # 主程序入口
├── requirements.txt     # Python 依赖
├── README.md           # 项目说明
├── run.sh              # macOS 启动脚本
└── run.bat             # Windows 启动脚本
```

## iOS 插件修改

为了支持实时通信，需要更新 iOS 插件：

1. 添加 `modules/realtime_sync.inc` 模块
2. 在 `capture_pipeline.inc` 中调用 `realtime_reportGoodsCaptured()`
3. 在 `task_store.inc` 中调用 `realtime_onTaskStarted()` 和 `realtime_onTaskFinished()`
4. 增强停止检查频率到 0.5 秒

## macOS 架构说明

- 使用系统自带的 `/usr/bin/python3` (arm64 架构)
- 如果使用其他 Python 版本，请确保与 PyQt6 架构匹配

## 打包应用

### macOS App

```bash
pip install pyinstaller
pyinstaller --windowed --onefile --name "PDD Device Manager" main.py
```

### Windows EXE

```bash
pip install pyinstaller
pyinstaller --windowed --onefile --name "PDD Device Manager" main.py
```

## 许可证

MIT License
