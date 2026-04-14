#!/usr/bin/env python3
import os
import json
import subprocess
import time
import uuid
from flask import Flask, render_template_string, request, Response, redirect, url_for

app = Flask(__name__)

PDDCTL_PATH = '/Users/leeekin/Desktop/device/pdd-ios-device-collect/scripts/pddctl.py'
DEVICE_REGISTRY_FILE = '/Users/leeekin/Desktop/device/devices.json'

TASK_PROGRESS = {
    'running': False,
    'current_keyword': '',
    'progress': 0,
    'saved_count': 0,
    'goods_count': 0,
    'total': 0,
    'completed': 0,
    'errors': [],
    'mode': 'continuous',
    'interval_seconds': 300,
    'next_run_time': None,
}

def load_devices():
    if os.path.exists(DEVICE_REGISTRY_FILE):
        with open(DEVICE_REGISTRY_FILE, 'r') as f:
            return json.load(f)
    return {'devices': [], 'selected_id': None}

def save_devices(data):
    with open(DEVICE_REGISTRY_FILE, 'w') as f:
        json.dump(data, f, indent=2)

def ssh_command(host, user, password, command, timeout=30):
    cmd = ['sshpass', '-p', password, 'ssh', '-o', 'StrictHostKeyChecking=no', '-o', f'ConnectTimeout={timeout}', f'{user}@{host}', command]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return result.stdout, result.stderr, result.returncode
    except Exception as e:
        return '', str(e), 1

def discover_device_paths(host, user, password):
    cmd = "find /var/mobile/Containers/Data/Application -maxdepth 4 -type d -path '*/Documents/PDDGoodsData' 2>/dev/null"
    stdout, stderr, code = ssh_command(host, user, password, cmd)
    paths = [p.strip() for p in stdout.strip().split('\n') if p.strip()]
    return paths

def get_selected_device():
    devices_data = load_devices()
    if not devices_data.get('selected_id'):
        return None
    for device in devices_data.get('devices', []):
        if device['id'] == devices_data['selected_id']:
            return device
    return None

