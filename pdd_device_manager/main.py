#!/usr/bin/env python3
"""
PDD Device Manager - 跨平台 iOS 设备管理工具
支持 Windows 和 macOS
"""

import sys
import json
import os
import subprocess
import threading
import time
from pathlib import Path
from datetime import datetime
from typing import Optional, Dict, List, Callable

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QLineEdit, QTextEdit, QTableWidget, QTableWidgetItem,
    QGroupBox, QFormLayout, QSpinBox, QComboBox, QCheckBox, QProgressBar,
    QTabWidget, QSplitter, QMessageBox, QFileDialog, QMenu, QSystemTrayIcon,
    QStatusBar, QToolBar, QDialog, QDialogButtonBox, QGridLayout, QFrame,
    QListWidget, QListWidgetItem, QStackedWidget, QScrollArea, QHeaderView,
    QRadioButton
)
from PyQt6.QtCore import (
    Qt, QThread, pyqtSignal, QTimer, QSize, QSettings, QUrl
)
from PyQt6.QtGui import (
    QIcon, QAction, QFont, QPalette, QColor, QDesktopServices
)
from PyQt6.QtWidgets import QGraphicsDropShadowEffect


# ============== 后台任务线程 ==============

class TaskSubmitThread(QThread):
    """任务提交后台线程 - 避免阻塞主线程"""
    progress = pyqtSignal(str)  # 进度消息
    finished = pyqtSignal(bool, list, list)  # success_count, failed_keywords, success_keywords
    
    def __init__(self, device, keywords, config, parent=None):
        super().__init__(parent)
        self.device = device
        self.keywords = keywords
        self.config = config
        self.running = True
    
    def run(self):
        """在后台线程中执行任务提交"""
        success_count = 0
        failed_keywords = []
        success_keywords = []
        
        # 先重启拼多多应用
        self.progress.emit("正在重启拼多多应用...")
        restart_success = self.restart_pinduoduo()
        if not restart_success:
            self.progress.emit("⚠️ 拼多多重启失败，继续提交任务...")
        else:
            self.progress.emit("✅ 拼多多已重启")
        
        # 构建任务命令
        sort_map = {
            "综合排序": "",
            "销量优先": "sales",
            "价格低到高": "price_asc",
            "价格高到低": "price_desc"
        }
        
        for keyword in self.keywords:
            if not self.running:
                break
            
            # 每个任务开始前重启拼多多
            self.progress.emit(f"准备执行任务: {keyword}")
            restart_success = self.restart_pinduoduo()
            if not restart_success:
                self.progress.emit(f"⚠️ 拼多多重启失败，尝试继续执行任务: {keyword}")
            else:
                self.progress.emit(f"✅ 拼多多已重启，开始执行任务: {keyword}")
                
            sort_by = sort_map.get(self.config.get("sort_by", ""), "")
            
            # 提交任务
            self.progress.emit(f"正在提交任务: {keyword}...")
            
            pddctl_cmd = [
                "python3",
                str(Path(__file__).parent.parent / "pdd-ios-device-collect" / "scripts" / "pddctl.py"),
                "--config", str(Path(__file__).parent.parent / "pdd-ios-device-collect" / "device-config.json"),
                "task", "collect",
                "--keyword", keyword,
                "--count", str(self.config.get("count", 5))
            ]
            
            if sort_by:
                pddctl_cmd.extend(["--sort-by", sort_by])
            if self.config.get("price_min"):
                pddctl_cmd.extend(["--price-min", str(self.config["price_min"])])
            if self.config.get("price_max"):
                pddctl_cmd.extend(["--price-max", str(self.config["price_max"])])
            
            try:
                # 生成任务ID
                import uuid
                task_id = f"task_{int(time.time())}_{uuid.uuid4().hex[:8]}"
                
                # 构建JSON格式的任务命令（插件期望的格式）
                task_info = {
                    "action": "collect",
                    "task_id": task_id,
                    "keyword": keyword,
                    "count": self.config.get("count", 5)
                }
                
                # 添加可选参数
                sort_map = {
                    "综合排序": "",
                    "销量优先": "sales",
                    "价格低到高": "price_asc",
                    "价格高到低": "price_desc"
                }
                sort_by = sort_map.get(self.config.get("sort_by", ""), "")
                if sort_by:
                    task_info["sort_by"] = sort_by
                if self.config.get("price_min"):
                    task_info["price_min"] = str(self.config["price_min"])
                if self.config.get("price_max"):
                    task_info["price_max"] = str(self.config["price_max"])
                
                # 创建JSON命令文件（插件只处理.json文件）
                inbox_path = f"{self.device.remote_path}/commands/inbox"
                json_file = f"{inbox_path}/{task_id}.json"
                
                # 将JSON内容写入文件
                json_content = json.dumps(task_info, ensure_ascii=False)
                # 使用printf避免引号问题
                create_cmd = f"mkdir -p '{inbox_path}' && printf '%s' '{json_content}' > '{json_file}'"
                success, stdout, stderr = SSHManager.execute_command(
                    self.device.host, self.device.user, self.device.password,
                    create_cmd, self.device.port, timeout=10
                )
                
                if success:
                    self.progress.emit(f"✅ 任务已提交: {keyword}")
                    self.last_task_id = task_id  # 记录任务ID
                    
                    # 等待任务完成
                    self.progress.emit(f"⏳ 等待任务完成: {keyword}...")
                    task_completed = self.wait_for_task_complete()
                    
                    if task_completed:
                        success_count += 1
                        success_keywords.append(keyword)
                        self.progress.emit(f"✅ 任务完成: {keyword}")
                    else:
                        failed_keywords.append(f"{keyword}: 任务未完成或超时")
                        self.progress.emit(f"❌ 任务未完成: {keyword}")
                else:
                    failed_keywords.append(f"{keyword}: 创建命令文件失败 - {stderr}")
                    self.progress.emit(f"❌ 任务提交失败: {keyword}")
                    
            except Exception as e:
                failed_keywords.append(f"{keyword}: {e}")
                self.progress.emit(f"❌ 任务异常: {keyword}")
        
        self.finished.emit(success_count, failed_keywords, success_keywords)
    
    def restart_pinduoduo(self) -> bool:
        """重启拼多多应用"""
        try:
            self.progress.emit(f"正在重启 {self.device.name} 上的拼多多...")
            
            # 先关闭拼多多
            kill_cmd = "killall -9 pinduoduo 2>/dev/null || true"
            success, stdout, stderr = SSHManager.execute_command(
                self.device.host, self.device.user, self.device.password, kill_cmd, self.device.port
            )
            
            # 等待应用完全关闭
            time.sleep(2)
            
            # 启动拼多多
            bundle_id = "com.xunmeng.pinduoduo"
            start_cmd = f"open 'pinduoduo://' 2>/dev/null || uiopen 'pinduoduo://' 2>/dev/null || open '{bundle_id}' 2>/dev/null || true"
            
            success, stdout, stderr = SSHManager.execute_command(
                self.device.host, self.device.user, self.device.password, start_cmd, self.device.port
            )
            
            # 等待应用启动
            self.progress.emit("等待拼多多启动...")
            time.sleep(5)
            
            # 检查应用是否运行
            check_cmd = "ps aux | grep -i pinduoduo | grep -v grep"
            success, stdout, stderr = SSHManager.execute_command(
                self.device.host, self.device.user, self.device.password, check_cmd, self.device.port
            )
            
            return success and stdout.strip()
                
        except Exception as e:
            self.progress.emit(f"重启拼多多失败: {e}")
            return False
    
    def wait_for_task_complete(self, timeout: int = 1800) -> bool:
        """等待当前任务完成 - 通过SSH直接读取状态文件"""
        start_time = time.time()
        check_interval = 3  # 每3秒检查一次
        last_task_id = None
        
        while self.running and (time.time() - start_time) < timeout:
            try:
                # 直接使用SSH读取全局状态文件
                status_file = f"{self.device.remote_path}/commands/status/current.json"
                read_cmd = f"cat '{status_file}' 2>/dev/null || echo '{{}}'"
                
                success, stdout, stderr = SSHManager.execute_command(
                    self.device.host, self.device.user, self.device.password, 
                    read_cmd, self.device.port, timeout=10
                )
                
                if success and stdout.strip():
                    try:
                        data = json.loads(stdout.strip())
                        state = data.get("state", "unknown")
                        active = data.get("active", False)
                        saved = data.get("saved_count", 0)
                        target = data.get("target_count", 0)
                        keyword = data.get("keyword", "")
                        task_id = data.get("task_id", "")
                        
                        # 记录当前任务ID
                        if task_id and active:
                            last_task_id = task_id
                        
                        # 更新进度
                        if target > 0:
                            progress = int((saved / target) * 100)
                            self.progress.emit(f"⏳ {keyword}: {saved}/{target} ({progress}%)")
                        elif keyword:
                            self.progress.emit(f"⏳ {keyword}: 采集中...")
                        
                        # 检查任务是否完成
                        if not active or state in ["completed", "failed", "stopped", "idle"]:
                            # 任务结束，检查具体状态
                            if state == "completed":
                                return True
                            elif state in ["failed", "stopped"]:
                                return False
                            elif state == "idle":
                                # 变为idle，需要检查任务目录确认是否真的完成
                                if last_task_id:
                                    task_completed = self.check_task_directory_complete(last_task_id)
                                    if task_completed:
                                        return True
                                # 如果没有任务ID或任务未完成，继续等待
                                pass
                    except json.JSONDecodeError:
                        pass
                
                # 等待下次检查
                time.sleep(check_interval)
                
            except Exception as e:
                self.progress.emit(f"检查任务状态出错: {e}")
                time.sleep(check_interval)
        
        # 超时前再检查一次任务目录
        if last_task_id:
            return self.check_task_directory_complete(last_task_id)
        
        # 超时
        self.progress.emit("⏰ 等待任务完成超时")
        return False
    
    def check_task_directory_complete(self, task_id: str) -> bool:
        """检查任务目录中的状态文件，确认任务是否完成"""
        try:
            task_status_file = f"{self.device.remote_path}/tasks/{task_id}/status.json"
            read_cmd = f"cat '{task_status_file}' 2>/dev/null || echo '{{}}'"
            
            success, stdout, stderr = SSHManager.execute_command(
                self.device.host, self.device.user, self.device.password,
                read_cmd, self.device.port, timeout=10
            )
            
            if success and stdout.strip():
                try:
                    data = json.loads(stdout.strip())
                    state = data.get("state", "unknown")
                    saved = data.get("saved_count", 0)
                    target = data.get("target_count", 0)
                    
                    self.progress.emit(f"📁 任务目录状态: {state}, {saved}/{target}")
                    
                    if state == "completed":
                        return True
                    elif state in ["failed", "stopped"]:
                        return False
                    elif saved >= target and target > 0:
                        # 采集数量达到目标，也算完成
                        return True
                    elif saved > 0:
                        # 有采集数据，但可能未完成，再等待一下
                        return False
                except json.JSONDecodeError:
                    pass
            
            return False
        except Exception as e:
            self.progress.emit(f"检查任务目录出错: {e}")
            return False
    
    def stop(self):
        self.running = False


class ExportThread(QThread):
    """导出产物后台线程 - 避免阻塞主线程"""
    progress = pyqtSignal(str)  # 进度消息
    finished = pyqtSignal(bool, int, str)  # success, count, message
    
    def __init__(self, device, save_dir, parent=None):
        super().__init__(parent)
        self.device = device
        self.save_dir = save_dir
        self.running = True
    
    def run(self):
        """在后台线程中执行导出"""
        try:
            self.progress.emit("正在扫描产物...")
            
            # 调试信息
            print(f"[ExportThread] 设备: {self.device.name}, host: {self.device.host}, port: {self.device.port}")
            
            # 直接从固定导出目录获取所有 txt 文件
            export_dir = "/var/mobile/PDDExports"
            cmd = f"ls -1 {export_dir}/*.txt 2>/dev/null"
            success, stdout, stderr = SSHManager.execute_command(
                self.device.host, self.device.user, self.device.password,
                cmd, self.device.port, timeout=30
            )
            
            print(f"[ExportThread] 命令执行结果: success={success}, stdout={stdout[:100] if stdout else 'empty'}, stderr={stderr[:100] if stderr else 'empty'}")
            
            if not success or not stdout.strip():
                self.finished.emit(False, 0, f"没有找到产物文件: {stderr}")
                return
            
            files = [f.strip() for f in stdout.strip().split('\n') if f.strip()]
            total_files = len(files)
            
            if total_files == 0:
                self.finished.emit(False, 0, "没有找到产物文件")
                return
            
            self.progress.emit(f"找到 {total_files} 个产物文件，开始下载...")
            
            # 下载所有文件到本地目录
            downloaded = 0
            self.progress.emit(f"开始下载 {total_files} 个文件到 {self.save_dir}...")
            
            for remote_file in files:
                if not self.running:
                    break
                
                # 获取文件名
                filename = os.path.basename(remote_file)
                local_file = os.path.join(self.save_dir, filename)
                
                print(f"[ExportThread] 下载: {remote_file} -> {local_file}")
                
                # 使用 paramiko SFTP 下载文件
                try:
                    import paramiko
                    
                    # 建立 SSH 连接
                    client = paramiko.SSHClient()
                    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
                    client.connect(
                        hostname=self.device.host,
                        port=self.device.port,
                        username=self.device.user,
                        password=self.device.password,
                        timeout=30,
                        allow_agent=False,
                        look_for_keys=False
                    )
                    
                    # 创建 SFTP 会话并下载文件
                    sftp = client.open_sftp()
                    sftp.get(remote_file, local_file)
                    sftp.close()
                    client.close()
                    
                    downloaded += 1
                    self.progress.emit(f"已下载 {downloaded}/{total_files}: {filename}")
                    print(f"[ExportThread] 下载成功: {filename}")
                    
                except ImportError:
                    print(f"[ExportThread] 下载失败: paramiko 未安装")
                except Exception as e:
                    print(f"[ExportThread] 下载异常: {e}")
            
            print(f"[ExportThread] 完成: 下载了 {downloaded}/{total_files} 个文件")
            
            # 删除设备上已导出的文件
            if downloaded > 0:
                self.progress.emit(f"正在清理设备上的已导出文件...")
                delete_cmd = f"rm -f {export_dir}/*.txt"
                delete_success, _, delete_stderr = SSHManager.execute_command(
                    self.device.host, self.device.user, self.device.password,
                    delete_cmd, self.device.port, timeout=30
                )
                if delete_success:
                    self.progress.emit(f"已清理设备上的 {downloaded} 个文件")
                else:
                    print(f"[ExportThread] 清理文件失败: {delete_stderr}")
            
            self.finished.emit(True, downloaded, f"成功导出 {downloaded} 个文件到 {self.save_dir}")
            
        except Exception as e:
            self.finished.emit(False, 0, f"导出失败: {e}")
    
    def stop(self):
        self.running = False


