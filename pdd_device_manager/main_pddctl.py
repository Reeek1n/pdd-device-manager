#!/usr/bin/env python3
"""
PDD Device Manager - 使用 pddctl 后端
整合 pddctl 命令和 Socket 实时通信
"""

import sys
import json
import os
import subprocess
import threading
import socket
import time
from pathlib import Path
from datetime import datetime
from typing import Optional, Dict, List, Callable
from dataclasses import dataclass

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QLineEdit, QTextEdit, QTableWidget, QTableWidgetItem,
    QGroupBox, QFormLayout, QSpinBox, QMessageBox, QProgressBar,
    QTabWidget, QSplitter, QListWidget, QListWidgetItem
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer
from PyQt6.QtGui import QFont, QColor


@dataclass
class TaskStatus:
    state: str
    active: bool
    keyword: Optional[str] = None
    saved_count: int = 0
    target_count: int = 0
    progress: float = 0.0


class PDDCTLWorker(QThread):
    """PDDCTL 工作线程"""
    status_updated = pyqtSignal(dict)
    goods_captured = pyqtSignal(dict)
    task_started = pyqtSignal(str, int)
    task_finished = pyqtSignal(str, str)
    log_message = pyqtSignal(str)
    
    def __init__(self, pddctl_dir: Path):
        super().__init__()
        self.pddctl_dir = pddctl_dir
        self.running = True
        self.socket_client = None
        
    def run(self):
        """主循环 - Socket 连接和消息处理"""
        # 连接 Socket
        if self.connect_socket():
            self.log_message.emit("✅ Socket 实时通信已连接")
            # 接收循环
            while self.running and self.socket_client:
                try:
                    data = self.socket_client.recv(4096)
                    if not data:
                        break
                    messages = data.decode().strip().split('\n')
                    for msg in messages:
                        if msg:
                            self.handle_message(msg)
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        self.log_message.emit(f"Socket 错误: {e}")
                    break
        else:
            self.log_message.emit("⚠️ Socket 连接失败，使用轮询模式")
            # 轮询模式
            while self.running:
                self.poll_status()
                time.sleep(2)
    
    def connect_socket(self) -> bool:
        try:
            self.socket_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket_client.settimeout(5)
            self.socket_client.connect(("192.168.0.34", 9999))
            welcome = self.socket_client.recv(1024)
            return True
        except:
            return False
    
    def handle_message(self, message: str):
        try:
            data = json.loads(message)
            msg_type = data.get("type")
            
            if msg_type == "goods_captured":
                self.goods_captured.emit(data)
            elif msg_type == "task_started":
                self.task_started.emit(data.get("keyword", ""), data.get("target_count", 0))
            elif msg_type == "task_finished":
                self.task_finished.emit(data.get("state", ""), data.get("error", ""))
            elif msg_type == "status":
                self.status_updated.emit(data)
        except:
            pass
    
    def poll_status(self):
        """轮询状态（备用）"""
        result = self.run_pddctl("task", "status")
        if result.get("ok") and "data" in result:
            self.status_updated.emit(result["data"])
    
    def run_pddctl(self, *args) -> dict:
        """运行 pddctl 命令"""
        cmd = [
            "python3",
            str(self.pddctl_dir / "scripts" / "pddctl.py"),
            "--config", str(self.pddctl_dir / "device-config.json"),
            *args
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if result.returncode == 0:
                return json.loads(result.stdout)
            return {"ok": False, "error": result.stderr}
        except Exception as e:
            return {"ok": False, "error": str(e)}
    
    def start_task(self, keyword: str, count: int):
        self.log_message.emit(f"启动任务: {keyword} x{count}")
        result = self.run_pddctl("task", "collect", "--keyword", keyword, "--count", str(count))
        if result.get("ok"):
            self.log_message.emit("✅ 任务启动成功")
        else:
            self.log_message.emit(f"❌ 启动失败: {result.get('error')}")
        return result
    
    def stop_task(self):
        self.log_message.emit("停止任务...")
        result = self.run_pddctl("task", "stop")
        if result.get("ok"):
            self.log_message.emit("✅ 停止命令已发送")
        else:
            self.log_message.emit(f"❌ 停止失败: {result.get('error')}")
        return result
    
    def stop(self):
        self.running = False
        if self.socket_client:
            try:
                self.socket_client.close()
            except:
                pass


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("PDD 设备管理器 (pddctl 版)")
        self.setMinimumSize(1000, 700)
        
        # 查找 pddctl 目录
        self.pddctl_dir = self.find_pddctl_dir()
        
        # 创建 UI
        self.create_ui()
        
        # 启动工作线程
        self.worker = PDDCTLWorker(self.pddctl_dir)
        self.worker.status_updated.connect(self.on_status_updated)
        self.worker.goods_captured.connect(self.on_goods_captured)
        self.worker.task_started.connect(self.on_task_started)
        self.worker.task_finished.connect(self.on_task_finished)
        self.worker.log_message.connect(self.on_log_message)
        self.worker.start()
        
        # 定时刷新
        self.timer = QTimer()
        self.timer.timeout.connect(self.refresh_status)
        self.timer.start(3000)
    
    def find_pddctl_dir(self) -> Path:
        """查找 pddctl 目录"""
        # 首先检查当前目录的父目录
        current = Path(__file__).parent
        pddctl = current.parent / "pdd-ios-device-collect"
        if pddctl.exists():
            return pddctl
        # 检查相邻目录
        for parent in [current.parent, current.parent.parent]:
            for child in parent.iterdir():
                if child.is_dir() and "pdd-ios-device-collect" in child.name:
                    return child
        # 默认返回
        return Path("/Users/leeekin/Desktop/device/pdd-ios-device-collect")
    
    def create_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QHBoxLayout(central)
        
        # 左侧控制面板
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setSpacing(15)
        
        # 状态显示
        status_group = QGroupBox("设备状态")
        status_layout = QVBoxLayout(status_group)
        self.status_label = QLabel("状态: 连接中...")
        self.status_label.setFont(QFont("Arial", 14, QFont.Weight.Bold))
        status_layout.addWidget(self.status_label)
        
        self.task_info = QLabel("无活跃任务")
        status_layout.addWidget(self.task_info)
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        status_layout.addWidget(self.progress_bar)
        left_layout.addWidget(status_group)
        
        # 任务控制
        control_group = QGroupBox("任务控制")
        control_layout = QFormLayout(control_group)
        
        self.keyword_input = QLineEdit()
        self.keyword_input.setPlaceholderText("输入关键词，如: 手机壳")
        control_layout.addRow("关键词:", self.keyword_input)
        
        self.count_input = QSpinBox()
        self.count_input.setRange(1, 100)
        self.count_input.setValue(5)
        control_layout.addRow("采集数量:", self.count_input)
        
        btn_layout = QHBoxLayout()
        self.start_btn = QPushButton("🚀 启动任务")
        self.start_btn.setStyleSheet("background-color: #4CAF50; color: white; padding: 10px;")
        self.start_btn.clicked.connect(self.on_start_task)
        btn_layout.addWidget(self.start_btn)
        
        self.stop_btn = QPushButton("⏹ 停止任务")
        self.stop_btn.setStyleSheet("background-color: #f44336; color: white; padding: 10px;")
        self.stop_btn.clicked.connect(self.on_stop_task)
        btn_layout.addWidget(self.stop_btn)
        control_layout.addRow(btn_layout)
        
        left_layout.addWidget(control_group)
        
        # 实时采集列表
        goods_group = QGroupBox("实时采集")
        goods_layout = QVBoxLayout(goods_group)
        self.goods_list = QListWidget()
        self.goods_list.setMaximumHeight(200)
        goods_layout.addWidget(self.goods_list)
        left_layout.addWidget(goods_group)
        
        left_layout.addStretch()
        layout.addWidget(left_panel, 1)
        
        # 右侧日志
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        
        log_group = QGroupBox("运行日志")
        log_layout = QVBoxLayout(log_group)
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        log_layout.addWidget(self.log_text)
        right_layout.addWidget(log_group)
        
        layout.addWidget(right_panel, 2)
    
    def on_start_task(self):
        keyword = self.keyword_input.text().strip()
        count = self.count_input.value()
        if keyword:
            self.worker.start_task(keyword, count)
        else:
            QMessageBox.warning(self, "警告", "请输入关键词")
    
    def on_stop_task(self):
        self.worker.stop_task()
    
    def on_status_updated(self, data: dict):
        state = data.get("state", "unknown")
        active = data.get("active", False)
        
        self.status_label.setText(f"状态: {state.upper()}")
        
        if active:
            keyword = data.get("keyword", "")
            saved = data.get("saved_count", 0)
            target = data.get("target_count", 0)
            progress = data.get("progress", 0)
            
            self.task_info.setText(f"任务: {keyword} ({saved}/{target})")
            self.progress_bar.setValue(int(progress))
            self.start_btn.setEnabled(False)
            self.stop_btn.setEnabled(True)
        else:
            self.task_info.setText("无活跃任务")
            self.progress_bar.setValue(0)
            self.start_btn.setEnabled(True)
            self.stop_btn.setEnabled(False)
    
    def on_goods_captured(self, data: dict):
        goods = data.get("goods", {})
        name = goods.get("goodsName", "未知商品")[:30]
        goods_id = goods.get("goodsId", "")
        
        item = QListWidgetItem(f"🛒 {name}... (ID: {goods_id})")
        self.goods_list.insertItem(0, item)
        
        # 只保留最近 20 条
        while self.goods_list.count() > 20:
            self.goods_list.takeItem(20)
    
    def on_task_started(self, keyword: str, count: int):
        self.log_message(f"🚀 任务开始: {keyword} x{count}")
    
    def on_task_finished(self, state: str, error: str):
        if error:
            self.log_message(f"❌ 任务结束: {state}, 错误: {error}")
        else:
            self.log_message(f"✅ 任务结束: {state}")
    
    def on_log_message(self, msg: str):
        self.log_message(msg)
    
    def log_message(self, msg: str):
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.append(f"[{timestamp}] {msg}")
    
    def refresh_status(self):
        # 通过 Socket 自动接收状态，这里不需要额外操作
        pass
    
    def closeEvent(self, event):
        self.worker.stop()
        self.worker.wait(2000)
        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    
    # 设置样式
    app.setStyleSheet("""
        QMainWindow {
            background-color: #f5f5f5;
        }
        QGroupBox {
            font-weight: bold;
            border: 2px solid #ddd;
            border-radius: 8px;
            margin-top: 10px;
            padding-top: 10px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
        }
        QPushButton {
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            opacity: 0.8;
        }
        QProgressBar {
            border: 2px solid #ddd;
            border-radius: 4px;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #4CAF50;
            border-radius: 2px;
        }
    """)
    
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