TEMPLATE = '''
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>PDD 数据采集控制台</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #f5f7fa;
            min-height: 100vh;
            color: #333;
        }
        .header {
            background: linear-gradient(135deg, #10b981 0%, #059669 100%);
            padding: 16px 24px;
            color: white;
            box-shadow: 0 2px 10px rgba(16, 185, 129, 0.3);
        }
        .header h1 { font-size: 20px; font-weight: 600; }
        .header .subtitle { font-size: 12px; opacity: 0.9; margin-top: 4px; }
        .main-container {
            display: grid;
            grid-template-columns: 340px 1fr 300px;
            gap: 20px;
            max-width: 1500px;
            margin: 20px auto;
            padding: 0 20px;
        }
        .panel {
            background: white;
            border-radius: 12px;
            padding: 20px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1);
        }
        .panel-title {
            font-size: 15px;
            font-weight: 600;
            color: #10b981;
            margin-bottom: 16px;
            padding-bottom: 10px;
            border-bottom: 1px solid #eee;
        }
        .form-group { margin-bottom: 14px; }
        label {
            display: block;
            font-size: 13px;
            color: #666;
            margin-bottom: 6px;
            font-weight: 500;
        }
        input, textarea, select {
            width: 100%;
            padding: 10px 12px;
            background: #f9fafb;
            border: 1px solid #e5e7eb;
            border-radius: 8px;
            font-size: 14px;
            color: #333;
            transition: all 0.2s;
        }
        input:focus, textarea:focus, select:focus {
            outline: none;
            border-color: #10b981;
            box-shadow: 0 0 0 3px rgba(16, 185, 129, 0.1);
        }
        textarea { min-height: 80px; resize: vertical; }
        select { cursor: pointer; }
        .btn {
            display: inline-block;
            padding: 10px 18px;
            border: none;
            border-radius: 8px;
            font-size: 14px;
            cursor: pointer;
            transition: all 0.2s;
            font-weight: 500;
        }
        .btn-primary { background: #10b981; color: white; }
        .btn-primary:hover { background: #059669; }
        .btn-danger { background: #ef4444; color: white; }
        .btn-danger:hover { background: #dc2626; }
        .btn-secondary { background: #6b7280; color: white; }
        .btn-secondary:hover { background: #4b5563; }
        .btn-outline {
            background: transparent;
            border: 1px solid #d1d5db;
            color: #666;
        }
        .btn-outline:hover { background: #f3f4f6; }
        .btn-sm { padding: 6px 12px; font-size: 12px; }
        .btn-group { display: flex; flex-direction: column; gap: 8px; }
        .btn-row { display: flex; gap: 8px; flex-wrap: wrap; }
        .alert {
            padding: 14px;
            border-radius: 8px;
            margin-bottom: 14px;
            font-size: 13px;
        }
        .alert-success { background: #d1fae5; color: #065f46; border: 1px solid #10b981; }
        .alert-error { background: #fee2e2; color: #991b1b; border: 1px solid #ef4444; }
        .alert-info { background: #dbeafe; color: #1e40af; border: 1px solid #3b82f6; }
        pre {
            background: #f9fafb;
            padding: 12px;
            border-radius: 6px;
            overflow-x: auto;
            font-size: 12px;
            white-space: pre-wrap;
            word-wrap: break-word;
            max-height: 300px;
        }
        .task-list { max-height: 280px; overflow-y: auto; }
        .task-item {
            background: #f9fafb;
            padding: 12px;
            border-radius: 8px;
            margin-bottom: 8px;
            border-left: 3px solid #10b981;
        }
        .task-item .keyword { font-weight: 600; color: #333; }
        .task-item .stats { font-size: 12px; color: #666; margin-top: 4px; }
        .task-item .time { font-size: 11px; color: #999; margin-top: 2px; }
        .config-row { display: flex; gap: 10px; }
        .config-row .form-group { flex: 1; }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 12px;
            margin-bottom: 16px;
        }
        .stat-card {
            background: #f9fafb;
            padding: 16px;
            border-radius: 10px;
            text-align: center;
        }
        .stat-card .value { font-size: 26px; font-weight: 700; color: #10b981; }
        .stat-card .label { font-size: 12px; color: #888; margin-top: 4px; }
        .progress-box {
            background: #f9fafb;
            border-radius: 10px;
            padding: 16px;
            margin-bottom: 16px;
            display: none;
        }
        .progress-box.show { display: block; }
        .progress-header { display: flex; justify-content: space-between; margin-bottom: 8px; font-size: 13px; }
        .progress-bar {
            height: 8px;
            background: #e5e7eb;
            border-radius: 4px;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #10b981, #059669);
            transition: width 0.3s ease;
        }
        .progress-footer {
            display: flex;
            justify-content: space-between;
            margin-top: 8px;
            font-size: 12px;
            color: #888;
        }
        .mode-selector {
            display: flex;
            gap: 8px;
            margin-bottom: 16px;
        }
        .mode-btn {
            flex: 1;
            padding: 12px;
            background: #f9fafb;
            border: 1px solid #e5e7eb;
            border-radius: 8px;
            color: #666;
            cursor: pointer;
            transition: all 0.2s;
            font-size: 13px;
            text-align: center;
        }
        .mode-btn.active {
            border-color: #10b981;
            color: #10b981;
            background: #d1fae5;
        }
        .mode-btn:hover { border-color: #10b981; }
        .interval-input {
            background: #f9fafb;
            border-radius: 8px;
            padding: 12px;
            margin-bottom: 14px;
            border: 1px solid #e5e7eb;
            display: none;
        }
        .interval-input.show { display: block; }
        .device-checklist {
            max-height: 150px;
            overflow-y: auto;
            background: white;
            border-radius: 6px;
            padding: 8px;
            border: 1px solid #e5e7eb;
        }
        .device-checkbox-item {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 8px;
            cursor: pointer;
            border-radius: 4px;
            font-size: 13px;
        }
        .device-checkbox-item:hover { background: #f3f4f6; }
        .device-checkbox-item input[type="checkbox"] { width: auto; }
        .help-text { font-size: 12px; color: #888; line-height: 1.6; }
        .device-selector {
            background: #f9fafb;
            border-radius: 8px;
            padding: 12px;
            margin-bottom: 16px;
        }
        .device-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 10px;
            background: white;
            border-radius: 6px;
            margin-bottom: 6px;
            border: 1px solid #e5e7eb;
        }
        .device-item.selected {
            border-color: #10b981;
            background: #d1fae5;
        }
        .device-item .name { font-weight: 600; }
        .device-item .host { font-size: 11px; color: #888; }
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0,0,0,0.5);
            justify-content: center;
            align-items: center;
            z-index: 1000;
        }
        .modal.show { display: flex; }
        .modal-content {
            background: white;
            padding: 24px;
            border-radius: 12px;
            max-width: 500px;
            width: 90%;
        }
        .modal h3 { margin-bottom: 16px; color: #333; }
        .modal .close {
            float: right;
            font-size: 24px;
            cursor: pointer;
            color: #888;
        }
        .path-list {
            max-height: 200px;
            overflow-y: auto;
            margin: 10px 0;
        }
        .path-item {
            padding: 10px;
            background: #f9fafb;
            border-radius: 6px;
            margin-bottom: 6px;
            cursor: pointer;
            border: 1px solid #e5e7eb;
            font-size: 12px;
            word-break: break-all;
        }
        .path-item:hover { border-color: #10b981; background: #d1fae5; }
        .path-item.selected { border-color: #10b981; background: #d1fae5; }
        @media (max-width: 1100px) {
            .main-container { grid-template-columns: 1fr; }
            .stats-grid { grid-template-columns: repeat(2, 1fr); }
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>📱 PDD 数据采集控制台</h1>
        <div class="subtitle">多设备管理 · {{ devices|length }} 台设备</div>
    </div>

    <div class="main-container">
        <div class="panel">
            <div class="panel-title">📝 任务配置</div>

            <form method="POST" action="/" id="mainForm">
                <div class="form-group">
                    <label>关键词列表（每行一个）</label>
                    <textarea name="keywords" placeholder="手机&#10;连衣裙&#10;耳机">{{ keywords }}</textarea>
                </div>

                <div class="form-group">
                    <label>采集数量</label>
                    <input type="number" name="count" value="{{ count }}" min="1" max="1000">
                </div>

                <div class="form-group">
                    <label>排序方式</label>
                    <select name="sort_by">
                        <option value="" {% if sort_by == '' %}selected{% endif %}>综合排序</option>
                        <option value="sales" {% if sort_by == 'sales' %}selected{% endif %}>销量优先</option>
                        <option value="price_asc" {% if sort_by == 'price_asc' %}selected{% endif %}>价格低到高</option>
                        <option value="price_desc" {% if sort_by == 'price_desc' %}selected{% endif %}>价格高到低</option>
                    </select>
                </div>

                <div class="config-row">
                    <div class="form-group">
                        <label>最低价</label>
                        <input type="number" name="price_min" value="{{ price_min }}" placeholder="0">
                    </div>
                    <div class="form-group">
                        <label>最高价</label>
                        <input type="number" name="price_max" value="{{ price_max }}" placeholder="999">
                    </div>
                </div>

                <div class="form-group">
                    <label>最低评价数（不填则采集全部）</label>
                    <input type="number" name="review_min" value="{{ review_min }}" placeholder="如: 100" min="0">
                </div>

                <div class="form-group" style="margin-top: 15px;">
                    <button type="button" class="btn btn-secondary" onclick="saveDefaults()">💾 保存默认设置</button>
                </div>

                <div class="panel-title" style="margin-top: 20px;">📡 任务分发</div>
                <div class="mode-selector">
                    <button type="button" class="mode-btn {% if distribute_mode != 'multi' %}active{% endif %}" onclick="setDistributeMode('single')">
                        📱 单设备
                    </button>
                    <button type="button" class="mode-btn {% if distribute_mode == 'multi' %}active{% endif %}" onclick="setDistributeMode('multi')">
                        🔄 分发模式
                    </button>
                </div>
                <input type="hidden" name="distribute_mode" id="distribute_mode" value="{% if distribute_mode %}{{distribute_mode}}{% else %}single{% endif %}">

                <div class="interval-input {% if distribute_mode == 'multi' %}show{% endif %}" id="device_selector_box">
                    <label style="margin-bottom: 8px;">选择分发设备：</label>
                    <div class="device-checklist">
                        {% for device in devices %}
                        <label class="device-checkbox-item">
                            <input type="checkbox" name="target_devices" value="{{ device.id }}" {% if device.id == selected_id %}checked{% endif %}>
                            <span>{{ device.name }}</span>
                            <small style="color:#888">{{ device.host }}</small>
                        </label>
                        {% endfor %}
                    </div>
                </div>

                <div class="panel-title" style="margin-top: 20px;">⚡ 执行模式</div>
                <div class="mode-selector">
                    <button type="button" class="mode-btn {% if task_mode != 'interval' %}active{% endif %}" onclick="setMode('continuous')">
                        🚀 连续执行
                    </button>
                    <button type="button" class="mode-btn {% if task_mode == 'interval' %}active{% endif %}" onclick="setMode('interval')">
                        ⏱ 间隔执行
                    </button>
                </div>
                <input type="hidden" name="task_mode" id="task_mode" value="{% if task_mode %}{{task_mode}}{% else %}continuous{% endif %}">

                <div class="interval-input {% if task_mode == 'interval' %}show{% endif %}" id="interval_input">
                    <div class="form-group" style="margin-bottom: 0;">
                        <label>间隔时间（秒）</label>
                        <input type="number" name="interval_seconds" value="{{ interval_seconds or 300 }}" min="30" placeholder="300">
                    </div>
                </div>

                <div class="btn-group" style="margin-top: 16px;">
                    <button type="submit" name="action" value="submit" class="btn btn-primary">🚀 开始执行</button>
                    <button type="submit" name="action" value="stop" class="btn btn-danger">⏹ 停止任务</button>
                    <button type="submit" name="action" value="doctor" class="btn btn-outline">🔍 系统检测</button>
                </div>

                <div class="panel-title" style="margin-top: 20px;">⚡ 快捷操作</div>
                <div class="btn-group" style="margin-bottom: 16px;">
                    <button type="submit" name="action" value="status" class="btn btn-outline">📱 设备状态</button>
                    <button type="submit" name="action" value="list" class="btn btn-outline">📁 产物列表</button>
                    <button type="submit" name="action" value="export" class="btn btn-outline">💾 导出数据</button>
                </div>
            </form>
        </div>

        <div class="panel">
            <div class="panel-title">📊 任务状态</div>

            <div class="progress-box" id="progress_box">
                <div class="progress-header">
                    <span>正在采集: <b id="current_keyword">-</b></span>
                    <span id="progress_percent">0%</span>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill" id="progress_fill"></div>
                </div>
                <div class="progress-footer">
                    <span>已保存: <b id="saved_count">0</b></span>
                    <span>商品数: <b id="goods_count">0</b></span>
                    <span>完成: <b id="completed_count">0</b>/<b id="total_count">0</b></span>
                    <span id="next_run"></span>
                </div>
            </div>

            {% if message %}
            <div class="alert alert-{% if message_type %}{{ message_type }}{% else %}info{% endif %}"><pre>{{ message }}</pre></div>
            {% endif %}

            <div class="stats-grid">
                <div class="stat-card">
                    <div class="value" id="stat_running">0</div>
                    <div class="label">运行中</div>
                </div>
                <div class="stat-card">
                    <div class="value" id="stat_completed">0</div>
                    <div class="label">已完成</div>
                </div>
                <div class="stat-card">
                    <div class="value" id="stat_saved">0</div>
                    <div class="label">已保存</div>
                </div>
                <div class="stat-card">
                    <div class="value" id="stat_errors">0</div>
                    <div class="label">错误数</div>
                </div>
            </div>

            <div class="panel-title">📋 任务日志</div>
            <div class="task-list" id="task_list">
                <div class="task-item">
                    <div class="keyword">等待任务...</div>
                    <div class="stats">准备就绪</div>
                </div>
            </div>
        </div>

        <div class="panel">
            <div class="panel-title">📱 设备管理
                <button type="button" class="btn btn-sm btn-secondary" onclick="clearSshSockets()" style="float:right;margin-left:10px;">🔧 修复连接</button>
            </div>

            <div class="device-selector">
                {% if devices %}
                    {% for device in devices %}
                    <div class="device-item {% if device.id == selected_id %}selected{% endif %}" onclick="selectDevice('{{ device.id }}')">
                        <div>
                            <div class="name">{{ device.name }}</div>
                            <div class="host">{{ device.host }}</div>
                        </div>
                        <div>
                            <button type="button" class="btn btn-sm btn-outline" onclick="event.stopPropagation(); editDevice('{{ device.id }}')">✏️</button>
                            <button type="button" class="btn btn-sm btn-outline" onclick="event.stopPropagation(); deleteDevice('{{ device.id }}')">🗑️</button>
                        </div>
                    </div>
                    {% endfor %}
                {% else %}
                    <div style="text-align: center; color: #888; padding: 20px;">
                        暂无设备，请添加
                    </div>
                {% endif %}
                <button type="button" class="btn btn-primary" style="width: 100%; margin-top: 10px;" onclick="showAddModal()">+ 添加设备</button>
            </div>

            <div class="panel-title" style="margin-top: 20px;">ℹ️ 使用说明</div>
            <div class="help-text">
                <p><strong>连续执行:</strong> 批量快速执行所有任务</p>
                <p><strong>间隔执行:</strong> 每个任务间隔N秒，避免风控</p>
                <p style="margin-top: 8px;">建议批量任务使用间隔模式</p>
            </div>
        </div>
    </div>

    <div class="modal" id="addModal">
        <div class="modal-content">
            <span class="close" onclick="hideModal()">&times;</span>
            <h3>添加新设备</h3>
            <form method="POST" action="/device/add">
                <div class="form-group">
                    <label>设备名称</label>
                    <input type="text" name="name" placeholder="如: 我的iPhone" required>
                </div>
                <div class="form-group">
                    <label>设备地址 (IP)</label>
                    <input type="text" name="host" placeholder="192.168.1.100" required>
                </div>
                <div class="form-group">
                    <label>SSH 用户</label>
                    <input type="text" name="user" value="mobile" placeholder="mobile" required>
                </div>
                <div class="form-group">
                    <label>SSH 密码</label>
                    <input type="password" name="password" placeholder="******" required>
                </div>
                <div class="btn-row">
                    <button type="submit" class="btn btn-primary" style="flex:1">🔍 搜索路径</button>
                </div>
            </form>
        </div>
    </div>

    <div class="modal" id="pathModal">
        <div class="modal-content">
            <span class="close" onclick="hidePathModal()">&times;</span>
            <h3>选择数据路径</h3>
            <p style="color: #888; margin-bottom: 10px;">请选择这台设备的 PDDGoodsData 路径：</p>
            <div class="path-list" id="pathList"></div>
            <form method="POST" action="/device/save">
                <input type="hidden" name="device_id" id="pathDeviceId">
                <input type="hidden" name="inbox_path" id="selectedInboxPath">
                <input type="hidden" name="name" id="pathDeviceName">
                <input type="hidden" name="host" id="pathDeviceHost">
                <input type="hidden" name="user" id="pathDeviceUser">
                <input type="hidden" name="password" id="pathDevicePassword">
                <button type="submit" class="btn btn-primary" style="width: 100%; margin-top: 10px;" id="savePathBtn" disabled>保存设备</button>
            </form>
        </div>
    </div>

    <script>
        let eventSource = null;

        function connectEvents() {
            eventSource = new EventSource('/events');
            eventSource.onmessage = function(event) {
                const data = JSON.parse(event.data);
                updateProgress(data);
            };
            eventSource.onerror = function() {
                eventSource.close();
                setTimeout(connectEvents, 3000);
            };
        }

        function updateProgress(data) {
            const box = document.getElementById('progress_box');
            if (data.running || data.completed > 0) {
                box.classList.add('show');
                document.getElementById('current_keyword').textContent = data.current_keyword || '-';
                document.getElementById('progress_percent').textContent = data.progress + '%';
                document.getElementById('progress_fill').style.width = data.progress + '%';
                document.getElementById('saved_count').textContent = data.saved_count || 0;
                document.getElementById('goods_count').textContent = data.goods_count || 0;
                document.getElementById('completed_count').textContent = data.completed || 0;
                document.getElementById('total_count').textContent = data.total || 0;
                document.getElementById('stat_running').textContent = data.running ? '1' : '0';
                document.getElementById('stat_completed').textContent = data.completed || 0;
                document.getElementById('stat_saved').textContent = data.saved_count || 0;
                document.getElementById('stat_errors').textContent = data.errors ? data.errors.length : 0;
                if (data.next_run_time) {
                    document.getElementById('next_run').textContent = '下次: ' + data.next_run_time;
                } else {
                    document.getElementById('next_run').textContent = '';
                }
            } else {
                box.classList.remove('show');
            }
        }

        function setMode(mode) {
            document.getElementById('task_mode').value = mode;
            document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('interval_input').classList.toggle('show', mode === 'interval');
        }

        function setDistributeMode(mode) {
            document.getElementById('distribute_mode').value = mode;
            document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('device_selector_box').classList.toggle('show', mode === 'multi');
        }

        function showAddModal() {
            document.getElementById('addModal').classList.add('show');
        }

        function hideModal() {
            document.getElementById('addModal').classList.remove('show');
        }

        function hidePathModal() {
            document.getElementById('pathModal').classList.remove('show');
        }

        function selectDevice(id) {
            fetch('/device/select/' + id, {method: 'POST'}).then(() => location.reload());
        }

        function editDevice(id) {
            alert('编辑功能开发中');
        }

        function deleteDevice(id) {
            if (confirm('确定删除这台设备？')) {
                fetch('/device/delete/' + id, {method: 'POST'}).then(() => location.reload());
            }
        }

        function clearSshSockets() {
            fetch('/admin/clear-ssh', {method: 'POST'})
                .then(r => r.json())
                .then(d => {
                    alert(d.message);
                });
        }

        function saveDefaults() {
            const form = document.getElementById('mainForm');
            const formData = new FormData(form);
            fetch('/settings/save', {method: 'POST', body: formData})
                .then(r => r.json())
                .then(d => {
                    if (d.ok) alert('✅ ' + d.message);
                    else alert('❌ ' + d.message);
                });
        }

        function selectPath(path) {
            document.querySelectorAll('.path-item').forEach(p => p.classList.remove('selected'));
            event.target.classList.add('selected');
            document.getElementById('selectedInboxPath').value = path;
            document.getElementById('savePathBtn').disabled = false;
        }

        document.addEventListener('DOMContentLoaded', function() {
            connectEvents();
        });
    </script>
</body>
</html>
'''

