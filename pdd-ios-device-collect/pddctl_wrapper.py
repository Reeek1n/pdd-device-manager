#!/usr/bin/env python3
"""
PDDCTL 包装器 - 整合 pddctl 命令和 Socket 实时通信
提供更易用的 Python API 和实时监控功能
"""

import subprocess
import json
import socket
import threading
import time
import os
from pathlib import Path
from typing import Optional, Callable, Dict, Any
from dataclasses import dataclass
from datetime import datetime


@dataclass
class TaskStatus:
    """任务状态"""
    state: str  # idle, running, completed, failed, stopped
    active: bool
    keyword: Optional[str] = None
    saved_count: int = 0
    target_count: int = 0
    progress: float = 0.0
    error: Optional[str] = None


@dataclass
class CapturedGoods:
    """采集到的商品"""
    goods_id: str
    goods_name: str
    timestamp: int
    task_id: str
    keyword: str


class PDDCTLWrapper:
    """pddctl 包装器类"""
    
    def __init__(self, config_path: str = "device-config.json"):
        self.config_path = Path(config_path)
        self.base_dir = self.config_path.parent
        self.socket_host = "192.168.0.34"
        self.socket_port = 9999
        self.socket_client: Optional[socket.socket] = None
        self.socket_thread: Optional[threading.Thread] = None
        self.running = False
        self.on_goods_captured: Optional[Callable[[CapturedGoods], None]] = None
        self.on_task_started: Optional[Callable[[str, int], None]] = None
        self.on_task_finished: Optional[Callable[[str, str], None]] = None
        self.on_status_update: Optional[Callable[[TaskStatus], None]] = None
        
    def _run_pddctl(self, *args) -> Dict[str, Any]:
        """运行 pddctl 命令"""
        cmd = [
            "python3",
            str(self.base_dir / "scripts" / "pddctl.py"),
            "--config", str(self.config_path),
            *args
        ]
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30
            )
            if result.returncode == 0:
                return json.loads(result.stdout)
            else:
                return {"ok": False, "error": result.stderr}
        except subprocess.TimeoutExpired:
            return {"ok": False, "error": "命令超时"}
        except Exception as e:
            return {"ok": False, "error": str(e)}
    
    def get_status(self) -> TaskStatus:
        """获取当前任务状态"""
        result = self._run_pddctl("task", "status")
        if result.get("ok") and "data" in result:
            data = result["data"]
            return TaskStatus(
                state=data.get("state", "unknown"),
                active=data.get("active", False),
                keyword=data.get("keyword"),
                saved_count=data.get("saved_count", 0),
                target_count=data.get("target_count", 0),
                progress=data.get("progress", 0.0),
                error=data.get("error")
            )
        return TaskStatus(state="error", active=False, error=result.get("error"))
    
    def start_task(self, keyword: str, count: int, 
                   sort_by: Optional[str] = None,
                   price_min: Optional[str] = None,
                   price_max: Optional[str] = None,
                   wait: bool = False) -> Dict[str, Any]:
        """启动采集任务"""
        args = ["task", "collect", "--keyword", keyword, "--count", str(count)]
        
        if sort_by:
            args.extend(["--sort-by", sort_by])
        if price_min:
            args.extend(["--price-min", price_min])
        if price_max:
            args.extend(["--price-max", price_max])
        if wait:
            args.append("--wait")
            
        return self._run_pddctl(*args)
    
    def stop_task(self) -> Dict[str, Any]:
        """停止当前任务"""
        return self._run_pddctl("task", "stop")
    
    def connect_socket(self) -> bool:
        """连接 Socket 实时通信"""
        try:
            self.socket_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket_client.settimeout(5)
            self.socket_client.connect((self.socket_host, self.socket_port))
            
            # 接收欢迎消息
            welcome = self.socket_client.recv(1024)
            print(f"[Socket] 已连接: {welcome.decode().strip()}")
            
            # 启动接收线程
            self.running = True
            self.socket_thread = threading.Thread(target=self._socket_receive_loop)
            self.socket_thread.daemon = True
            self.socket_thread.start()
            
            return True
        except Exception as e:
            print(f"[Socket] 连接失败: {e}")
            return False
    
    def disconnect_socket(self):
        """断开 Socket 连接"""
        self.running = False
        if self.socket_client:
            try:
                self.socket_client.close()
            except:
                pass
            self.socket_client = None
        print("[Socket] 已断开")
    
    def _socket_receive_loop(self):
        """Socket 接收循环"""
        while self.running and self.socket_client:
            try:
                data = self.socket_client.recv(4096)
                if not data:
                    break
                    
                # 处理可能的多条消息
                messages = data.decode().strip().split('\n')
                for msg in messages:
                    if msg:
                        self._handle_socket_message(msg)
                        
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[Socket] 接收错误: {e}")
                break
    
    def _handle_socket_message(self, message: str):
        """处理 Socket 消息"""
        try:
            data = json.loads(message)
            msg_type = data.get("type")
            
            if msg_type == "goods_captured":
                # 商品采集通知
                goods = CapturedGoods(
                    goods_id=data.get("goods", {}).get("goodsId", ""),
                    goods_name=data.get("goods", {}).get("goodsName", ""),
                    timestamp=data.get("timestamp", 0),
                    task_id=data.get("task_id", ""),
                    keyword=data.get("keyword", "")
                )
                if self.on_goods_captured:
                    self.on_goods_captured(goods)
                    
            elif msg_type == "task_started":
                # 任务开始
                if self.on_task_started:
                    self.on_task_started(data.get("keyword", ""), data.get("target_count", 0))
                    
            elif msg_type == "task_finished":
                # 任务结束
                if self.on_task_finished:
                    self.on_task_finished(data.get("state", ""), data.get("error", ""))
                    
            elif msg_type == "status":
                # 状态更新
                status = TaskStatus(
                    state=data.get("state", "idle"),
                    active=data.get("has_task", False),
                    keyword=data.get("keyword"),
                    saved_count=data.get("saved_count", 0),
                    target_count=data.get("target_count", 0),
                    progress=data.get("progress", 0.0)
                )
                if self.on_status_update:
                    self.on_status_update(status)
                    
        except json.JSONDecodeError:
            pass
        except Exception as e:
            print(f"[Socket] 处理消息错误: {e}")
    
    def send_socket_command(self, command: str, **kwargs) -> bool:
        """发送 Socket 命令"""
        if not self.socket_client:
            return False
            
        try:
            msg = {"command": command, **kwargs}
            self.socket_client.send((json.dumps(msg) + "\n").encode())
            return True
        except Exception as e:
            print(f"[Socket] 发送命令失败: {e}")
            return False