# ============== 数据模型 ==============

class Device:
    """设备模型"""
    def __init__(self, device_id: str, name: str, host: str, user: str, 
                 password: str, port: int = 22, use_usb: bool = False):
        self.id = device_id
        self.name = name
        self.host = host
        self.user = user
        self.password = password
        self.port = port
        self.use_usb = use_usb
        self.remote_path: Optional[str] = None
        self.status = "offline"  # offline, online, busy, error
        self.last_seen: Optional[datetime] = None
        self.current_task: Optional[str] = None
    
    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "host": self.host,
            "user": self.user,
            "password": self.password,
            "port": self.port,
            "use_usb": self.use_usb,
            "remote_path": self.remote_path,
            "status": self.status,
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "Device":
        device = cls(
            device_id=data["id"],
            name=data["name"],
            host=data["host"],
            user=data["user"],
            password=data["password"],
            port=data.get("port", 22),
            use_usb=data.get("use_usb", False)
        )
        device.remote_path = data.get("remote_path")
        device.status = data.get("status", "offline")
        return device


class Task:
    """任务模型"""
    def __init__(self, task_id: str, keyword: str, device_id: str):
        self.id = task_id
        self.keyword = keyword
        self.device_id = device_id
        self.status = "pending"  # pending, running, completed, failed, stopped
        self.created_at = datetime.now()
        self.started_at: Optional[datetime] = None
        self.finished_at: Optional[datetime] = None
        self.progress = 0
        self.saved_count = 0
        self.attempted_count = 0
        self.target_count = 0
        self.error_message: Optional[str] = None
    
    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "keyword": self.keyword,
            "device_id": self.device_id,
            "status": self.status,
            "created_at": self.created_at.isoformat(),
            "started_at": self.started_at.isoformat() if self.started_at else None,
            "finished_at": self.finished_at.isoformat() if self.finished_at else None,
            "progress": self.progress,
            "saved_count": self.saved_count,
            "attempted_count": self.attempted_count,
            "target_count": self.target_count,
            "error_message": self.error_message,
        }


# ============== SSH 连接管理 ==============