def run_pddctl(args, host, user, password, port=22):
    import glob
    for f in glob.glob(f'/tmp/ssh-mux-*{user}@{host}*'):
        try:
            os.remove(f)
        except:
            pass
    cmd = ['python3', PDDCTL_PATH, '--ssh-host', host, '--ssh-port', str(port), '--ssh-user', user, '--ssh-password', password] + args
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return '', 'Command timed out', 124
    except Exception as e:
        return '', str(e), 1

def cleanup_export(task_dir):
    import shutil
    keep_files = {'goods_items.json'}
    keep_dirs = {'raw'}
    remove_items = []

    if not os.path.isdir(task_dir):
        return remove_items

    for item in os.listdir(task_dir):
        item_path = os.path.join(task_dir, item)
        if item == 'raw':
            continue
        elif item in keep_files:
            continue
        elif os.path.isdir(item_path):
            remove_items.append(item_path)
            shutil.rmtree(item_path)
        else:
            remove_items.append(item_path)
            os.remove(item_path)

    return remove_items

def format_json_output(stdout):
    try:
        data = json.loads(stdout)
        return json.dumps(data, indent=2, ensure_ascii=False)
    except:
        return stdout

@app.route('/events')
def events():
    def generate():
        while True:
            data = json.dumps(TASK_PROGRESS)
            yield f"data: {data}\n\n"
            time.sleep(1)
    return Response(generate(), mimetype='text/event-stream')

