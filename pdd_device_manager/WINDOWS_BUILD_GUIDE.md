# Windows 版本构建指南

## 方案一：完整安装包（推荐）

使用 Inno Setup 创建安装程序，自动安装 Python 和依赖。

### 准备工作

1. **下载并安装 Inno Setup**
   - 官网：https://jrsoftware.org/isinfo.php
   - 下载 `innosetup-6.2.2.exe` 并安装

2. **下载 Python 安装包**
   - 官网：https://www.python.org/downloads/release/python-31011/
   - 下载 `python-3.10.11-amd64.exe`
   - 放在项目根目录

3. **准备图标文件**
   - 创建 `icon.ico` 图标文件（可选）
   - 放在项目根目录

### 构建步骤

1. **构建应用程序**
   ```cmd
   pip install pyinstaller
   pyinstaller pdd_device_manager_windows.spec --clean
   ```

2. **编译安装包**
   - 打开 Inno Setup Compiler
   - 打开 `windows_installer.iss` 文件
   - 点击 Build -> Compile
   - 生成的安装包在 `installer_output/PDD-Device-Manager-Setup.exe`

### 安装包功能

- ✅ 自动检测并安装 Python 3.10
- ✅ 自动安装所有依赖（PyQt6、paramiko 等）
- ✅ 创建桌面快捷方式
- ✅ 创建开始菜单项
- ✅ 安装完成后自动启动应用

---

## 方案二：便携版（ZIP 包）

不需要安装，解压即用。

### 构建步骤

1. **运行构建脚本**
   ```cmd
   build_windows.bat
   ```

2. **分发**
   - 将 `dist/PDD-Device-Manager-Windows.zip` 发给朋友
   - 朋友解压后运行 `PDD Device Manager.exe`

### 注意

便携版需要目标电脑已安装 Python 3.10+，否则会提示错误。

---

## 方案三：单文件 EXE（最简单）

只有一个 .exe 文件，双击运行。

### 构建步骤

```cmd
pip install pyinstaller
pyinstaller --onefile --windowed --name "PDD Device Manager" main.py
```

生成的文件在 `dist/PDD Device Manager.exe`

### 缺点

- 启动较慢（需要解压）
- 文件较大（约 200MB）
- 同样需要目标电脑有 Python

---

## 推荐方案

| 方案 | 优点 | 缺点 | 适用场景 |
|------|------|------|----------|
| 安装包 | 一键安装，自动配置 | 需要管理员权限 | 普通用户 |
| 便携版 | 无需安装，绿色运行 | 需要预装 Python | 技术人员 |
| 单文件 | 只有一个文件 | 启动慢，体积大 | 快速测试 |

**推荐使用方案一（安装包）**，用户体验最好！