class USBManager:
    """USB 连接管理器 - 使用 usbmuxd/iproxy 进行 USB 连接"""
    
    _last_device_count = 0
    _device_connected_callback = None
    _monitor_timer = None
    
    @staticmethod
    def is_device_connected() -> bool:
        """检查是否有 iOS 设备通过 USB 连接"""
        try:
            result = subprocess.run(
                ["idevice_id", "-l"],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0 and result.stdout.strip()
        except:
            return False
    
    @staticmethod
    def get_device_count() -> int:
        """获取连接的 iOS 设备数量"""
        try:
            result = subprocess.run(
                ["idevice_id", "-l"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                return len([l for l in result.stdout.strip().split("\n") if l.strip()])
            return 0
        except:
            return 0
    
    @staticmethod
    def get_device_info() -> List[dict]:
        """获取连接的 iOS 设备信息列表"""
        devices = []
        try:
            result = subprocess.run(
                ["idevice_id", "-l"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                for line in result.stdout.strip().split("\n"):
                    if line.strip():
                        parts = line.strip().split()
                        udid = parts[0]
                        # 获取设备名称
                        name_result = subprocess.run(
                            ["idevicename", "-u", udid],
                            capture_output=True,
                            text=True,
                            timeout=5
                        )
                        name = name_result.stdout.strip() if name_result.returncode == 0 else "iOS Device"
                        devices.append({
                            "udid": udid,
                            "name": name,
                            "connection": "USB"
                        })
        except Exception as e:
            print(f"[USBManager] 获取设备信息失败: {e}")
        return devices
    
    @staticmethod
    def get_device_udid() -> Optional[str]:
        """获取连接设备的 UDID"""
        try:
            result = subprocess.run(
                ["idevice_id", "-l"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                lines = result.stdout.strip().split("\n")
                for line in lines:
                    if line.strip():
                        return line.strip().split()[0]
            return None
        except:
            return None
    
    @staticmethod
    def start_proxy(local_port: int = 2222, remote_port: int = 22) -> Optional[subprocess.Popen]:
        """启动 iproxy 进行端口转发"""
        try:
            process = subprocess.Popen(
                ["iproxy", str(local_port), str(remote_port)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            time.sleep(1)
            return process
        except:
            return None
    
    @staticmethod
    def stop_proxy(process: subprocess.Popen):
        """停止 iproxy 进程"""
        if process:
            try:
                process.terminate()
                process.wait(timeout=2)
            except:
                try:
                    process.kill()
                except:
                    pass
    
    @classmethod
    def start_monitoring(cls, callback):
        """开始监控 USB 设备连接状态"""
        cls._device_connected_callback = callback
        cls._last_device_count = cls.get_device_count()
        
        def check_devices():
            current_count = cls.get_device_count()
            if current_count > cls._last_device_count:
                # 有新设备连接
                new_devices = cls.get_device_info()
                if cls._device_connected_callback:
                    for device in new_devices:
                        cls._device_connected_callback(device)
            cls._last_device_count = current_count
        
        # 使用 QTimer 定期检查
        from PyQt6.QtCore import QTimer
        cls._monitor_timer = QTimer()
        cls._monitor_timer.timeout.connect(check_devices)
        cls._monitor_timer.start(2000)  # 每2秒检查一次
    
    @classmethod
    def stop_monitoring(cls):
        """停止监控"""
        if cls._monitor_timer:
            cls._monitor_timer.stop()
            cls._monitor_timer = None


class NetworkScanner:
    """局域网设备扫描器"""
    
    @staticmethod
    def get_local_network_range() -> str:
        """获取本地网络范围"""
        try:
            # 获取默认网关的网段
            result = subprocess.run(
                ["ipconfig", "getifaddr", "en0"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                ip = result.stdout.strip()
                parts = ip.split(".")
                return f"{parts[0]}.{parts[1]}.{parts[2]}.0/24"
        except:
            pass
        return "192.168.0.0/24"
    
    @staticmethod
    def scan_port(ip: str, port: int, timeout: float = 1.0) -> bool:
        """扫描单个端口"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            result = sock.connect_ex((ip, port))
            sock.close()
            return result == 0
        except:
            return False
    
    @staticmethod
    def check_ssh_service(ip: str, timeout: float = 2.0) -> tuple[bool, Optional[str]]:
        """检查是否是 SSH 服务 (iOS 设备)"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            sock.connect((ip, 22))
            
            # 接收 banner
            banner = sock.recv(1024).decode('utf-8', errors='ignore')
            sock.close()
            
            # 检查是否是 OpenSSH
            if "SSH" in banner:
                return True, banner.strip()
        except:
            pass
        return False, None
    
    @staticmethod
    def scan_network(network_range: str = None, progress_callback=None) -> List[dict]:
        """扫描局域网中的 iOS 设备"""
        if network_range is None:
            network_range = NetworkScanner.get_local_network_range()
        
        # 解析网段
        parts = network_range.replace("/24", "").split(".")
        base_ip = f"{parts[0]}.{parts[1]}.{parts[2]}"
        
        found_devices = []
        total = 254
        
        # 使用线程池并发扫描
        from concurrent.futures import ThreadPoolExecutor, as_completed
        
        def check_ip(i):
            ip = f"{base_ip}.{i}"
            is_ssh, banner = NetworkScanner.check_ssh_service(ip)
            if is_ssh:
                return {
                    "ip": ip,
                    "port": 22,
                    "banner": banner,
                    "connection": "WiFi"
                }
            return None
        
        with ThreadPoolExecutor(max_workers=50) as executor:
            futures = {executor.submit(check_ip, i): i for i in range(1, 255)}
            completed = 0
            
            for future in as_completed(futures):
                completed += 1
                result = future.result()
                if result:
                    found_devices.append(result)
                
                if progress_callback and completed % 10 == 0:
                    progress_callback(completed, total)
        
        return found_devices
    
    @staticmethod
    def test_device_connection(ip: str, user: str = "mobile", password: str = "001314", timeout: int = 5) -> tuple[bool, Optional[str]]:
        """测试设备连接并获取信息"""
        success, message = SSHManager.test_connection(ip, user, password, 22, timeout)
        if success:
            # 尝试获取设备名称
            name_success, name_stdout, _ = SSHManager.execute_command(
                ip, user, password, "hostname", 22, timeout
            )
            device_name = name_stdout.strip() if name_success else f"iOS Device ({ip})"
            
            # 尝试发现 PDD 路径
            remote_path = SSHManager.discover_remote_path(ip, user, password, 22)
            
            return True, device_name, remote_path
        return False, None, None


class SSHManager:
    """SSH 连接管理器 - 使用 paramiko 实现跨平台支持"""
    
    _usb_proxy_process: Optional[subprocess.Popen] = None
    _usb_local_port: int = 2222
    
    @classmethod
    def setup_usb_connection(cls) -> tuple[bool, str]:
        """设置 USB 连接"""
        if not USBManager.is_device_connected():
            return False, "没有检测到 USB 连接的 iOS 设备"
        
        udid = USBManager.get_device_udid()
        if not udid:
            return False, "无法获取设备 UDID"
        
        # 停止之前的代理
        if cls._usb_proxy_process:
            USBManager.stop_proxy(cls._usb_proxy_process)
        
        # 启动新的代理
        cls._usb_proxy_process = USBManager.start_proxy(cls._usb_local_port, 22)
        if not cls._usb_proxy_process:
            return False, "无法启动 USB 端口转发"
        
        return True, f"USB 连接已建立 (设备: {udid[:16]}...)"
    
    @classmethod
    def get_connection_params(cls, host: str, port: int) -> tuple[str, int]:
        """获取实际的连接参数 (处理 USB 连接)"""
        if host == "usb" or host == "127.0.0.1" and cls._usb_proxy_process:
            return "127.0.0.1", cls._usb_local_port
        return host, port
    
    @staticmethod
    def _get_ssh_client(host: str, user: str, password: str, port: int = 22, timeout: int = 10):
        """获取 SSH 客户端连接"""
        try:
            import paramiko
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(
                hostname=host,
                port=port,
                username=user,
                password=password,
                timeout=timeout,
                allow_agent=False,
                look_for_keys=False
            )
            return client
        except ImportError:
            raise ImportError("paramiko 未安装，请运行: pip install paramiko")
        except Exception as e:
            raise e
    
    @staticmethod
    def test_connection(host: str, user: str, password: str, port: int = 22, timeout: int = 10, use_usb: bool = False) -> tuple[bool, str]:
        """测试 SSH 连接"""
        try:
            client = SSHManager._get_ssh_client(host, user, password, port, timeout)
            stdin, stdout, stderr = client.exec_command("echo 'connected'", timeout=timeout)
            output = stdout.read().decode().strip()
            client.close()
            
            if "connected" in output:
                return True, "连接成功"
            return False, "连接测试失败"
        except ImportError as e:
            return False, f"缺少依赖: {str(e)}"
        except Exception as e:
            return False, str(e)
    
    @staticmethod
    def execute_command(host: str, user: str, password: str, command: str, 
                        port: int = 22, timeout: int = 30) -> tuple[bool, str, str]:
        """执行远程命令"""
        try:
            client = SSHManager._get_ssh_client(host, user, password, port, timeout)
            stdin, stdout, stderr = client.exec_command(command, timeout=timeout)
            
            stdout_data = stdout.read().decode()
            stderr_data = stderr.read().decode()
            exit_code = stdout.channel.recv_exit_status()
            
            client.close()
            return exit_code == 0, stdout_data, stderr_data
        except ImportError as e:
            return False, "", f"缺少依赖: {str(e)}"
        except Exception as e:
            return False, "", str(e)
    
    @staticmethod
    def discover_remote_path(host: str, user: str, password: str, port: int = 22) -> Optional[str]:
        """发现远程设备上的 PDDGoodsData 路径"""
        cmd = "find /var/mobile/Containers/Data/Application -maxdepth 4 -type d -path '*/Documents/PDDGoodsData' 2>/dev/null | head -1"
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port, timeout=15)
        if success and stdout.strip():
            return stdout.strip()
        return None
    
    @staticmethod
    def get_task_status(host: str, user: str, password: str, remote_path: str,
                        port: int = 22) -> Optional[dict]:
        """获取设备上的任务状态"""
        # 查找最新的任务状态文件
        cmd = f"find {remote_path}/tasks -name 'status.json' -type f -exec ls -t {{}} + 2>/dev/null | head -1"
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port)
        
        if not success or not stdout.strip():
            return None
        
        status_file = stdout.strip()
        cmd = f"cat '{status_file}'"
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port)
        
        if success and stdout.strip():
            try:
                return json.loads(stdout)
            except json.JSONDecodeError:
                return None
        return None
    
    @staticmethod
    def list_artifacts(host: str, user: str, password: str, remote_path: str,
                       port: int = 22) -> List[dict]:
        """列出设备上的产物 - 使用单个SSH命令提高效率"""
        artifacts = []
        
        # 使用单个命令查找所有包含产物文件的raw目录
        # 找到所有raw目录下的.txt文件（排除debug文件）
        cmd = f"find {remote_path}/tasks -path '*/raw/*.txt' ! -name '*debug*' -type f 2>/dev/null"
        
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port, timeout=30)
        
        if success and stdout.strip():
            # 统计每个任务目录下的文件数量
            task_counts = {}
            for line in stdout.strip().split("\n"):
                line = line.strip()
                if not line:
                    continue
                # 从路径中提取任务目录名
                # 路径格式: .../tasks/{task_id}/raw/{goods_id}.txt
                parts = line.split('/')
                if len(parts) >= 2:
                    # 找到raw目录的父目录（即任务目录）
                    for i, part in enumerate(parts):
                        if part == 'raw' and i > 0:
                            task_dir = parts[i-1]
                            if task_dir:
                                task_counts[task_dir] = task_counts.get(task_dir, 0) + 1
                            break
            
            # 构建产物列表
            for task_dir, file_count in task_counts.items():
                if file_count > 0:
                    raw_path = f"{remote_path}/tasks/{task_dir}/raw"
                    artifacts.append({
                        "task_id": task_dir,
                        "path": raw_path,
                        "file_count": file_count
                    })
                    
        return artifacts
    
    @staticmethod
    def download_directory(host: str, user: str, password: str, remote_dir: str,
                          local_dir: str, port: int = 22) -> tuple[bool, str]:
        """下载远程目录到本地 - 使用 paramiko SFTP"""
        try:
            import paramiko
            
            # 建立 SSH 连接
            client = SSHManager._get_ssh_client(host, user, password, port, timeout=30)
            
            # 创建 SFTP 会话
            sftp = client.open_sftp()
            
            # 确保本地目录存在
            os.makedirs(local_dir, exist_ok=True)
            
            # 获取远程文件列表
            try:
                remote_files = sftp.listdir(remote_dir)
            except IOError as e:
                sftp.close()
                client.close()
                return False, f"无法访问远程目录: {str(e)}"
            
            # 下载每个文件
            downloaded = 0
            for filename in remote_files:
                remote_path = f"{remote_dir}/{filename}"
                local_path = os.path.join(local_dir, filename)
                
                try:
                    sftp.get(remote_path, local_path)
                    downloaded += 1
                except Exception as e:
                    print(f"[SSHManager] 下载文件失败 {filename}: {e}")
            
            sftp.close()
            client.close()
            
            return True, f"成功下载 {downloaded} 个文件"
        except ImportError as e:
            return False, f"缺少依赖: {str(e)}"
        except Exception as e:
            return False, str(e)
    
    @staticmethod
    def get_realtime_status(host: str, user: str, password: str, remote_path: str,
                           port: int = 22) -> Optional[dict]:
        """获取实时状态"""
        cmd = f"cat {remote_path}/realtime/status.json 2>/dev/null"
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port, timeout=5)
        if success and stdout.strip():
            try:
                return json.loads(stdout)
            except json.JSONDecodeError:
                return None
        return None
    
    @staticmethod
    def get_realtime_goods(host: str, user: str, password: str, remote_path: str,
                          port: int = 22, limit: int = 10) -> List[dict]:
        """获取实时采集的商品列表"""
        # 获取商品文件列表
        cmd = f"ls -t {remote_path}/realtime/goods/*.json 2>/dev/null | head -{limit}"
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port, timeout=5)
        
        goods_list = []
        if success and stdout.strip():
            for file_path in stdout.strip().split("\n"):
                if file_path:
                    # 读取每个商品文件
                    read_cmd = f"cat '{file_path}'"
                    file_success, file_stdout, file_stderr = SSHManager.execute_command(
                        host, user, password, read_cmd, port, timeout=3
                    )
                    if file_success and file_stdout.strip():
                        try:
                            goods_data = json.loads(file_stdout)
                            goods_list.append(goods_data)
                        except json.JSONDecodeError:
                            continue
        return goods_list
    
    @staticmethod
    def send_stop_command(host: str, user: str, password: str, remote_path: str,
                         port: int = 22, use_global: bool = True) -> tuple[bool, str]:
        """发送停止命令"""
        timestamp = int(time.time())
        
        if use_global:
            # 发送全局停止信号（立即停止所有任务）
            stop_file = f"{remote_path}/commands/stop/global.stop"
            cmd = f"mkdir -p {remote_path}/commands/stop && echo 'stop' > '{stop_file}'"
        else:
            # 发送特定任务停止信号
            stop_file = f"{remote_path}/commands/stop/stop_{timestamp}.flag"
            cmd = f"mkdir -p {remote_path}/commands/stop && echo 'stop' > '{stop_file}'"
        
        success, stdout, stderr = SSHManager.execute_command(host, user, password, cmd, port, timeout=5)
        if success:
            return True, "停止命令已发送"
        return False, stderr or "发送失败"


# ============== Socket 实时通信客户端 ==============

class SocketClientThread(QThread):
    """Socket 客户端线程 - 实现真正的即时双向通信"""
    connected = pyqtSignal(str)  # device_id
    disconnected = pyqtSignal(str)  # device_id
    message_received = pyqtSignal(str, dict)  # device_id, message
    goods_captured = pyqtSignal(str, dict)  # device_id, goods_data
    task_started = pyqtSignal(str, dict)  # device_id, task_info
    task_finished = pyqtSignal(str, dict)  # device_id, result
    progress_updated = pyqtSignal(str, dict)  # device_id, progress
    
    def __init__(self, device: Device, socket_port: int = 9999):
        super().__init__()
        self.device = device
        self.socket_port = socket_port
        self.running = True
        self.socket: Optional[socket.socket] = None
        self.connected_flag = False
        self.buffer = ""
    
    def run(self):
        while self.running:
            try:
                if not self.connected_flag:
                    self._connect()
                
                if self.connected_flag and self.socket:
                    # 接收数据
                    data = self.socket.recv(4096)
                    if data:
                        self.buffer += data.decode('utf-8')
                        self._process_buffer()
                    else:
                        # 连接断开
                        self._disconnect()
                        
            except socket.timeout:
                pass
            except Exception as e:
                print(f"[SocketClient] Error: {e}")
                self._disconnect()
            
            self.msleep(10)  # 10ms 高频检查
    
    def _connect(self):
        """连接到设备 Socket 服务端"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(3.0)
            
            # 直接连接设备的 Socket 端口
            # 注意：需要在设备上开放端口或通过 SSH 隧道
            self.socket.connect((self.device.host, self.socket_port))
            
            self.socket.setblocking(False)
            self.connected_flag = True
            self.connected.emit(self.device.id)
            print(f"[SocketClient] 已连接到设备 {self.device.name} 的 Socket 端口 {self.socket_port}")
            
        except Exception as e:
            if self.socket:
                try:
                    self.socket.close()
                except:
                    pass
                self.socket = None
            # print(f"[SocketClient] 连接失败: {e}")
            self.msleep(2000)  # 2秒后重试
    
    def _disconnect(self):
        """断开连接"""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
        
        if self.connected_flag:
            self.connected_flag = False
            self.disconnected.emit(self.device.id)
            print(f"[SocketClient] 与设备 {self.device.name} 断开连接")
    
    def _process_buffer(self):
        """处理接收到的数据"""
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            if line.strip():
                try:
                    message = json.loads(line)
                    self._handle_message(message)
                except json.JSONDecodeError:
                    pass
    
    def _handle_message(self, message: dict):
        """处理消息"""
        msg_type = message.get("type", "")
        
        if msg_type == "goods_captured":
            self.goods_captured.emit(self.device.id, message)
        elif msg_type == "task_started":
            self.task_started.emit(self.device.id, message)
        elif msg_type == "task_finished":
            self.task_finished.emit(self.device.id, message)
        elif msg_type == "progress":
            self.progress_updated.emit(self.device.id, message)
        elif msg_type == "status":
            self.message_received.emit(self.device.id, message)
        elif msg_type == "connected":
            print(f"[SocketClient] 设备连接确认: {message.get('message')}")
        elif msg_type == "command_ack":
            print(f"[SocketClient] 命令确认: {message}")
        
        # 通用消息转发
        self.message_received.emit(self.device.id, message)
    
    def send_command(self, command: str, data: dict = None) -> bool:
        """发送命令到设备"""
        if not self.connected_flag or not self.socket:
            return False
        
        try:
            message = {"command": command}
            if data:
                message.update(data)
            
            json_str = json.dumps(message) + "\n"
            self.socket.sendall(json_str.encode('utf-8'))
            return True
        except Exception as e:
            print(f"[SocketClient] 发送命令失败: {e}")
            return False
    
    def stop_task(self) -> bool:
        """发送立即停止命令"""
        return self.send_command("stop")
    
    def pause_task(self) -> bool:
        """发送暂停命令"""
        return self.send_command("pause")
    
    def resume_task(self) -> bool:
        """发送恢复命令"""
        return self.send_command("resume")
    
    def get_status(self) -> bool:
        """请求当前状态"""
        return self.send_command("get_status")
    
    def start_task(self, keyword: str, count: int) -> bool:
        """启动新任务"""
        return self.send_command("new_task", {"keyword": keyword, "count": count})
    
    def stop(self):
        """停止线程"""
        self.running = False
        self._disconnect()


# ============== 后台监控线程 (保留 SSH 作为备用) ==============

class DeviceMonitorThread(QThread):
    """设备监控线程 - SSH 轮询作为 Socket 的备用，带自动重连"""
    device_updated = pyqtSignal(str, dict)  # device_id, status_info
    realtime_goods_updated = pyqtSignal(str, list)  # device_id, goods_list
    connection_lost = pyqtSignal(str)  # device_id
    connection_restored = pyqtSignal(str)  # device_id
    
    def __init__(self, device: Device, interval: int = 5):
        super().__init__()
        self.device = device
        self.interval = interval
        self.running = True
        self.last_goods_count = 0
        self.consecutive_failures = 0
        self.max_failures = 3  # 连续失败3次才标记为离线
        self.was_online = False
    
    def run(self):
        while self.running:
            try:
                # 测试连接
                success, message = SSHManager.test_connection(
                    self.device.host, self.device.user, 
                    self.device.password, self.device.port
                )
                
                if success:
                    # 连接成功
                    self.consecutive_failures = 0
                    
                    # 如果是从离线恢复
                    if not self.was_online:
                        self.was_online = True
                        self.connection_restored.emit(self.device.id)
                        print(f"[DeviceMonitor] 设备 {self.device.name} 连接已恢复")
                    
                    self.device.status = "online"
                    self.device.last_seen = datetime.now()
                    
                    # 获取任务状态 (SSH 作为备用)
                    if self.device.remote_path:
                        task_status = SSHManager.get_task_status(
                            self.device.host, self.device.user,
                            self.device.password, self.device.remote_path,
                            self.device.port
                        )
                        
                        if task_status:
                            state = task_status.get("state", "idle")
                            if state == "running":
                                self.device.status = "busy"
                                self.device.current_task = task_status.get("keyword", "")
                            
                            self.device_updated.emit(self.device.id, {
                                "status": self.device.status,
                                "task": task_status
                            })
                        else:
                            self.device_updated.emit(self.device.id, {
                                "status": self.device.status,
                                "task": None
                            })
                else:
                    # 连接失败
                    self.consecutive_failures += 1
                    
                    # 只有连续失败多次才标记为离线
                    if self.consecutive_failures >= self.max_failures:
                        if self.was_online:
                            self.was_online = False
                            self.connection_lost.emit(self.device.id)
                            print(f"[DeviceMonitor] 设备 {self.device.name} 连接已丢失")
                        
                        self.device.status = "offline"
                        self.device_updated.emit(self.device.id, {
                            "status": "offline",
                            "error": message,
                            "failures": self.consecutive_failures
                        })
                    else:
                        # 短暂失败，保持之前状态
                        print(f"[DeviceMonitor] 设备 {self.device.name} 连接尝试失败 ({self.consecutive_failures}/{self.max_failures})")
                    
            except Exception as e:
                self.consecutive_failures += 1
                
                if self.consecutive_failures >= self.max_failures:
                    if self.was_online:
                        self.was_online = False
                        self.connection_lost.emit(self.device.id)
                    
                    self.device.status = "error"
                    self.device_updated.emit(self.device.id, {
                        "status": "error",
                        "error": str(e),
                        "failures": self.consecutive_failures
                    })
            
            # 等待下一次检查
            for _ in range(self.interval * 10):
                if not self.running:
                    break
                self.msleep(100)
    
    def stop(self):
        self.running = False


# ============== UI 组件 ==============

class DeviceCard(QFrame):
    """设备卡片组件 - 优化版"""
    clicked = pyqtSignal(str)  # device_id
    edit_clicked = pyqtSignal(str)
    delete_clicked = pyqtSignal(str)
    connect_clicked = pyqtSignal(str)
    
    def __init__(self, device: Device, parent=None):
        super().__init__(parent)
        self.device = device
        self.setFixedHeight(100)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setup_ui()
        self.update_status(device.status)
        
        # 添加悬停效果
        self.setStyleSheet("""
            DeviceCard {
                background-color: white;
                border: 2px solid #e5e7eb;
                border-radius: 10px;
            }
            DeviceCard:hover {
                border: 2px solid #3b82f6;
                background-color: #f8fafc;
            }
        """)
    
    def setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(15, 12, 15, 12)
        
        # 左侧状态指示器
        status_layout = QVBoxLayout()
        status_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        self.status_indicator = QLabel("●")
        self.status_indicator.setFont(QFont("Arial", 24))
        status_layout.addWidget(self.status_indicator)
        
        self.status_text = QLabel("离线")
        self.status_text.setStyleSheet("color: #9ca3af; font-size: 10px;")
        self.status_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
        status_layout.addWidget(self.status_text)
        
        layout.addLayout(status_layout)
        
        # 分隔线
        line = QFrame()
        line.setFrameShape(QFrame.Shape.VLine)
        line.setStyleSheet("background-color: #e5e7eb;")
        line.setFixedWidth(1)
        layout.addWidget(line)
        
        # 设备信息
        info_layout = QVBoxLayout()
        info_layout.setSpacing(4)
        
        # 设备名称和类型图标
        name_layout = QHBoxLayout()
        
        device_icon = QLabel("📱" if not self.device.use_usb else "🔌")
        device_icon.setFont(QFont("Arial", 16))
        name_layout.addWidget(device_icon)
        
        self.name_label = QLabel(self.device.name)
        self.name_label.setFont(QFont("Arial", 14, QFont.Weight.Bold))
        self.name_label.setStyleSheet("color: #1f2937;")
        name_layout.addWidget(self.name_label)
        name_layout.addStretch()
        
        info_layout.addLayout(name_layout)
        
        # 主机信息
        host_container = QWidget()
        host_layout = QHBoxLayout(host_container)
        host_layout.setContentsMargins(0, 0, 0, 0)
        host_layout.setSpacing(4)
        
        host_icon = QLabel("🌐" if not self.device.use_usb else "🔌")
        host_layout.addWidget(host_icon)
        
        self.host_label = QLabel(f"{self.device.host}:{self.device.port}")
        self.host_label.setStyleSheet("color: #6b7280; font-size: 12px;")
        host_layout.addWidget(self.host_label)
        host_layout.addStretch()
        
        info_layout.addWidget(host_container)
        
        # 任务状态
        self.task_label = QLabel("💤 无运行中的任务")
        self.task_label.setStyleSheet("color: #9ca3af; font-size: 11px; padding: 2px 0;")
        info_layout.addWidget(self.task_label)
        
        layout.addLayout(info_layout, stretch=1)
        
        # 右侧操作按钮
        btn_layout = QVBoxLayout()
        btn_layout.setSpacing(6)
        
        self.connect_btn = QPushButton("🔗 连接")
        self.connect_btn.setFixedWidth(70)
        self.connect_btn.setStyleSheet("""
            QPushButton {
                background-color: #3b82f6;
                color: white;
                padding: 5px;
                font-size: 11px;
                font-weight: bold;
                border-radius: 4px;
                border: none;
            }
            QPushButton:hover {
                background-color: #2563eb;
            }
        """)
        self.connect_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.connect_btn.clicked.connect(lambda: self.connect_clicked.emit(self.device.id))
        btn_layout.addWidget(self.connect_btn)
        
        # 更多操作按钮
        more_btn = QPushButton("⚙️")
        more_btn.setFixedWidth(70)
        more_btn.setStyleSheet("""
            QPushButton {
                background-color: #f3f4f6;
                color: #4b5563;
                padding: 5px;
                font-size: 11px;
                border-radius: 4px;
                border: 1px solid #d1d5db;
            }
            QPushButton:hover {
                background-color: #e5e7eb;
            }
        """)
        more_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        
        # 创建菜单
        menu = QMenu(self)
        menu.setStyleSheet("""
            QMenu {
                background-color: white;
                border: 1px solid #e5e7eb;
                border-radius: 6px;
                padding: 5px;
            }
            QMenu::item {
                padding: 8px 20px;
                border-radius: 4px;
            }
            QMenu::item:selected {
                background-color: #eff6ff;
                color: #3b82f6;
            }
        """)
        
        edit_action = QAction("✏️ 编辑", self)
        edit_action.triggered.connect(lambda: self.edit_clicked.emit(self.device.id))
        menu.addAction(edit_action)
        
        delete_action = QAction("🗑️ 删除", self)
        delete_action.triggered.connect(lambda: self.delete_clicked.emit(self.device.id))
        menu.addAction(delete_action)
        
        more_btn.setMenu(menu)
        btn_layout.addWidget(more_btn)
        
        layout.addLayout(btn_layout)
    
    def update_status(self, status: str):
        colors = {
            "online": "#10b981",    # 绿色
            "offline": "#9ca3af",   # 灰色
            "busy": "#f59e0b",      # 橙色
            "error": "#ef4444",     # 红色
        }
        color = colors.get(status, "#9ca3af")
        self.status_indicator.setStyleSheet(f"color: {color};")
        
        status_text_map = {
            "online": "在线",
            "offline": "离线",
            "busy": "运行中",
            "error": "错误",
        }
        status_text = status_text_map.get(status, status)
        self.status_text.setText(status_text)
        self.status_text.setStyleSheet(f"color: {color}; font-size: 10px; font-weight: bold;")
        
        # 更新连接按钮状态
        if status == "online":
            self.connect_btn.setText("✓ 已连")
            self.connect_btn.setStyleSheet("""
                QPushButton {
                    background-color: #10b981;
                    color: white;
                    padding: 5px;
                    font-size: 11px;
                    font-weight: bold;
                    border-radius: 4px;
                    border: none;
                }
            """)
        else:
            self.connect_btn.setText("🔗 连接")
            self.connect_btn.setStyleSheet("""
                QPushButton {
                    background-color: #3b82f6;
                    color: white;
                    padding: 5px;
                    font-size: 11px;
                    font-weight: bold;
                    border-radius: 4px;
                    border: none;
                }
                QPushButton:hover {
                    background-color: #2563eb;
                }
            """)
    
    def update_task(self, task_info: Optional[dict]):
        if task_info:
            keyword = task_info.get("keyword", "")
            state = task_info.get("state", "")
            progress = task_info.get("saved_count", 0)
            target = task_info.get("target_count", 0)
            
            if state == "running":
                self.task_label.setText(f"🔄 {keyword} ({progress}/{target})")
                self.task_label.setStyleSheet("color: #f59e0b; font-size: 11px; font-weight: bold; padding: 2px 0;")
            elif state == "completed":
                self.task_label.setText(f"✅ {keyword} 完成")
                self.task_label.setStyleSheet("color: #10b981; font-size: 11px; font-weight: bold; padding: 2px 0;")
            elif state == "failed":
                self.task_label.setText(f"❌ {keyword} 失败")
                self.task_label.setStyleSheet("color: #ef4444; font-size: 11px; font-weight: bold; padding: 2px 0;")
            else:
                self.task_label.setText(f"⏸ {keyword}")
                self.task_label.setStyleSheet("color: #6b7280; font-size: 11px; padding: 2px 0;")
        else:
            self.task_label.setText("💤 无运行中的任务")
            self.task_label.setStyleSheet("color: #9ca3af; font-size: 11px; padding: 2px 0;")
    
    def enterEvent(self, event):
        """鼠标进入"""
        self.setStyleSheet("""
            DeviceCard {
                background-color: #f8fafc;
                border: 2px solid #3b82f6;
                border-radius: 10px;
            }
        """)
        super().enterEvent(event)
    
    def leaveEvent(self, event):
        """鼠标离开"""
        self.setStyleSheet("""
            DeviceCard {
                background-color: white;
                border: 2px solid #e5e7eb;
                border-radius: 10px;
            }
            DeviceCard:hover {
                border: 2px solid #3b82f6;
                background-color: #f8fafc;
            }
        """)
        super().leaveEvent(event)
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit(self.device.id)


class TaskConfigPanel(QWidget):
    """任务配置面板 - 优化版"""
    submit_clicked = pyqtSignal(dict)
    back_clicked = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(450, 550)
        self.setup_ui()
    
    def setup_ui(self):
        # 主布局
        layout = QVBoxLayout(self)
        layout.setSpacing(15)
        layout.setContentsMargins(20, 20, 20, 20)
        
        # 标题区域
        title_container = QWidget()
        title_layout = QHBoxLayout(title_container)
        title_layout.setContentsMargins(0, 0, 0, 0)
        
        title = QLabel("📋 任务配置")
        title.setFont(QFont("Arial", 18, QFont.Weight.Bold))
        title.setStyleSheet("color: #1f2937;")
        title_layout.addWidget(title)
        title_layout.addStretch()
        
        # 重置按钮
        reset_btn = QPushButton("🔄 重置")
        reset_btn.setStyleSheet("""
            QPushButton {
                background-color: #f3f4f6;
                color: #6b7280;
                padding: 6px 12px;
                font-size: 12px;
                border-radius: 4px;
                border: 1px solid #d1d5db;
            }
            QPushButton:hover {
                background-color: #e5e7eb;
            }
        """)
        reset_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        reset_btn.clicked.connect(self.reset_form)
        title_layout.addWidget(reset_btn)
        
        layout.addWidget(title_container)
        
        # 分隔线
        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet("background-color: #e5e7eb;")
        line.setFixedHeight(1)
        layout.addWidget(line)
        
        # 滚动区域（内容多时可以滚动）
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        
        content_widget = QWidget()
        content_layout = QVBoxLayout(content_widget)
        content_layout.setSpacing(15)
        content_layout.setContentsMargins(0, 10, 0, 10)
        
        # ===== 关键词区域 =====
        keywords_group = self.create_group_box("🔍 关键词设置")
        keywords_layout = QVBoxLayout()
        
        keywords_hint = QLabel("每行输入一个关键词，支持多个关键词批量采集")
        keywords_hint.setStyleSheet("color: #6b7280; font-size: 12px;")
        keywords_layout.addWidget(keywords_hint)
        
        self.keywords_input = QTextEdit()
        self.keywords_input.setPlaceholderText("例如:\n手机\n连衣裙\n耳机")
        self.keywords_input.setMaximumHeight(100)
        self.keywords_input.setStyleSheet("""
            QTextEdit {
                border: 1px solid #d1d5db;
                border-radius: 6px;
                padding: 8px;
                font-size: 13px;
            }
            QTextEdit:focus {
                border: 2px solid #3b82f6;
            }
        """)
        keywords_layout.addWidget(self.keywords_input)
        
        # 关键词计数
        self.keywords_count_label = QLabel("已输入 0 个关键词")
        self.keywords_count_label.setStyleSheet("color: #9ca3af; font-size: 11px;")
        self.keywords_count_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        keywords_layout.addWidget(self.keywords_count_label)
        self.keywords_input.textChanged.connect(self.update_keywords_count)
        
        keywords_group.setLayout(keywords_layout)
        content_layout.addWidget(keywords_group)
        
        # ===== 采集设置区域 =====
        settings_group = self.create_group_box("⚙️ 采集设置")
        settings_layout = QFormLayout()
        settings_layout.setSpacing(12)
        
        # 采集数量
        count_container = QWidget()
        count_layout = QHBoxLayout(count_container)
        count_layout.setContentsMargins(0, 0, 0, 0)
        
        self.count_spin = QSpinBox()
        self.count_spin.setRange(1, 1000)
        self.count_spin.setValue(50)
        self.count_spin.setStyleSheet(self.get_spinbox_style())
        self.count_spin.setFixedWidth(100)
        count_layout.addWidget(self.count_spin)
        
        count_hint = QLabel("个商品（建议 20-100）")
        count_hint.setStyleSheet("color: #6b7280; font-size: 12px;")
        count_layout.addWidget(count_hint)
        count_layout.addStretch()
        
        settings_layout.addRow("采集数量:", count_container)
        
        # 排序方式
        sort_container = QWidget()
        sort_layout = QHBoxLayout(sort_container)
        sort_layout.setContentsMargins(0, 0, 0, 0)
        sort_layout.setSpacing(15)
        
        self.sort_default = self.create_sort_radio("综合排序", True)
        self.sort_sales = self.create_sort_radio("销量优先", False)
        self.sort_price_asc = self.create_sort_radio("价格低到高", False)
        self.sort_price_desc = self.create_sort_radio("价格高到低", False)
        
        sort_layout.addWidget(self.sort_default)
        sort_layout.addWidget(self.sort_sales)
        sort_layout.addWidget(self.sort_price_asc)
        sort_layout.addWidget(self.sort_price_desc)
        sort_layout.addStretch()
        
        settings_layout.addRow("排序方式:", sort_container)
        
        # 价格范围
        price_container = QWidget()
        price_layout = QHBoxLayout(price_container)
        price_layout.setContentsMargins(0, 0, 0, 0)
        price_layout.setSpacing(8)
        
        self.price_min = QSpinBox()
        self.price_min.setRange(0, 99999)
        self.price_min.setPrefix("¥")
        self.price_min.setStyleSheet(self.get_spinbox_style())
        self.price_min.setFixedWidth(90)
        
        self.price_max = QSpinBox()
        self.price_max.setRange(0, 99999)
        self.price_max.setValue(100)
        self.price_max.setPrefix("¥")
        self.price_max.setStyleSheet(self.get_spinbox_style())
        self.price_max.setFixedWidth(90)
        
        price_layout.addWidget(self.price_min)
        price_layout.addWidget(QLabel("-"))
        price_layout.addWidget(self.price_max)
        
        price_clear_btn = QPushButton("清除")
        price_clear_btn.setStyleSheet("""
            QPushButton {
                background-color: transparent;
                color: #6b7280;
                font-size: 11px;
                border: none;
            }
            QPushButton:hover {
                color: #ef4444;
            }
        """)
        price_clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        price_clear_btn.clicked.connect(self.clear_price_range)
        price_layout.addWidget(price_clear_btn)
        price_layout.addStretch()
        
        settings_layout.addRow("价格范围:", price_container)
        
        settings_group.setLayout(settings_layout)
        content_layout.addWidget(settings_group)
        
        # ===== 执行模式区域 =====
        mode_group = self.create_group_box("▶️ 执行模式")
        mode_layout = QVBoxLayout()
        mode_layout.setSpacing(10)
        
        # 连续执行选项
        self.continuous_radio = QCheckBox("连续执行多个关键词")
        self.continuous_radio.setChecked(True)
        self.continuous_radio.setStyleSheet("""
            QCheckBox {
                font-size: 13px;
                color: #374151;
            }
            QCheckBox::indicator {
                width: 18px;
                height: 18px;
            }
        """)
        mode_layout.addWidget(self.continuous_radio)
        
        # 间隔时间
        interval_container = QWidget()
        interval_layout = QHBoxLayout(interval_container)
        interval_layout.setContentsMargins(20, 0, 0, 0)
        
        interval_label = QLabel("任务间隔:")
        interval_label.setStyleSheet("color: #6b7280; font-size: 12px;")
        interval_layout.addWidget(interval_label)
        
        self.interval_spin = QSpinBox()
        self.interval_spin.setRange(1, 3600)
        self.interval_spin.setValue(5)
        self.interval_spin.setStyleSheet(self.get_spinbox_style())
        self.interval_spin.setFixedWidth(70)
        interval_layout.addWidget(self.interval_spin)
        
        interval_unit = QLabel("秒")
        interval_unit.setStyleSheet("color: #6b7280; font-size: 12px;")
        interval_layout.addWidget(interval_unit)
        interval_layout.addStretch()
        
        mode_layout.addWidget(interval_container)
        
        mode_hint = QLabel("💡 提示：每个任务完成后会自动重启拼多多并导出产物")
        mode_hint.setStyleSheet("color: #3b82f6; font-size: 11px; padding: 5px; background: #eff6ff; border-radius: 4px;")
        mode_layout.addWidget(mode_hint)
        
        mode_group.setLayout(mode_layout)
        content_layout.addWidget(mode_group)
        
        content_layout.addStretch()
        scroll.setWidget(content_widget)
        layout.addWidget(scroll)
        
        # ===== 底部按钮区域 =====
        btn_container = QWidget()
        btn_layout = QHBoxLayout(btn_container)
        btn_layout.setContentsMargins(0, 10, 0, 0)
        
        # 返回按钮
        self.back_btn = QPushButton("← 返回")
        self.back_btn.setStyleSheet("""
            QPushButton {
                background-color: #f3f4f6;
                color: #4b5563;
                padding: 10px 20px;
                font-size: 14px;
                font-weight: bold;
                border-radius: 6px;
                border: 1px solid #d1d5db;
            }
            QPushButton:hover {
                background-color: #e5e7eb;
            }
        """)
        self.back_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.back_btn.clicked.connect(self.back_clicked.emit)
        btn_layout.addWidget(self.back_btn)
        
        btn_layout.addStretch()
        
        # 提交按钮
        self.submit_btn = QPushButton("🚀 开始采集")
        self.submit_btn.setStyleSheet("""
            QPushButton {
                background-color: #10b981;
                color: white;
                padding: 12px 30px;
                font-size: 15px;
                font-weight: bold;
                border-radius: 6px;
                border: none;
            }
            QPushButton:hover {
                background-color: #059669;
            }
            QPushButton:pressed {
                background-color: #047857;
            }
            QPushButton:disabled {
                background-color: #9ca3af;
            }
        """)
        self.submit_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.submit_btn.clicked.connect(self.on_submit)
        btn_layout.addWidget(self.submit_btn)
        
        layout.addWidget(btn_container)
    
    def create_group_box(self, title):
        """创建统一风格的组框"""
        group = QGroupBox(title)
        group.setStyleSheet("""
            QGroupBox {
                font-weight: bold;
                font-size: 13px;
                color: #374151;
                border: 1px solid #e5e7eb;
                border-radius: 8px;
                margin-top: 10px;
                padding-top: 10px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
        """)
        return group
    
    def create_sort_radio(self, text, checked):
        """创建排序单选按钮"""
        radio = QRadioButton(text)
        radio.setChecked(checked)
        radio.setStyleSheet("""
            QRadioButton {
                font-size: 12px;
                color: #4b5563;
                padding: 4px 8px;
            }
            QRadioButton::indicator {
                width: 16px;
                height: 16px;
            }
            QRadioButton:checked {
                color: #1f2937;
                font-weight: bold;
            }
        """)
        radio.setCursor(Qt.CursorShape.PointingHandCursor)
        return radio
    
    def get_spinbox_style(self):
        """获取数字输入框样式"""
        return """
            QSpinBox {
                border: 1px solid #d1d5db;
                border-radius: 4px;
                padding: 4px;
                font-size: 13px;
            }
            QSpinBox:focus {
                border: 2px solid #3b82f6;
            }
            QSpinBox::up-button, QSpinBox::down-button {
                width: 16px;
            }
        """
    
    def update_keywords_count(self):
        """更新关键词计数"""
        text = self.keywords_input.toPlainText().strip()
        count = len([k for k in text.split("\n") if k.strip()])
        self.keywords_count_label.setText(f"已输入 {count} 个关键词")
        if count > 0:
            self.keywords_count_label.setStyleSheet("color: #10b981; font-size: 11px; font-weight: bold;")
        else:
            self.keywords_count_label.setStyleSheet("color: #9ca3af; font-size: 11px;")
    
    def clear_price_range(self):
        """清除价格范围"""
        self.price_min.setValue(0)
        self.price_max.setValue(0)
    
    def reset_form(self):
        """重置表单"""
        self.keywords_input.clear()
        self.count_spin.setValue(50)
        self.sort_default.setChecked(True)
        self.price_min.setValue(0)
        self.price_max.setValue(100)
        self.continuous_radio.setChecked(True)
        self.interval_spin.setValue(5)
        self.update_keywords_count()
    
    def get_sort_by(self):
        """获取当前选中的排序方式"""
        if self.sort_sales.isChecked():
            return "销量优先"
        elif self.sort_price_asc.isChecked():
            return "价格低到高"
        elif self.sort_price_desc.isChecked():
            return "价格高到低"
        return "综合排序"
    
    def on_submit(self):
        keywords = [k.strip() for k in self.keywords_input.toPlainText().strip().split("\n") if k.strip()]
        if not keywords:
            QMessageBox.warning(self, "⚠️ 提示", "请输入至少一个关键词")
            return
        
        config = {
            "keywords": keywords,
            "count": self.count_spin.value(),
            "sort_by": self.get_sort_by(),
            "price_min": self.price_min.value() if self.price_min.value() > 0 else None,
            "price_max": self.price_max.value() if self.price_max.value() > 0 else None,
            "continuous": self.continuous_radio.isChecked(),
            "interval": self.interval_spin.value(),
        }
        self.submit_clicked.emit(config)


class DeviceDetailPanel(QWidget):
    """设备详情面板"""
    refresh_clicked = pyqtSignal()
    export_clicked = pyqtSignal()
    stop_clicked = pyqtSignal()
    new_task_clicked = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.current_device: Optional[Device] = None
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        
        # 设备信息
        self.info_group = QGroupBox("设备信息")
        info_layout = QFormLayout(self.info_group)
        
        self.name_label = QLabel("-")
        info_layout.addRow("名称:", self.name_label)
        
        self.host_label = QLabel("-")
        info_layout.addRow("地址:", self.host_label)
        
        self.status_label = QLabel("-")
        info_layout.addRow("状态:", self.status_label)
        
        self.path_label = QLabel("-")
        self.path_label.setWordWrap(True)
        info_layout.addRow("远程路径:", self.path_label)
        
        layout.addWidget(self.info_group)
        
        # 当前任务
        self.task_group = QGroupBox("当前任务")
        task_layout = QVBoxLayout(self.task_group)
        
        self.task_info = QLabel("无运行中的任务")
        task_layout.addWidget(self.task_info)
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        task_layout.addWidget(self.progress_bar)
        
        # 任务操作按钮
        task_btn_layout = QHBoxLayout()
        
        self.refresh_btn = QPushButton("🔄 刷新状态")
        self.refresh_btn.clicked.connect(self.refresh_clicked.emit)
        task_btn_layout.addWidget(self.refresh_btn)
        
        self.stop_btn = QPushButton("⏹ 停止任务")
        self.stop_btn.clicked.connect(self.stop_clicked.emit)
        task_btn_layout.addWidget(self.stop_btn)
        
        self.export_btn = QPushButton("📥 导出产物")
        self.export_btn.clicked.connect(self.export_clicked.emit)
        task_btn_layout.addWidget(self.export_btn)
        
        task_layout.addLayout(task_btn_layout)
        layout.addWidget(self.task_group)
        
        # 实时采集商品列表
        self.realtime_group = QGroupBox("实时采集")
        realtime_layout = QVBoxLayout(self.realtime_group)
        
        self.realtime_status = QLabel("等待采集...")
        self.realtime_status.setStyleSheet("color: #666; font-size: 12px;")
        realtime_layout.addWidget(self.realtime_status)
        
        self.goods_list = QListWidget()
        self.goods_list.setMaximumHeight(200)
        realtime_layout.addWidget(self.goods_list)
        
        layout.addWidget(self.realtime_group)
        
        # 发布任务按钮
        self.new_task_btn = QPushButton("🚀 发布新任务")
        self.new_task_btn.setStyleSheet("""
            QPushButton {
                background-color: #3b82f6;
                color: white;
                padding: 15px;
                font-size: 16px;
                font-weight: bold;
                border-radius: 8px;
                margin-top: 10px;
            }
            QPushButton:hover {
                background-color: #2563eb;
            }
        """)
        self.new_task_btn.clicked.connect(self.new_task_clicked.emit)
        layout.addWidget(self.new_task_btn)
        
        # 提示信息
        hint = QLabel('💡 点击"发布新任务"按钮配置并提交采集任务')
        hint.setStyleSheet("color: #666; font-size: 12px; margin-top: 5px;")
        hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(hint)
        
        # 任务历史
        self.history_group = QGroupBox("任务历史")
        history_layout = QVBoxLayout(self.history_group)
        
        self.history_table = QTableWidget()
        self.history_table.setColumnCount(5)
        self.history_table.setHorizontalHeaderLabels(["任务ID", "关键词", "状态", "保存数", "时间"])
        self.history_table.horizontalHeader().setStretchLastSection(True)
        history_layout.addWidget(self.history_table)
        
        layout.addWidget(self.history_group)
    
    def set_device(self, device: Device):
        self.current_device = device
        self.name_label.setText(device.name)
        self.host_label.setText(f"{device.host}:{device.port}")
        self.status_label.setText(device.status)
        self.path_label.setText(device.remote_path or "未配置")
    
    def update_task_status(self, task_info: Optional[dict]):
        if task_info:
            keyword = task_info.get("keyword", "")
            state = task_info.get("state", "")
            saved = task_info.get("saved_count", 0)
            target = task_info.get("target_count", 0)
            attempted = task_info.get("attempted_count", 0)
            
            self.task_info.setText(f"关键词: {keyword}\n状态: {state}\n进度: {saved}/{target} (尝试: {attempted})")
            
            if target > 0:
                progress = int((saved / target) * 100)
                self.progress_bar.setValue(progress)
            else:
                self.progress_bar.setValue(0)
            
            # 更新实时状态标签
            if state == "running":
                self.realtime_status.setText(f"🟢 正在采集: {keyword} ({saved}/{target})")
            elif state == "completed":
                self.realtime_status.setText(f"✅ 采集完成: {saved} 个商品")
            elif state == "failed":
                self.realtime_status.setText(f"❌ 采集失败")
            else:
                self.realtime_status.setText("⏸ 等待中...")
        else:
            self.task_info.setText("无运行中的任务")
            self.progress_bar.setValue(0)
            self.realtime_status.setText("等待采集...")
    
    def update_realtime_goods(self, goods_list: List[dict]):
        """更新实时商品列表"""
        self.goods_list.clear()
        for goods in goods_list:
            goods_name = goods.get("goodsName", goods.get("goods_name", "未知商品"))
            goods_id = goods.get("goodsId", goods.get("goods_id", ""))
            price = goods.get("price", "")
            
            display_text = f"{goods_name}"
            if price:
                display_text += f" - ¥{price}"
            
            item = QListWidgetItem(display_text)
            item.setToolTip(f"ID: {goods_id}")
            self.goods_list.addItem(item)
        
        # 滚动到最新
        if self.goods_list.count() > 0:
            self.goods_list.scrollToBottom()


class AddDeviceDialog(QDialog):
    """添加设备对话框"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("添加设备")
        self.setMinimumWidth(400)
        self.setup_ui()
    
    def setup_ui(self):
        layout = QFormLayout(self)
        
        # 连接方式选择
        self.connection_group = QWidget()
        connection_layout = QHBoxLayout(self.connection_group)
        connection_layout.setContentsMargins(0, 0, 0, 0)
        
        self.wifi_radio = QCheckBox("WiFi")
        self.wifi_radio.setChecked(True)
        self.wifi_radio.stateChanged.connect(self.on_connection_changed)
        connection_layout.addWidget(self.wifi_radio)
        
        self.usb_radio = QCheckBox("USB数据线")
        self.usb_radio.stateChanged.connect(self.on_connection_changed)
        connection_layout.addWidget(self.usb_radio)
        
        connection_layout.addStretch()
        layout.addRow("连接方式:", self.connection_group)
        
        self.name_input = QLineEdit()
        self.name_input.setPlaceholderText("例如: 我的iPhone")
        layout.addRow("设备名称:", self.name_input)
        
        self.host_input = QLineEdit()
        self.host_input.setPlaceholderText("例如: 192.168.0.34")
        layout.addRow("IP地址:", self.host_input)
        
        self.user_input = QLineEdit()
        self.user_input.setText("mobile")
        layout.addRow("用户名:", self.user_input)
        
        self.password_input = QLineEdit()
        self.password_input.setEchoMode(QLineEdit.EchoMode.Password)
        self.password_input.setPlaceholderText("SSH密码")
        layout.addRow("密码:", self.password_input)
        
        self.port_input = QSpinBox()
        self.port_input.setRange(1, 65535)
        self.port_input.setValue(22)
        layout.addRow("端口:", self.port_input)
        
        # USB 连接提示
        self.usb_hint = QLabel("💡 USB连接需要安装 libimobiledevice 工具")
        self.usb_hint.setStyleSheet("color: #666; font-size: 12px;")
        self.usb_hint.hide()
        layout.addRow(self.usb_hint)
        
        # 按钮
        btn_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        btn_box.accepted.connect(self.accept)
        btn_box.rejected.connect(self.reject)
        layout.addRow(btn_box)
    
    def on_connection_changed(self):
        if self.usb_radio.isChecked():
            self.wifi_radio.setChecked(False)
            self.host_input.setText("usb")
            self.host_input.setEnabled(False)
            self.port_input.setValue(2222)
            self.usb_hint.show()
        else:
            self.wifi_radio.setChecked(True)
            self.host_input.setEnabled(True)
            self.host_input.setText("")
            self.port_input.setValue(22)
            self.usb_hint.hide()
    
    def get_device_data(self) -> dict:
        return {
            "name": self.name_input.text(),
            "host": self.host_input.text(),
            "user": self.user_input.text(),
            "password": self.password_input.text(),
            "port": self.port_input.value(),
            "use_usb": self.usb_radio.isChecked(),
        }


class DeviceDiscoveryDialog(QDialog):
    """设备搜索对话框 - 手动搜索版"""
    device_selected = pyqtSignal(dict)  # 选中的设备信息
    
    def __init__(self, parent=None, existing_devices=None):
        super().__init__(parent)
        self.setWindowTitle("🔍 搜索设备")
        self.setMinimumSize(600, 500)
        self.found_devices: List[dict] = []
        self.existing_devices = existing_devices or []
        self.setup_ui()
        
        # 不自动扫描，等待用户点击
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(15)
        layout.setContentsMargins(20, 20, 20, 20)
        
        # 标题
        title = QLabel("🔍 搜索设备")
        title.setFont(QFont("Arial", 18, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet("color: #1f2937;")
        layout.addWidget(title)
        
        # 说明文字
        hint = QLabel("搜索局域网中的 iOS 设备，已添加的设备将不会显示")
        hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hint.setStyleSheet("color: #6b7280; font-size: 12px;")
        layout.addWidget(hint)
        
        # 分隔线
        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet("background-color: #e5e7eb;")
        line.setFixedHeight(1)
        layout.addWidget(line)
        
        # USB 设备区域
        usb_group = QGroupBox("🔌 USB 设备")
        usb_group.setStyleSheet("""
            QGroupBox {
                font-weight: bold;
                font-size: 13px;
                color: #374151;
                border: 1px solid #e5e7eb;
                border-radius: 8px;
                margin-top: 10px;
                padding-top: 10px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
        """)
        usb_layout = QVBoxLayout(usb_group)
        
        self.usb_status = QLabel('点击"刷新"按钮检查 USB 设备')
        self.usb_status.setStyleSheet("color: #6b7280; font-size: 12px;")
        usb_layout.addWidget(self.usb_status)
        
        self.usb_list = QListWidget()
        self.usb_list.setStyleSheet("""
            QListWidget {
                border: 1px solid #e5e7eb;
                border-radius: 6px;
                padding: 5px;
            }
            QListWidget::item {
                padding: 8px;
                border-radius: 4px;
            }
            QListWidget::item:selected {
                background-color: #eff6ff;
                color: #3b82f6;
            }
        """)
        self.usb_list.itemClicked.connect(self.on_usb_selected)
        usb_layout.addWidget(self.usb_list)
        
        self.refresh_usb_btn = QPushButton("🔄 刷新 USB 设备")
        self.refresh_usb_btn.setStyleSheet("""
            QPushButton {
                background-color: #f3f4f6;
                color: #374151;
                padding: 8px;
                font-weight: bold;
                border-radius: 6px;
                border: 1px solid #d1d5db;
            }
            QPushButton:hover {
                background-color: #e5e7eb;
            }
        """)
        self.refresh_usb_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.refresh_usb_btn.clicked.connect(self.check_usb_devices)
        usb_layout.addWidget(self.refresh_usb_btn)
        
        layout.addWidget(usb_group)
        
        # WiFi 设备扫描区域
        wifi_group = QGroupBox("🌐 WiFi 设备")
        wifi_group.setStyleSheet("""
            QGroupBox {
                font-weight: bold;
                font-size: 13px;
                color: #374151;
                border: 1px solid #e5e7eb;
                border-radius: 8px;
                margin-top: 10px;
                padding-top: 10px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
        """)
        wifi_layout = QVBoxLayout(wifi_group)
        
        scan_btn_layout = QHBoxLayout()
        self.scan_wifi_btn = QPushButton("🔍 开始扫描")
        self.scan_wifi_btn.setStyleSheet("""
            QPushButton {
                background-color: #3b82f6;
                color: white;
                padding: 10px 20px;
                font-weight: bold;
                border-radius: 6px;
                border: none;
            }
            QPushButton:hover {
                background-color: #2563eb;
            }
            QPushButton:disabled {
                background-color: #9ca3af;
            }
        """)
        self.scan_wifi_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.scan_wifi_btn.clicked.connect(self.start_wifi_scan)
        scan_btn_layout.addWidget(self.scan_wifi_btn)
        
        self.scan_progress = QProgressBar()
        self.scan_progress.setRange(0, 100)
        self.scan_progress.setValue(0)
        self.scan_progress.setTextVisible(True)
        self.scan_progress.hide()
        scan_btn_layout.addWidget(self.scan_progress)
        
        wifi_layout.addLayout(scan_btn_layout)
        
        self.wifi_status = QLabel('点击"开始扫描"按钮搜索 WiFi 设备（约需 10-30 秒）')
        self.wifi_status.setStyleSheet("color: #6b7280; font-size: 12px;")
        wifi_layout.addWidget(self.wifi_status)
        
        self.wifi_list = QListWidget()
        self.wifi_list.setStyleSheet("""
            QListWidget {
                border: 1px solid #e5e7eb;
                border-radius: 6px;
                padding: 5px;
            }
            QListWidget::item {
                padding: 8px;
                border-radius: 4px;
            }
            QListWidget::item:selected {
                background-color: #eff6ff;
                color: #3b82f6;
            }
        """)
        self.wifi_list.itemClicked.connect(self.on_wifi_selected)
        wifi_layout.addWidget(self.wifi_list)
        
        layout.addWidget(wifi_group)
        
        # 选中的设备信息
        self.selected_info = QLabel("请选择一个设备")
        self.selected_info.setStyleSheet("color: #666; padding: 10px; background: #f3f4f6; border-radius: 5px;")
        layout.addWidget(self.selected_info)
        
        # 按钮
        btn_layout = QHBoxLayout()
        
        self.add_btn = QPushButton("➕ 添加选中设备")
        self.add_btn.setEnabled(False)
        self.add_btn.setStyleSheet("""
            QPushButton {
                background-color: #3b82f6;
                color: white;
                padding: 10px 20px;
                font-weight: bold;
            }
            QPushButton:disabled {
                background-color: #9ca3af;
            }
        """)
        self.add_btn.clicked.connect(self.add_selected_device)
        btn_layout.addWidget(self.add_btn)
        
        btn_layout.addStretch()
        
        cancel_btn = QPushButton("取消")
        cancel_btn.clicked.connect(self.reject)
        btn_layout.addWidget(cancel_btn)
        
        layout.addLayout(btn_layout)
    
    def check_usb_devices(self):
        """检查 USB 连接的设备"""
        self.usb_list.clear()
        self.usb_status.setText("正在检查 USB 设备...")
        
        devices = USBManager.get_device_info()
        
        # 过滤掉已存在的设备
        new_devices = []
        for device in devices:
            is_existing = False
            for existing in self.existing_devices:
                if existing.get("name") == device["name"]:
                    is_existing = True
                    break
            if not is_existing:
                new_devices.append(device)
        
        if new_devices:
            self.usb_status.setText(f"✅ 发现 {len(new_devices)} 个新 USB 设备")
            for device in new_devices:
                item_text = f"📱 {device['name']} (UDID: {device['udid'][:16]}...)"
                item = QListWidgetItem(item_text)
                item.setData(Qt.ItemDataRole.UserRole, device)
                self.usb_list.addItem(item)
        elif devices:
            self.usb_status.setText("ℹ️ USB 设备已在列表中")
        else:
            self.usb_status.setText("❌ 未发现 USB 设备\n请确保设备已通过 USB 连接并信任此电脑")
    
    def start_wifi_scan(self):
        """开始 WiFi 扫描"""
        self.wifi_list.clear()
        self.scan_wifi_btn.setEnabled(False)
        self.scan_progress.show()
        self.scan_progress.setValue(0)
        self.wifi_status.setText("正在扫描局域网...")
        
        # 在后台线程中扫描
        from PyQt6.QtCore import QThread, pyqtSignal
        
        class ScanThread(QThread):
            progress = pyqtSignal(int, int)
            finished = pyqtSignal(list)
            
            def run(self):
                def progress_callback(current, total):
                    self.progress.emit(current, total)
                
                devices = NetworkScanner.scan_network(progress_callback=progress_callback)
                self.finished.emit(devices)
        
        self.scan_thread = ScanThread()
        self.scan_thread.progress.connect(self.on_scan_progress)
        self.scan_thread.finished.connect(self.on_scan_finished)
        self.scan_thread.start()
    
    def on_scan_progress(self, current, total):
        """扫描进度更新"""
        progress = int((current / total) * 100)
        self.scan_progress.setValue(progress)
        self.wifi_status.setText(f"正在扫描... {current}/{total}")
    
    def on_scan_finished(self, devices):
        """扫描完成"""
        self.scan_wifi_btn.setEnabled(True)
        self.scan_progress.hide()
        
        # 过滤掉已存在的设备（根据 IP 和端口判断）
        new_devices = []
        for device in devices:
            is_existing = False
            for existing in self.existing_devices:
                if existing.get("host") == device["ip"] and existing.get("port") == device["port"]:
                    is_existing = True
                    break
            if not is_existing:
                new_devices.append(device)
        
        self.found_devices = new_devices
        
        if new_devices:
            self.wifi_status.setText(f"✅ 发现 {len(new_devices)} 个新设备")
            for device in new_devices:
                item_text = f"📡 {device['ip']}:{device['port']} ({device.get('banner', 'SSH Service')[:30]}...)"
                item = QListWidgetItem(item_text)
                item.setData(Qt.ItemDataRole.UserRole, device)
                self.wifi_list.addItem(item)
        elif devices:
            self.wifi_status.setText("ℹ️ 发现的设备已在列表中")
        else:
            self.wifi_status.setText("❌ 未发现 WiFi 设备\n请确保设备和电脑在同一局域网")
    
    def on_usb_selected(self, item):
        """选择 USB 设备"""
        device = item.data(Qt.ItemDataRole.UserRole)
        self.selected_device = {
            "type": "usb",
            "name": device["name"],
            "udid": device["udid"],
            "connection": "USB"
        }
        self.selected_info.setText(f"已选择 USB 设备: {device['name']}")
        self.add_btn.setEnabled(True)
    
    def on_wifi_selected(self, item):
        """选择 WiFi 设备"""
        device = item.data(Qt.ItemDataRole.UserRole)
        self.selected_device = {
            "type": "wifi",
            "ip": device["ip"],
            "port": device["port"],
            "connection": "WiFi"
        }
        self.selected_info.setText(f"已选择 WiFi 设备: {device['ip']}:{device['port']}")
        self.add_btn.setEnabled(True)
    
    def add_selected_device(self):
        """添加选中的设备"""
        if hasattr(self, 'selected_device'):
            self.device_selected.emit(self.selected_device)
            self.accept()


# ============== 主窗口 ==============

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("PDD Device Manager - iOS设备管理工具")
        self.setMinimumSize(1200, 800)
        
        self.devices: Dict[str, Device] = {}
        self.monitors: Dict[str, DeviceMonitorThread] = {}
        self.socket_clients: Dict[str, SocketClientThread] = {}  # Socket 实时通信客户端
        self.current_device_id: Optional[str] = None
        
        self.settings = QSettings("PDD", "DeviceManager")
        self.load_devices()
        
        self.setup_ui()
        self.setup_menu()
        self.setup_statusbar()
        self.setup_tray()
        
        # 启动定时器刷新UI
        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self.refresh_devices_status)
        self.refresh_timer.start(1000)  # 每秒刷新一次
        
        # 启动 USB 设备自动检测
        self.setup_usb_auto_detection()
        
        # 启动时检查是否有 USB 设备
        self.check_usb_on_startup()
    
    def setup_usb_auto_detection(self):
        """设置 USB 设备自动检测"""
        USBManager.start_monitoring(self.on_usb_device_connected)
    
    def check_usb_on_startup(self):
        """启动时检查 USB 设备 - 不再自动提示添加"""
        # 移除了自动提示，用户需要手动点击"搜索"按钮添加设备
        pass
    
    def on_usb_device_connected(self, device_info: dict):
        """USB 设备连接回调 - 仅在状态栏显示，不弹窗提示"""
        # 检查是否已存在
        for device in self.devices.values():
            if device.use_usb and device.name == device_info.get("name"):
                return  # 已存在
        
        # 仅在状态栏显示通知，不弹窗打扰用户
        self.statusbar.showMessage(f"🔌 检测到 USB 设备: {device_info.get('name')}，点击搜索按钮添加")
    
    def setup_ui(self):
        # 中央部件
        central = QWidget()
        self.setCentralWidget(central)
        
        # 主布局
        main_layout = QHBoxLayout(central)
        main_layout.setSpacing(0)
        main_layout.setContentsMargins(0, 0, 0, 0)
        
        # 左侧边栏 - 设备列表
        sidebar = QWidget()
        sidebar.setFixedWidth(350)
        sidebar.setStyleSheet("background-color: #f3f4f6;")
        sidebar_layout = QVBoxLayout(sidebar)
        sidebar_layout.setSpacing(10)
        sidebar_layout.setContentsMargins(15, 15, 15, 15)
        
        # 标题
        title = QLabel("📱 设备列表")
        title.setFont(QFont("Arial", 16, QFont.Weight.Bold))
        sidebar_layout.addWidget(title)
        
        # 按钮区域
        btn_container = QWidget()
        btn_layout = QHBoxLayout(btn_container)
        btn_layout.setContentsMargins(0, 0, 0, 0)
        btn_layout.setSpacing(8)
        
        # 添加设备按钮
        add_btn = QPushButton("+ 添加")
        add_btn.setStyleSheet("""
            QPushButton {
                background-color: #10b981;
                color: white;
                padding: 8px 12px;
                font-weight: bold;
                border-radius: 6px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #059669;
            }
        """)
        add_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        add_btn.clicked.connect(self.show_add_device_dialog)
        btn_layout.addWidget(add_btn)
        
        # 搜索设备按钮
        search_btn = QPushButton("🔍 搜索")
        search_btn.setStyleSheet("""
            QPushButton {
                background-color: #3b82f6;
                color: white;
                padding: 8px 12px;
                font-weight: bold;
                border-radius: 6px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #2563eb;
            }
        """)
        search_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        search_btn.clicked.connect(self.show_device_discovery)
        btn_layout.addWidget(search_btn)
        
        sidebar_layout.addWidget(btn_container)
        
        # 设备列表容器
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        
        self.device_list_widget = QWidget()
        self.device_list_layout = QVBoxLayout(self.device_list_widget)
        self.device_list_layout.setSpacing(10)
        self.device_list_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        
        scroll.setWidget(self.device_list_widget)
        sidebar_layout.addWidget(scroll)
        
        main_layout.addWidget(sidebar)
        
        # 右侧内容区
        self.content_stack = QStackedWidget()
        
        # 欢迎页面
        welcome = QWidget()
        welcome_layout = QVBoxLayout(welcome)
        welcome_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        welcome_text = QLabel("👋 欢迎使用 PDD Device Manager")
        welcome_text.setFont(QFont("Arial", 24, QFont.Weight.Bold))
        welcome_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
        welcome_layout.addWidget(welcome_text)
        
        welcome_sub = QLabel("请从左侧选择或添加设备")
        welcome_sub.setStyleSheet("color: gray; margin-top: 10px;")
        welcome_sub.setAlignment(Qt.AlignmentFlag.AlignCenter)
        welcome_layout.addWidget(welcome_sub)
        
        self.content_stack.addWidget(welcome)
        
        # 设备详情页面
        self.detail_panel = DeviceDetailPanel()
        self.detail_panel.refresh_clicked.connect(self.refresh_current_device)
        self.detail_panel.export_clicked.connect(self.export_artifacts)
        self.detail_panel.stop_clicked.connect(self.stop_current_task)
        self.detail_panel.new_task_clicked.connect(self.show_task_config)
        self.content_stack.addWidget(self.detail_panel)
        
        # 任务配置页面
        self.task_config = TaskConfigPanel()
        self.task_config.submit_clicked.connect(self.submit_task)
        self.task_config.back_clicked.connect(self.show_device_detail)
        self.content_stack.addWidget(self.task_config)
        
        main_layout.addWidget(self.content_stack, stretch=1)
        
        # 刷新设备列表
        self.refresh_device_list()
    
    def setup_menu(self):
        menubar = self.menuBar()
        
        # 文件菜单
        file_menu = menubar.addMenu("文件")
        
        # 自动发现设备
        discover_action = QAction("🔍 自动发现设备", self)
        discover_action.setShortcut("Ctrl+D")
        discover_action.triggered.connect(self.show_device_discovery)
        file_menu.addAction(discover_action)
        
        add_action = QAction("➕ 手动添加设备", self)
        add_action.setShortcut("Ctrl+N")
        add_action.triggered.connect(self.show_add_device_dialog)
        file_menu.addAction(add_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("退出", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # 工具菜单
        tools_menu = menubar.addMenu("工具")
        
        refresh_action = QAction("刷新所有设备", self)
        refresh_action.setShortcut("F5")
        refresh_action.triggered.connect(self.refresh_all_devices)
        tools_menu.addAction(refresh_action)
        
        export_all_action = QAction("导出所有产物", self)
        export_all_action.triggered.connect(self.export_all_artifacts)
        tools_menu.addAction(export_all_action)
        
        # 帮助菜单
        help_menu = menubar.addMenu("帮助")
        
        about_action = QAction("关于", self)
        about_action.triggered.connect(self.show_about)
        help_menu.addAction(about_action)
    
    def setup_statusbar(self):
        self.statusbar = QStatusBar()
        self.setStatusBar(self.statusbar)
        self.statusbar.showMessage("就绪")
    
    def setup_tray(self):
        if QSystemTrayIcon.isSystemTrayAvailable():
            self.tray_icon = QSystemTrayIcon(self)
            self.tray_icon.setToolTip("PDD Device Manager")
            
            tray_menu = QMenu()
            show_action = QAction("显示", self)
            show_action.triggered.connect(self.show)
            tray_menu.addAction(show_action)
            
            tray_menu.addSeparator()
            
            quit_action = QAction("退出", self)
            quit_action.triggered.connect(self.close)
            tray_menu.addAction(quit_action)
            
            self.tray_icon.setContextMenu(tray_menu)
            self.tray_icon.activated.connect(self.tray_activated)
    
    def tray_activated(self, reason):
        if reason == QSystemTrayIcon.ActivationReason.DoubleClick:
            self.show()
    
    def load_devices(self):
        """从设置加载设备列表"""
        devices_data = self.settings.value("devices", [])
        if devices_data:
            for data in devices_data:
                device = Device.from_dict(data)
                self.devices[device.id] = device
    
    def save_devices(self):
        """保存设备列表到设置"""
        devices_data = [d.to_dict() for d in self.devices.values()]
        self.settings.setValue("devices", devices_data)
    
    def refresh_device_list(self):
        """刷新设备列表UI"""
        # 清空现有列表
        while self.device_list_layout.count():
            item = self.device_list_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        
        # 添加设备卡片
        for device in self.devices.values():
            card = DeviceCard(device)
            card.clicked.connect(self.on_device_selected)
            card.edit_clicked.connect(self.edit_device)
            card.delete_clicked.connect(self.delete_device)
            card.connect_clicked.connect(self.test_device_connection)
            self.device_list_layout.addWidget(card)
            
            # 启动监控线程 (SSH 备用)
            if device.id not in self.monitors:
                monitor = DeviceMonitorThread(device)
                monitor.device_updated.connect(self.on_device_updated)
                monitor.connection_lost.connect(self.on_connection_lost)
                monitor.connection_restored.connect(self.on_connection_restored)
                monitor.start()
                self.monitors[device.id] = monitor
            
            # 启动 Socket 实时通信客户端
            if device.id not in self.socket_clients:
                socket_client = SocketClientThread(device, socket_port=9999)
                socket_client.connected.connect(self.on_socket_connected)
                socket_client.disconnected.connect(self.on_socket_disconnected)
                socket_client.goods_captured.connect(self.on_socket_goods_captured)
                socket_client.task_started.connect(self.on_socket_task_started)
                socket_client.task_finished.connect(self.on_socket_task_finished)
                socket_client.progress_updated.connect(self.on_socket_progress_updated)
                socket_client.start()
                self.socket_clients[device.id] = socket_client
        
        self.device_list_layout.addStretch()
    
    def refresh_devices_status(self):
        """定时刷新设备状态显示"""
        for i in range(self.device_list_layout.count()):
            widget = self.device_list_layout.itemAt(i).widget()
            if isinstance(widget, DeviceCard):
                widget.update_status(widget.device.status)
    
    def on_device_updated(self, device_id: str, info: dict):
        """设备状态更新回调"""
        if device_id in self.devices:
            device = self.devices[device_id]
            device.status = info.get("status", device.status)
            
            # 更新当前显示的详情
            if self.current_device_id == device_id:
                self.detail_panel.update_task_status(info.get("task"))
    
    def on_connection_lost(self, device_id: str):
        """设备连接丢失"""
        if device_id in self.devices:
            device = self.devices[device_id]
            self.statusbar.showMessage(f"⚠️ 与设备 {device.name} 的连接已丢失，正在尝试重连...")
            
            # 显示通知
            if self.current_device_id == device_id:
                self.detail_panel.realtime_status.setText("⚠️ 连接已丢失，正在重连...")
    
    def on_connection_restored(self, device_id: str):
        """设备连接恢复"""
        if device_id in self.devices:
            device = self.devices[device_id]
            self.statusbar.showMessage(f"✅ 与设备 {device.name} 的连接已恢复")
            
            # 显示通知
            if self.current_device_id == device_id:
                self.detail_panel.realtime_status.setText("✅ 连接已恢复")
    
    def on_realtime_goods_updated(self, device_id: str, goods_list: list):
        """实时商品更新回调 (SSH 备用)"""
        if device_id == self.current_device_id:
            self.detail_panel.update_realtime_goods(goods_list)
    
    # ============== Socket 实时通信回调 ==============
    
    def on_socket_connected(self, device_id: str):
        """Socket 连接成功"""
        print(f"[MainWindow] Socket 已连接到设备 {device_id}")
        self.statusbar.showMessage("✅ 实时通信已连接")
    
    def on_socket_disconnected(self, device_id: str):
        """Socket 断开连接"""
        print(f"[MainWindow] Socket 与设备 {device_id} 断开")
        self.statusbar.showMessage("⚠️ 实时通信已断开，使用 SSH 备用模式")
    
    def on_socket_goods_captured(self, device_id: str, message: dict):
        """收到商品采集通知 - 即时推送"""
        if device_id != self.current_device_id:
            return
        
        goods_data = message.get("goods", {})
        saved_count = message.get("saved_count", 0)
        target_count = message.get("target_count", 0)
        progress = message.get("progress", 0)
        
        # 更新商品列表
        goods_name = goods_data.get("goodsName", goods_data.get("goods_name", "未知商品"))
        price = goods_data.get("price", "")
        
        # 添加到列表顶部
        display_text = f"{goods_name}"
        if price:
            display_text += f" - ¥{price}"
        
        from PyQt6.QtWidgets import QListWidgetItem
        item = QListWidgetItem(display_text)
        item.setToolTip(f"ID: {goods_data.get('goodsId', goods_data.get('goods_id', ''))}")
        self.detail_panel.goods_list.insertItem(0, item)  # 插入到顶部
        
        # 限制列表数量
        while self.detail_panel.goods_list.count() > 50:
            self.detail_panel.goods_list.takeItem(self.detail_panel.goods_list.count() - 1)
        
        # 更新状态
        self.detail_panel.realtime_status.setText(
            f"🟢 正在采集 ({saved_count}/{target_count}) - 刚刚采集: {goods_name[:20]}..."
        )
        self.detail_panel.progress_bar.setValue(progress)
        
        # 更新任务信息
        self.detail_panel.task_info.setText(
            f"关键词: {message.get('keyword', '')}\n"
            f"状态: running\n"
            f"进度: {saved_count}/{target_count}"
        )
        
        self.statusbar.showMessage(f"📦 采集到商品: {goods_name[:30]}...")
    
    def on_socket_task_started(self, device_id: str, message: dict):
        """任务开始通知"""
        if device_id != self.current_device_id:
            return
        
        keyword = message.get("keyword", "")
        target_count = message.get("target_count", 0)
        
        self.detail_panel.realtime_status.setText(f"🚀 任务开始: {keyword} (目标: {target_count})")
        self.detail_panel.goods_list.clear()
        self.statusbar.showMessage(f"🚀 任务已开始: {keyword}")
    
    def on_socket_task_finished(self, device_id: str, message: dict):
        """任务结束通知"""
        if device_id != self.current_device_id:
            return
        
        state = message.get("state", "")
        saved_count = message.get("saved_count", 0)
        
        if state == "completed":
            self.detail_panel.realtime_status.setText(f"✅ 采集完成: {saved_count} 个商品")
            # 自动导出产物
            self.statusbar.showMessage(f"✅ 任务完成，正在自动导出产物...")
            QTimer.singleShot(1000, self.auto_export_after_task_complete)
        elif state == "stopped":
            self.detail_panel.realtime_status.setText(f"⏹ 任务已停止: {saved_count} 个商品")
        else:
            self.detail_panel.realtime_status.setText(f"❌ 任务失败: {saved_count} 个商品")
        
        self.statusbar.showMessage(f"任务已结束: {state}")
    
    def auto_export_after_task_complete(self):
        """任务完成后自动导出产物"""
        if not self.current_device_id:
            return
        
        device = self.devices.get(self.current_device_id)
        if not device:
            return
        
        # 检查设备连接
        success, _ = SSHManager.test_connection(
            device.host, device.user, device.password, device.port
        )
        if not success:
            self.statusbar.showMessage("❌ 自动导出失败: 设备连接不可用")
            return
        
        # 使用默认导出目录
        save_dir = os.path.expanduser("~/pddgood")
        os.makedirs(save_dir, exist_ok=True)
        
        self.statusbar.showMessage(f"🔄 正在自动导出产物到 {save_dir}...")
        
        # 创建并启动导出线程
        self.export_thread = ExportThread(device, save_dir, self)
        self.export_thread.progress.connect(self.on_export_progress)
        self.export_thread.finished.connect(self.on_auto_export_finished)
        self.export_thread.start()
    
    def on_auto_export_finished(self, success: bool, count: int, message: str):
        """自动导出完成"""
        if success:
            self.statusbar.showMessage(f"✅ 自动导出完成: {message}")
            # 显示通知（不阻塞）
            if count > 0:
                QMessageBox.information(self, "自动导出完成", f"任务产物已自动导出！\n\n{message}")
        else:
            self.statusbar.showMessage(f"❌ 自动导出失败: {message}")
    
    def on_socket_progress_updated(self, device_id: str, message: dict):
        """进度更新通知"""
        if device_id != self.current_device_id:
            return
        
        saved_count = message.get("saved_count", 0)
        target_count = message.get("target_count", 0)
        progress = message.get("progress", 0)
        
        self.detail_panel.progress_bar.setValue(progress)
        self.detail_panel.task_info.setText(
            f"进度: {saved_count}/{target_count}"
        )
    
    def on_device_selected(self, device_id: str):
        """设备被选中"""
        self.current_device_id = device_id
        device = self.devices.get(device_id)
        if device:
            self.detail_panel.set_device(device)
            self.content_stack.setCurrentIndex(1)  # 显示详情页
    
    def show_task_config(self):
        """显示任务配置页面"""
        if not self.current_device_id:
            QMessageBox.warning(self, "警告", "请先选择一个设备")
            return
        self.content_stack.setCurrentIndex(2)  # 显示任务配置页
    
    def show_device_detail(self):
        """显示设备详情页面"""
        self.content_stack.setCurrentIndex(1)  # 显示设备详情页
    
    def show_device_discovery(self):
        """显示设备搜索对话框"""
        # 获取已存在的设备信息，避免重复添加
        existing_devices = []
        for device in self.devices.values():
            existing_devices.append({
                "host": device.host,
                "port": device.port,
                "name": device.name
            })
        
        dialog = DeviceDiscoveryDialog(self, existing_devices)
        dialog.device_selected.connect(self.on_device_discovered)
        dialog.exec()
    
    def on_device_discovered(self, device_info: dict):
        """处理发现的设备"""
        device_type = device_info.get("type")
        
        if device_type == "usb":
            # USB 设备 - 自动设置
            self._add_usb_device(device_info)
        elif device_type == "wifi":
            # WiFi 设备 - 需要测试连接
            self._add_wifi_device(device_info)
    
    def _add_usb_device(self, device_info: dict):
        """添加 USB 设备"""
        self.statusbar.showMessage("正在设置 USB 设备...")
        
        # 设置 USB 端口转发
        success, message = SSHManager.setup_usb_connection()
        if not success:
            QMessageBox.critical(self, "USB 连接失败", message)
            return
        
        # 使用本地端口连接
        host = "127.0.0.1"
        port = 2222
        
        # 测试连接
        success, conn_message = SSHManager.test_connection(host, "mobile", "001314", port)
        if not success:
            QMessageBox.critical(self, "连接失败", f"无法连接到 USB 设备:\n{conn_message}")
            return
        
        # 发现远程路径
        remote_path = SSHManager.discover_remote_path(host, "mobile", "001314", port)
        if not remote_path:
            QMessageBox.warning(self, "路径发现失败", "未找到 PDDGoodsData 目录")
            return
        
        # 创建设备
        import uuid
        device = Device(
            device_id=str(uuid.uuid4())[:8],
            name=device_info.get("name", "iOS USB Device"),
            host=host,
            user="mobile",
            password="001314",
            port=port,
            use_usb=True
        )
        device.remote_path = remote_path
        device.status = "online"
        
        self.devices[device.id] = device
        self.save_devices()
        self.refresh_device_list()
        
        self.statusbar.showMessage(f"USB 设备 {device.name} 添加成功")
        QMessageBox.information(self, "成功", f"USB 设备 {device.name} 已自动添加！")
    
    def _add_wifi_device(self, device_info: dict):
        """添加 WiFi 设备"""
        ip = device_info.get("ip")
        port = device_info.get("port", 22)
        
        self.statusbar.showMessage(f"正在连接 {ip}:{port}...")
        
        # 测试连接
        success, message = SSHManager.test_connection(ip, "mobile", "001314", port)
        if not success:
            QMessageBox.critical(self, "连接失败", f"无法连接到 {ip}:\n{message}\n\n请确保:\n1. 设备已越狱\n2. 已安装 OpenSSH\n3. 密码正确 (默认: 001314)")
            return
        
        # 获取设备名称
        name_success, name_stdout, _ = SSHManager.execute_command(
            ip, "mobile", "001314", "hostname", port
        )
        device_name = name_stdout.strip() if name_success else f"iOS Device ({ip})"
        
        # 发现远程路径
        remote_path = SSHManager.discover_remote_path(ip, "mobile", "001314", port)
        if not remote_path:
            QMessageBox.warning(self, "路径发现失败", "未找到 PDDGoodsData 目录，请确认插件已安装")
            return
        
        # 创建设备
        import uuid
        device = Device(
            device_id=str(uuid.uuid4())[:8],
            name=device_name,
            host=ip,
            user="mobile",
            password="001314",
            port=port,
            use_usb=False
        )
        device.remote_path = remote_path
        device.status = "online"
        
        self.devices[device.id] = device
        self.save_devices()
        self.refresh_device_list()
        
        self.statusbar.showMessage(f"WiFi 设备 {device.name} 添加成功")
        QMessageBox.information(self, "成功", f"WiFi 设备 {device.name} 已添加！")
    
    def show_add_device_dialog(self):
        """显示手动添加设备对话框"""
        dialog = AddDeviceDialog(self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            data = dialog.get_device_data()
            
            # 测试连接
            self.statusbar.showMessage("正在测试连接...")
            success, message = SSHManager.test_connection(
                data["host"], data["user"], data["password"], data["port"]
            )
            
            if not success:
                QMessageBox.critical(self, "连接失败", f"无法连接到设备:\n{message}")
                self.statusbar.showMessage("连接失败")
                return
            
            # 发现远程路径
            self.statusbar.showMessage("正在发现远程路径...")
            remote_path = SSHManager.discover_remote_path(
                data["host"], data["user"], data["password"], data["port"]
            )
            
            if not remote_path:
                QMessageBox.warning(self, "路径发现失败", "未找到 PDDGoodsData 目录，请确认插件已安装")
                self.statusbar.showMessage("路径发现失败")
                return
            
            # 创建设备
            import uuid
            device = Device(
                device_id=str(uuid.uuid4())[:8],
                name=data["name"],
                host=data["host"],
                user=data["user"],
                password=data["password"],
                port=data["port"]
            )
            device.remote_path = remote_path
            device.status = "online"
            
            self.devices[device.id] = device
            self.save_devices()
            self.refresh_device_list()
            
            self.statusbar.showMessage(f"设备 {device.name} 添加成功")
            QMessageBox.information(self, "成功", f"设备 {device.name} 已添加并连接成功！")
    
    def edit_device(self, device_id: str):
        """编辑设备"""
        # TODO: 实现编辑功能
        QMessageBox.information(self, "提示", "编辑功能开发中")
    
    def delete_device(self, device_id: str):
        """删除设备"""
        device = self.devices.get(device_id)
        if not device:
            return
        
        reply = QMessageBox.question(
            self, "确认删除",
            f"确定要删除设备 {device.name} 吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            # 停止监控线程
            if device_id in self.monitors:
                self.monitors[device_id].stop()
                self.monitors[device_id].wait()
                del self.monitors[device_id]
            
            del self.devices[device_id]
            self.save_devices()
            self.refresh_device_list()
            
            if self.current_device_id == device_id:
                self.content_stack.setCurrentIndex(0)
                self.current_device_id = None
    
    def test_device_connection(self, device_id: str):
        """测试设备连接"""
        device = self.devices.get(device_id)
        if not device:
            return
        
        self.statusbar.showMessage(f"正在测试 {device.name} 的连接...")
        success, message = SSHManager.test_connection(
            device.host, device.user, device.password, device.port
        )
        
        if success:
            device.status = "online"
            QMessageBox.information(self, "连接成功", f"设备 {device.name} 连接正常！")
        else:
            device.status = "offline"
            QMessageBox.critical(self, "连接失败", f"设备 {device.name} 连接失败:\n{message}")
        
        self.statusbar.showMessage("就绪")
        self.refresh_device_list()
    
    def refresh_current_device(self):
        """刷新当前设备状态"""
        if self.current_device_id:
            self.test_device_connection(self.current_device_id)
    
    def refresh_all_devices(self):
        """刷新所有设备"""
        for device_id in self.devices:
            self.test_device_connection(device_id)
    
    def submit_task(self, config: dict):
        """提交任务到设备 - 使用后台线程避免阻塞"""
        if not self.current_device_id:
            QMessageBox.warning(self, "警告", "请先选择一个设备")
            return
        
        device = self.devices.get(self.current_device_id)
        if not device or not device.remote_path:
            QMessageBox.warning(self, "警告", "设备未配置远程路径")
            return
        
        # 先检查是否有正在运行的任务
        self.statusbar.showMessage("检查设备状态...")
        status_result = self.check_device_status(device)
        if status_result.get("active", False):
            current_task = status_result.get("keyword", "未知任务")
            reply = QMessageBox.question(
                self, "设备忙",
                f"设备 {device.name} 正在执行任务: {current_task}\n\n"
                f"是否停止当前任务并提交新任务？",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            if reply != QMessageBox.StandardButton.Yes:
                return
            # 先停止当前任务
            self.statusbar.showMessage("正在停止当前任务...")
            self.stop_task_sync(device)
        
        keywords = config["keywords"]
        reply = QMessageBox.question(
            self, "确认提交",
            f"将向设备 {device.name} 提交 {len(keywords)} 个任务:\n" +
            "\n".join([f"  • {k}" for k in keywords[:5]]) +
            (f"\n  ... 等共 {len(keywords)} 个" if len(keywords) > 5 else ""),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply != QMessageBox.StandardButton.Yes:
            return
        
        # 创建并启动后台任务线程
        self.submit_thread = TaskSubmitThread(device, keywords, config, self)
        self.submit_thread.progress.connect(self.on_submit_progress)
        self.submit_thread.finished.connect(self.on_submit_finished)
        self.submit_thread.start()
        
        # 禁用提交按钮，防止重复提交
        self.statusbar.showMessage("任务提交中...")
    
    def check_device_status(self, device) -> dict:
        """检查设备当前任务状态 - 使用SSH直接读取"""
        try:
            status_file = f"{device.remote_path}/commands/status/current.json"
            read_cmd = f"cat '{status_file}' 2>/dev/null || echo '{{}}'"
            
            success, stdout, stderr = SSHManager.execute_command(
                device.host, device.user, device.password, 
                read_cmd, device.port, timeout=10
            )
            
            if success and stdout.strip():
                try:
                    return json.loads(stdout.strip())
                except json.JSONDecodeError:
                    pass
            return {"active": False}
        except Exception as e:
            print(f"检查设备状态失败: {e}")
            return {"active": False}
    
    def stop_task_sync(self, device) -> bool:
        """同步停止任务 - 使用SSH直接创建停止标记文件"""
        try:
            # 创建停止标记文件
            stop_file = f"{device.remote_path}/commands/stop/stop.now"
            create_cmd = f"mkdir -p '{device.remote_path}/commands/stop' && touch '{stop_file}'"
            
            success, stdout, stderr = SSHManager.execute_command(
                device.host, device.user, device.password, 
                create_cmd, device.port, timeout=10
            )
            
            time.sleep(2)  # 等待任务停止
            return success
        except Exception as e:
            print(f"停止任务失败: {e}")
            return False
    
    def _old_stop_task_sync(self, device) -> bool:
        """旧方法：使用pddctl停止任务"""
        try:
            pddctl_cmd = [
                "python3",
                str(Path(__file__).parent.parent / "pdd-ios-device-collect" / "scripts" / "pddctl.py"),
                "--config", str(Path(__file__).parent.parent / "pdd-ios-device-collect" / "device-config.json"),
                "task", "stop"
            ]
            result = subprocess.run(
                pddctl_cmd,
                capture_output=True,
                text=True,
                timeout=10
            )
            # 等待任务停止
            time.sleep(2)
            return result.returncode == 0
        except Exception as e:
            print(f"停止任务失败: {e}")
            return False
    
    def on_submit_progress(self, message: str):
        """任务提交进度更新"""
        self.statusbar.showMessage(message)
    
    def on_submit_finished(self, success_count: int, failed_keywords: list, success_keywords: list):
        """任务提交完成"""
        device = self.devices.get(self.current_device_id)
        
        if failed_keywords:
            QMessageBox.warning(
                self, "部分任务提交失败",
                f"成功: {success_count}/{len(failed_keywords) + success_count}\n\n失败:\n" + "\n".join(failed_keywords)
            )
        else:
            QMessageBox.information(
                self, "提交成功",
                f"已成功向 {device.name} 提交 {success_count} 个任务！"
            )
        
        self.statusbar.showMessage(f"任务提交完成: {success_count}/{len(failed_keywords) + success_count}")
        
        # 返回设备详情页
        self.content_stack.setCurrentIndex(1)
    
    def restart_pinduoduo(self, device) -> bool:
        """重启拼多多应用"""
        try:
            self.statusbar.showMessage(f"正在重启 {device.name} 上的拼多多...")
            
            # 先关闭拼多多
            kill_cmd = "killall -9 pinduoduo 2>/dev/null || true"
            success, stdout, stderr = SSHManager.execute_command(
                device.host, device.user, device.password, kill_cmd, device.port
            )
            
            # 等待应用完全关闭
            import time
            time.sleep(2)
            
            # 启动拼多多
            # 使用 open 命令启动应用（iOS）
            bundle_id = "com.xunmeng.pinduoduo"
            start_cmd = f"open 'pinduoduo://' 2>/dev/null || uiopen 'pinduoduo://' 2>/dev/null || open '{bundle_id}' 2>/dev/null || true"
            
            success, stdout, stderr = SSHManager.execute_command(
                device.host, device.user, device.password, start_cmd, device.port
            )
            
            # 等待应用启动
            self.statusbar.showMessage("等待拼多多启动...")
            time.sleep(5)
            
            # 检查应用是否运行
            check_cmd = "ps aux | grep -i pinduoduo | grep -v grep"
            success, stdout, stderr = SSHManager.execute_command(
                device.host, device.user, device.password, check_cmd, device.port
            )
            
            if success and stdout.strip():
                self.statusbar.showMessage("✅ 拼多多已重启成功")
                return True
            else:
                self.statusbar.showMessage("⚠️ 拼多多重启状态未知")
                return False
                
        except Exception as e:
            self.statusbar.showMessage(f"❌ 重启拼多多失败: {e}")
            return False
    
    def stop_current_task(self):
        """停止当前任务 - 使用 Socket 即时通信"""
        if not self.current_device_id:
            return
        
        device = self.devices.get(self.current_device_id)
        if not device:
            return
        
        reply = QMessageBox.question(
            self, "确认停止",
            f"确定要立即停止设备 {device.name} 的当前任务吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply != QMessageBox.StandardButton.Yes:
            return
        
        self.statusbar.showMessage(f"⏹ 正在立即停止 {device.name} 的任务...")
        
        # 优先使用 Socket 发送即时停止命令
        socket_client = self.socket_clients.get(self.current_device_id)
        if socket_client and socket_client.connected_flag:
            success = socket_client.stop_task()
            if success:
                self.statusbar.showMessage(f"✅ 停止命令已通过 Socket 发送到 {device.name}")
                QMessageBox.information(self, "成功", "停止命令已即时发送！\n设备将立即停止当前操作。")
                return
        
        # Socket 失败时使用 SSH 备用
        if device.remote_path:
            success, message = SSHManager.send_stop_command(
                device.host, device.user, device.password,
                device.remote_path, device.port, use_global=True
            )
            
            if success:
                self.statusbar.showMessage(f"✅ 停止命令已发送到 {device.name}")
                QMessageBox.information(self, "成功", f"停止命令已发送！\n设备将在 0.5 秒内停止当前任务。")
                return
            else:
                self.statusbar.showMessage(f"❌ 停止命令发送失败: {message}")
                QMessageBox.critical(self, "失败", f"停止命令发送失败:\n{message}")
    
    def export_artifacts(self):
        """导出产物 - 使用后台线程"""
        # 查找可用的设备（优先使用 WiFi 连接）
        device = None
        
        # 首先尝试使用当前选中的设备
        if self.current_device_id:
            current = self.devices.get(self.current_device_id)
            if current and current.remote_path:
                # 测试连接
                success, _ = SSHManager.test_connection(
                    current.host, current.user, current.password, current.port
                )
                if success:
                    device = current
        
        # 如果当前设备不可用，查找其他可用设备
        if not device:
            for d in self.devices.values():
                if d.remote_path:
                    success, _ = SSHManager.test_connection(
                        d.host, d.user, d.password, d.port
                    )
                    if success:
                        device = d
                        break
        
        if not device:
            QMessageBox.warning(self, "错误", "没有可用的设备，请检查设备连接")
            return
        
        if not device.remote_path:
            QMessageBox.warning(self, "错误", f"设备 {device.name} 未配置远程路径")
            return
        
        # 默认导出目录（避免 macOS Desktop 权限问题）
        default_dir = os.path.expanduser("~/pddgood")
        os.makedirs(default_dir, exist_ok=True)
        
        # 选择保存目录（使用默认目录）
        save_dir = QFileDialog.getExistingDirectory(self, "选择保存目录", default_dir)
        if not save_dir:
            return
        
        # 检查是否是受限制的目录（macOS Desktop/Documents/Downloads）
        home = os.path.expanduser("~")
        restricted_dirs = [
            os.path.join(home, "Desktop"),
            os.path.join(home, "Documents"),
            os.path.join(home, "Downloads"),
        ]
        
        for restricted in restricted_dirs:
            if save_dir.startswith(restricted):
                reply = QMessageBox.question(
                    self, "权限警告",
                    f"选择的目录 {save_dir} 可能受到 macOS 系统权限限制，导致导出失败。\n\n"
                    f"建议使用 {default_dir} 目录。\n\n"
                    "是否继续使用当前目录？",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No
                )
                if reply == QMessageBox.StandardButton.No:
                    return
                break
        
        self.statusbar.showMessage(f"使用设备 {device.name} ({device.host}) 导出产物...")
        
        # 创建并启动导出线程
        self.export_thread = ExportThread(device, save_dir, self)
        self.export_thread.progress.connect(self.on_export_progress)
        self.export_thread.finished.connect(self.on_export_finished)
        self.export_thread.start()
    
    def on_export_progress(self, message: str):
        """导出进度更新"""
        self.statusbar.showMessage(message)
    
    def on_export_finished(self, success: bool, count: int, message: str):
        """导出完成"""
        if success:
            self.statusbar.showMessage(f"✅ {message}")
            QMessageBox.information(self, "导出完成", message)
        else:
            self.statusbar.showMessage(f"❌ {message}")
            QMessageBox.warning(self, "导出失败", message)
    
    def export_task_artifacts(self, device, task_id: str, save_dir: str = None) -> bool:
        """导出指定任务的产物 - 用于任务完成后自动导出"""
        if not save_dir:
            # 使用默认导出目录（避免 macOS Desktop 权限问题）
            save_dir = os.path.expanduser("~/pddgood")
        
        os.makedirs(save_dir, exist_ok=True)
        
        raw_path = f"{device.remote_path}/tasks/{task_id}/raw"
        local_path = os.path.join(save_dir, task_id)
        
        self.statusbar.showMessage(f"正在导出任务 {task_id} 的产物...")
        
        success, message = SSHManager.download_directory(
            device.host, device.user, device.password,
            raw_path, local_path, device.port
        )
        
        if success:
            self.statusbar.showMessage(f"✅ 任务 {task_id} 产物已导出到: {local_path}")
        else:
            self.statusbar.showMessage(f"❌ 导出失败: {message}")
        
        return success
    
    def export_all_artifacts(self):
        """导出所有设备的产物"""
        # TODO: 实现批量导出
        QMessageBox.information(self, "提示", "批量导出功能开发中")
    
    def show_about(self):
        """显示关于对话框"""
        QMessageBox.about(
            self, "关于 PDD Device Manager",
            "<h2>PDD Device Manager</h2>"
            "<p>版本: 1.0.0</p>"
            "<p>跨平台 iOS 设备管理工具</p>"
            "<p>支持 WiFi 和 USB 连接</p>"
        )
    
    def closeEvent(self, event):
        """关闭事件处理"""
        # 停止 USB 监控
        USBManager.stop_monitoring()
        
        # 停止所有 Socket 客户端
        for socket_client in self.socket_clients.values():
            socket_client.stop()
        
        for socket_client in self.socket_clients.values():
            socket_client.wait(2000)
        
        # 停止所有监控线程
        for monitor in self.monitors.values():
            monitor.stop()
        
        for monitor in self.monitors.values():
            monitor.wait(2000)
        
        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("PDD Device Manager")
    app.setApplicationVersion("1.0.0")
    
    # 设置应用样式
    app.setStyle("Fusion")
    
    # 设置调色板
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(243, 244, 246))
    palette.setColor(QPalette.ColorRole.WindowText, QColor(31, 41, 55))
    app.setPalette(palette)
    
    window = MainWindow()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