@app.route('/', methods=['GET', 'POST'])
def index():
    global TASK_PROGRESS

    devices_data = load_devices()
    devices = devices_data.get('devices', [])
    selected_device = get_selected_device()

    config = {'host': '', 'user': '', 'password': ''}
    if selected_device:
        config['host'] = selected_device.get('host', '')
        config['user'] = selected_device.get('user', '')
        config['password'] = selected_device.get('password', '')

    message = ''
    message_type = 'info'
    keywords = ''
    default_settings = load_default_settings()
    count = default_settings.get('count', '50')
    sort_by = default_settings.get('sort_by', '')
    price_min = default_settings.get('price_min', '')
    price_max = default_settings.get('price_max', '')
    review_min = default_settings.get('review_min', '')
    task_mode = default_settings.get('task_mode', 'once')
    interval_seconds = int(default_settings.get('interval_seconds', 300))
    distribute_mode = 'single'
    target_devices = []

    if request.method == 'POST' and selected_device:
        keywords = request.form.get('keywords', '')
        count = request.form.get('count', '50')
        sort_by = request.form.get('sort_by', '')
        price_min = request.form.get('price_min', '')
        price_max = request.form.get('price_max', '')
        review_min = request.form.get('review_min', '')
        task_mode = request.form.get('task_mode', 'once')
        interval_seconds = int(request.form.get('interval_seconds', 300))
        distribute_mode = request.form.get('distribute_mode', 'single')
        target_devices = request.form.getlist('target_devices')
        action = request.form.get('action', 'submit')

        host = selected_device['host']
        user = selected_device['user']
        password = selected_device['password']
        port = selected_device.get('port', 22)
        inbox_path = selected_device.get('inbox_path', '')

        if not inbox_path:
            message = "错误: 请先配置设备路径"
            message_type = 'error'

        elif action == 'doctor':
            stdout, stderr, code = run_pddctl(['doctor'], host, user, password, port)
            if code == 0:
                message = format_json_output(stdout)
                message_type = 'info'
            else:
                message = f"错误: {stderr or stdout}"
                message_type = 'error'

        elif action == 'status':
            stdout, stderr, code = run_pddctl(['task', 'status'], host, user, password, port)
            if code == 0:
                message = format_json_output(stdout)
                message_type = 'info'
            else:
                message = f"错误: {stderr or stdout}"
                message_type = 'error'

        elif action == 'stop':
            stdout, stderr, code = run_pddctl(['task', 'stop'], host, user, password, port)
            if code == 0:
                message = "⏹ 已发送停止命令，正在导出产物...\n"
                export_args = ['artifact', 'export']
                exp_stdout, exp_stderr, exp_code = run_pddctl(export_args, host, user, password, port)
                if exp_code == 0:
                    try:
                        exp_data = json.loads(exp_stdout)
                        local_root = exp_data.get('data', {}).get('local_task_root', '')
                        if local_root and os.path.isdir(local_root):
                            removed = cleanup_export(local_root)
                            message += f"✓ 产物已导出到: {local_root}\n"
                            if removed:
                                message += f"  (已清理 {len(removed)} 个不需要的文件/目录)\n"
                        else:
                            message += f"✓ 产物已导出\n"
                    except:
                        message += f"✓ 导出完成\n"
                else:
                    message += f"导出状态: {exp_stderr or exp_stdout}\n"
                TASK_PROGRESS['running'] = False
                message_type = 'info'
            else:
                message = f"错误: {stderr or stdout}"
                message_type = 'error'

        elif action == 'list':
            stdout, stderr, code = run_pddctl(['artifact', 'list'], host, user, password, port)
            if code == 0:
                message = format_json_output(stdout)
                message_type = 'info'
            else:
                message = f"错误: {stderr or stdout}"
                message_type = 'error'

        elif action == 'export':
            stdout, stderr, code = run_pddctl(['artifact', 'list'], host, user, password, port)
            if code == 0:
                try:
                    data = json.loads(stdout)
                    items = data.get('data', {}).get('items', [])
                    if not items:
                        message = "暂无产物数据"
                    else:
                        lines = ["📦 产物列表：", ""]
                        completed_count = 0
                        failed_count = 0
                        total_saved = 0
                        for item in items[:10]:
                            state = item.get('state', '')
                            keyword = item.get('keyword', '')
                            saved = item.get('saved_count', 0)
                            task_id = item.get('task_id', '')
                            if state == 'completed':
                                completed_count += 1
                                total_saved += saved
                                lines.append(f"✅ {keyword} | 已保存: {saved} | ID: {task_id}")
                            else:
                                failed_count += 1
                                lines.append(f"❌ {keyword} | 失败 | ID: {task_id}")
                        lines.append("")
                        lines.append(f"总计: {len(items)} 个任务 | ✅完成: {completed_count} | ❌失败: {failed_count} | 总保存: {total_saved}")
                    message = '\n'.join(lines)
                except:
                    message = format_json_output(stdout)
                message_type = 'info'
            else:
                message = f"错误: {stderr or stdout}"
                message_type = 'error'

        elif action == 'submit':
            keywords_list = [k.strip() for k in keywords.strip().split('\n') if k.strip()]
            success_count = 0
            errors = []

            TASK_PROGRESS['running'] = True
            TASK_PROGRESS['current_keyword'] = ''
            TASK_PROGRESS['progress'] = 0
            TASK_PROGRESS['saved_count'] = 0
            TASK_PROGRESS['goods_count'] = 0
            TASK_PROGRESS['total'] = len(keywords_list)
            TASK_PROGRESS['completed'] = 0
            TASK_PROGRESS['errors'] = []
            TASK_PROGRESS['mode'] = task_mode
            TASK_PROGRESS['interval_seconds'] = interval_seconds
            TASK_PROGRESS['next_run_time'] = None

            devices_data = load_devices()
            all_devices = devices_data.get('devices', [])

            if distribute_mode == 'multi' and target_devices:
                selected_devices_info = [d for d in all_devices if d['id'] in target_devices]
                if not selected_devices_info:
                    message = "错误: 请选择至少一个目标设备"
                    message_type = 'error'
                    TASK_PROGRESS['running'] = False
                else:
                    device_assignments = {}
                    for i, keyword in enumerate(keywords_list):
                        target_idx = i % len(selected_devices_info)
                        target_device = selected_devices_info[target_idx]
                        device_id = target_device['id']
                        if device_id not in device_assignments:
                            device_assignments[device_id] = []
                        device_assignments[device_id].append(keyword)

                    import threading
                    results_lock = threading.Lock()
                    device_results = {}

                    def run_device_tasks(device_info, kws):
                        dev_host = device_info['host']
                        dev_user = device_info['user']
                        dev_password = device_info['password']
                        dev_port = device_info.get('port', 22)
                        dev_results = []

                        for keyword in kws:
                            task_args = ['task', 'collect', '--keyword', keyword, '--count', count, '--fresh-start']
                            if sort_by:
                                task_args.extend(['--sort-by', sort_by])
                            if price_min:
                                task_args.extend(['--price-min', str(price_min)])
                            if price_max:
                                task_args.extend(['--price-max', str(price_max)])

                            stdout, stderr, code = run_pddctl(task_args, dev_host, dev_user, dev_password, dev_port)

                            task_id = None
                            try:
                                result = json.loads(stdout)
                                if result.get('ok') == True:
                                    task_id = result.get('data', {}).get('task_id')
                            except:
                                pass

                            if not task_id:
                                dev_results.append({
                                    'keyword': keyword,
                                    'saved': 0,
                                    'attempted': 0,
                                    'status': 'failed',
                                    'error': '任务提交失败'
                                })
                                continue

                            poll_count = 0
                            max_polls = 3600
                            task_completed = False
                            saved = 0
                            attempted = 0

                            while poll_count < max_polls:
                                time.sleep(2)
                                poll_count += 1

                                status_stdout, _, status_code = run_pddctl(['task', 'status'], dev_host, dev_user, dev_password, dev_port)
                                if status_code != 0:
                                    continue

                                try:
                                    status_result = json.loads(status_stdout)
                                    task_data = status_result.get('data', {})
                                    current_task_id = task_data.get('task_id', '')
                                    state = task_data.get('state', '')
                                    source = task_data.get('source', '')

                                    if current_task_id != task_id and source == 'current':
                                        continue

                                    if state == 'completed':
                                        saved = task_data.get('saved_count', 0)
                                        attempted = task_data.get('attempted_count', 0)
                                        task_completed = True
                                        break
                                    elif state in ('failed', 'idle', 'stopped'):
                                        task_completed = True
                                        break
                                except:
                                    pass

                            if task_completed and saved > 0:
                                exp_stdout, _, exp_code = run_pddctl(['artifact', 'export'], dev_host, dev_user, dev_password, dev_port)
                                if exp_code == 0:
                                    try:
                                        exp_data = json.loads(exp_stdout)
                                        local_root = exp_data.get('data', {}).get('local_task_root', '')
                                        if local_root and os.path.isdir(local_root):
                                            cleanup_export(local_root)
                                    except:
                                        pass

                                dev_results.append({
                                    'keyword': keyword,
                                    'saved': saved,
                                    'attempted': attempted,
                                    'status': 'success',
                                    'error': ''
                                })
                            else:
                                dev_results.append({
                                    'keyword': keyword,
                                    'saved': saved,
                                    'attempted': attempted,
                                    'status': 'failed',
                                    'error': '任务未完成' if not task_completed else '超时'
                                })

                            with results_lock:
                                TASK_PROGRESS['completed'] += 1
                                TASK_PROGRESS['current_keyword'] = f"[{device_info['name']}] {keyword}"
                                TASK_PROGRESS['saved_count'] += saved
                                TASK_PROGRESS['goods_count'] += attempted
                                TASK_PROGRESS['progress'] = int((TASK_PROGRESS['completed'] / len(keywords_list)) * 100)

                        with results_lock:
                            device_results[device_info['id']] = dev_results

                    threads = []
                    for device_id, kws in device_assignments.items():
                        device_info = next((d for d in all_devices if d['id'] == device_id), None)
                        if not device_info:
                            continue
                        t = threading.Thread(target=run_device_tasks, args=(device_info, kws))
                        t.start()
                        threads.append(t)

                    for t in threads:
                        t.join()

                    message = "📡 任务已分发（并行执行）：\n\n"
                    for device_id, kws in device_assignments.items():
                        device_info = next((d for d in all_devices if d['id'] == device_id), None)
                        if not device_info:
                            continue
                        message += f"📱 {device_info['name']} ({device_info['host']}):\n"
                        results = device_results.get(device_id, [])
                        for r in results:
                            if r['status'] == 'success':
                                message += f"  ✓ {r['keyword']}: 成功 ({r['saved']}个)\n"
                            else:
                                message += f"  ✗ {r['keyword']}: 失败 ({r['error']})\n"

                    success_count = sum(1 for r in device_results.values() for res in r if res['status'] == 'success')
                    failed_count = sum(1 for r in device_results.values() for res in r if res['status'] == 'failed')

                    TASK_PROGRESS['running'] = False
                    TASK_PROGRESS['next_run_time'] = None

                    if failed_count == 0:
                        message_type = 'success'
                    elif success_count > 0:
                        message_type = 'info'
                    else:
                        message_type = 'error'

            elif inbox_path:
                for i, keyword in enumerate(keywords_list):
                    TASK_PROGRESS['current_keyword'] = keyword
                    TASK_PROGRESS['progress'] = int((i / len(keywords_list)) * 100)

                    task_args = ['task', 'collect', '--keyword', keyword, '--count', count, '--fresh-start', '--wait', '--timeout', '120']
                    if sort_by:
                        task_args.extend(['--sort-by', sort_by])
                    if price_min:
                        task_args.extend(['--price-min', str(price_min)])
                    if price_max:
                        task_args.extend(['--price-max', str(price_max)])

                    stdout, stderr, code = run_pddctl(task_args, host, user, password, port)

                    saved = 0
                    attempted = 0
                    try:
                        result = json.loads(stdout)
                        if result.get('ok') == True:
                            data = result.get('data', {})
                            saved = data.get('saved_count', 0)
                            attempted = data.get('attempted_count', 0)
                            TASK_PROGRESS['saved_count'] += saved
                            TASK_PROGRESS['goods_count'] += attempted

                            exp_stdout, _, exp_code = run_pddctl(['artifact', 'export'], host, user, password, port)
                            if exp_code == 0:
                                try:
                                    exp_data = json.loads(exp_stdout)
                                    local_root = exp_data.get('data', {}).get('local_task_root', '')
                                    if local_root and os.path.isdir(local_root):
                                        cleanup_export(local_root)
                                except:
                                    pass

                            message += f"✓ {keyword}: 成功 ({saved}个)\n"
                            success_count += 1
                        else:
                            error_msg = result.get('error', result.get('message', '未知错误'))
                            message += f"✗ {keyword}: 失败 ({error_msg})\n"
                            errors.append(f"{keyword}: {error_msg}")
                            TASK_PROGRESS['errors'].append(f"{keyword}: {error_msg}")
                    except:
                        if code == 0:
                            message += f"✓ {keyword}: 完成\n"
                            success_count += 1
                        else:
                            message += f"✗ {keyword}: 失败\n"
                            errors.append(f"{keyword}: 命令失败")
                            TASK_PROGRESS['errors'].append(f"{keyword}: 命令失败")

                    TASK_PROGRESS['completed'] = i + 1
                    TASK_PROGRESS['progress'] = int(((i + 1) / len(keywords_list)) * 100)

                    if task_mode == 'interval' and i < len(keywords_list) - 1:
                        TASK_PROGRESS['next_run_time'] = f"{interval_seconds}秒后"
                        time.sleep(interval_seconds)

            TASK_PROGRESS['running'] = False
            TASK_PROGRESS['next_run_time'] = None

            if errors:
                message += "\n✗ 失败任务:\n" + '\n'.join(errors)
                message_type = 'error'
            elif success_count > 0:
                message = f"✓ 任务完成 ({success_count}/{len(keywords_list)})\n" + message
                message_type = 'success'

    return render_template_string(TEMPLATE,
        message=message,
        message_type=message_type,
        config=config,
        devices=devices,
        selected_id=devices_data.get('selected_id'),
        keywords=keywords,
        count=count,
        sort_by=sort_by,
        price_min=price_min,
        price_max=price_max,
        review_min=review_min,
        task_mode=task_mode,
        interval_seconds=interval_seconds,
        distribute_mode=distribute_mode
    )