class PDDCTLInteractive:
    """交互式控制台"""
    
    def __init__(self):
        self.wrapper = PDDCTLWrapper()
        self.captured_goods: list[CapturedGoods] = []
        
    def on_goods_captured(self, goods: CapturedGoods):
        """商品采集回调"""
        self.captured_goods.append(goods)
        print(f"\n🛒 [实时] 采集到商品: {goods.goods_name[:30]}... (ID: {goods.goods_id})")
        
    def on_task_started(self, keyword: str, count: int):
        """任务开始回调"""
        print(f"\n🚀 [实时] 任务开始: 关键词={keyword}, 目标={count}")
        
    def on_task_finished(self, state: str, error: str):
        """任务结束回调"""
        if error:
            print(f"\n❌ [实时] 任务结束: {state}, 错误: {error}")
        else:
            print(f"\n✅ [实时] 任务结束: {state}")
            
    def on_status_update(self, status: TaskStatus):
        """状态更新回调"""
        if status.active:
            print(f"\n📊 [实时] 进度: {status.saved_count}/{status.target_count} ({status.progress}%)")
    
    def run(self):
        """运行交互式控制台"""
        print("=" * 50)
        print("PDDCTL 实时控制面板")
        print("=" * 50)
        
        # 设置回调
        self.wrapper.on_goods_captured = self.on_goods_captured
        self.wrapper.on_task_started = self.on_task_started
        self.wrapper.on_task_finished = self.on_task_finished
        self.wrapper.on_status_update = self.on_status_update
        
        # 连接 Socket
        print("\n正在连接 Socket 实时通信...")
        if self.wrapper.connect_socket():
            print("✅ Socket 连接成功")
        else:
            print("⚠️ Socket 连接失败，将继续使用轮询模式")
        
        # 获取初始状态
        status = self.wrapper.get_status()
        print(f"\n当前状态: {status.state}")
        if status.active:
            print(f"活跃任务: {status.keyword} ({status.saved_count}/{status.target_count})")
        
        # 交互循环
        while True:
            print("\n" + "-" * 50)
            print("命令:")
            print("  1. start <关键词> <数量> - 启动任务")
            print("  2. stop - 停止任务")
            print("  3. status - 查看状态")
            print("  4. goods - 查看已采集商品")
            print("  5. quit - 退出")
            print("-" * 50)
            
            try:
                cmd = input("\n> ").strip().split()
                if not cmd:
                    continue
                    
                action = cmd[0].lower()
                
                if action == "start" and len(cmd) >= 3:
                    keyword = cmd[1]
                    count = int(cmd[2])
                    print(f"\n启动任务: {keyword}, 数量: {count}")
                    result = self.wrapper.start_task(keyword, count)
                    if result.get("ok"):
                        print("✅ 任务启动成功")
                    else:
                        print(f"❌ 启动失败: {result.get('error')}")
                        
                elif action == "stop":
                    print("\n停止任务...")
                    result = self.wrapper.stop_task()
                    if result.get("ok"):
                        print("✅ 停止命令已发送")
                    else:
                        print(f"❌ 停止失败: {result.get('error')}")
                        
                elif action == "status":
                    status = self.wrapper.get_status()
                    print(f"\n状态: {status.state}")
                    print(f"活跃: {status.active}")
                    if status.keyword:
                        print(f"关键词: {status.keyword}")
                        print(f"进度: {status.saved_count}/{status.target_count} ({status.progress}%)")
                    if status.error:
                        print(f"错误: {status.error}")
                        
                elif action == "goods":
                    print(f"\n已采集商品 ({len(self.captured_goods)}):")
                    for i, g in enumerate(self.captured_goods[-10:], 1):
                        print(f"  {i}. {g.goods_name[:40]}...")
                        
                elif action == "quit":
                    print("\n退出...")
                    break
                    
                else:
                    print("未知命令")
                    
            except KeyboardInterrupt:
                print("\n退出...")
                break
            except Exception as e:
                print(f"错误: {e}")
        
        # 清理
        self.wrapper.disconnect_socket()


if __name__ == "__main__":
    # 切换到正确目录
    os.chdir(Path(__file__).parent)
    
    # 运行交互式控制台
    console = PDDCTLInteractive()
    console.run()