@app.route('/device/add', methods=['POST'])
def device_add():
    name = request.form.get('name')
    host = request.form.get('host')
    user = request.form.get('user')
    password = request.form.get('password')

    paths = discover_device_paths(host, user, password)

    if not paths:
        return f"错误: 未找到 PDDGoodsData 路径，请确认插件已安装<br><a href='/'>返回</a>"

    if len(paths) == 1:
        device_id = str(uuid.uuid4())[:8]
        inbox_path = paths[0] + '/commands/inbox'

        devices_data = load_devices()
        devices_data['devices'].append({
            'id': device_id,
            'name': name,
            'host': host,
            'user': user,
            'password': password,
            'inbox_path': inbox_path
        })
        devices_data['selected_id'] = device_id
        save_devices(devices_data)

        return redirect('/')

    return f'''
    <html>
    <head><title>选择路径</title></head>
    <body>
        <h3>找到 {len(paths)} 个路径，请选择：</h3>
        <form method="POST" action="/device/save">
            <input type="hidden" name="name" value="{name}">
            <input type="hidden" name="host" value="{host}">
            <input type="hidden" name="user" value="{user}">
            <input type="hidden" name="password" value="{password}">
            <select name="inbox_path" style="width:100%;padding:10px;">
            {''.join([f'<option value="{p}/commands/inbox">{p}/commands/inbox</option>' for p in paths])}
            </select>
            <br><br>
            <button type="submit" style="padding:10px 20px;">保存</button>
        </form>
        <br><a href='/'>返回</a>
    </body>
    </html>
    '''

@app.route('/device/save', methods=['POST'])
def device_save():
    name = request.form.get('name')
    host = request.form.get('host')
    user = request.form.get('user')
    password = request.form.get('password')
    inbox_path = request.form.get('inbox_path')

    device_id = str(uuid.uuid4())[:8]

    devices_data = load_devices()
    devices_data['devices'].append({
        'id': device_id,
        'name': name,
        'host': host,
        'user': user,
        'password': password,
        'inbox_path': inbox_path
    })
    devices_data['selected_id'] = device_id
    save_devices(devices_data)

    return redirect('/')

@app.route('/device/select/<device_id>', methods=['POST'])
def device_select(device_id):
    devices_data = load_devices()
    devices_data['selected_id'] = device_id
    save_devices(devices_data)
    return ''

DEFAULT_SETTINGS_FILE = '/Users/leeekin/Desktop/device/default_settings.json'

def load_default_settings():
    if os.path.exists(DEFAULT_SETTINGS_FILE):
        with open(DEFAULT_SETTINGS_FILE, 'r') as f:
            return json.load(f)
    return {}

import glob as glob_module
@app.route('/admin/clear-ssh', methods=['POST'])
def admin_clear_ssh():
    import subprocess
    temp_dir = '/var/folders'
    pattern = os.path.join(temp_dir, '**', 'pddctl_*.lock')
    removed = 0
    for f in glob_module.glob(pattern, recursive=True):
        try:
            os.remove(f)
            removed += 1
        except:
            pass
    pattern2 = os.path.join(temp_dir, '**', 'pddctl_*.sock')
    for f in glob_module.glob(pattern2, recursive=True):
        try:
            os.remove(f)
            removed += 1
        except:
            pass
    return json.dumps({'ok': True, 'message': f'已清理 {removed} 个残留文件'})

@app.route('/settings/save', methods=['POST'])
def settings_save():
    settings = {
        'count': request.form.get('count', '50'),
        'sort_by': request.form.get('sort_by', ''),
        'price_min': request.form.get('price_min', ''),
        'price_max': request.form.get('price_max', ''),
        'review_min': request.form.get('review_min', ''),
        'task_mode': request.form.get('task_mode', 'once'),
        'interval_seconds': request.form.get('interval_seconds', '300')
    }
    with open(DEFAULT_SETTINGS_FILE, 'w') as f:
        json.dump(settings, f, indent=2)
    return json.dumps({'ok': True, 'message': '默认设置已保存'})

@app.route('/device/delete/<device_id>', methods=['POST'])
def device_delete(device_id):
    devices_data = load_devices()
    devices_data['devices'] = [d for d in devices_data['devices'] if d['id'] != device_id]
    if devices_data.get('selected_id') == device_id:
        devices_data['selected_id'] = devices_data['devices'][0]['id'] if devices_data['devices'] else None
    save_devices(devices_data)
    return ''

if __name__ == '__main__':
    print("=" * 50)
    print("PDD 数据采集控制台 (多设备版)")
    print("=" * 50)
    print(f"使用 pddctl: {PDDCTL_PATH}")
    print(f"设备配置: {DEVICE_REGISTRY_FILE}")
    print()
    print("启动服务: http://localhost:8080")
    print("=" * 50)

    app.run(host='0.0.0.0', port=8080, debug=True)
