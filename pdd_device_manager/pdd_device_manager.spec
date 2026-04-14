# -*- mode: python ; coding: utf-8 -*-

import sys
import os
from PyInstaller.building.build_main import Analysis, PYZ, EXE, BUNDLE, COLLECT

block_cipher = None

# 获取 PyQt6 路径
import PyQt6
pyqt6_path = os.path.dirname(PyQt6.__file__)

a = Analysis(
    ['main.py'],
    pathex=['/Users/leeekin/Desktop/device/pdd_device_manager'],
    binaries=[],
    datas=[
        ('../pdd-ios-device-collect', 'pdd-ios-device-collect'),
    ],
    hiddenimports=[
        'PyQt6',
        'PyQt6.QtCore',
        'PyQt6.QtGui',
        'PyQt6.QtWidgets',
        'PyQt6.sip',
        'paramiko',
        'paramiko.transport',
        'paramiko.rsakey',
        'paramiko.ecdsakey',
        'paramiko.ed25519key',
        'paramiko.sftp_client',
        'paramiko.ssh_exception',
        'bcrypt',
        'cryptography',
        'nacl',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='PDD Device Manager',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='PDD Device Manager'
)

app = BUNDLE(
    coll,
    name='PDD Device Manager.app',
    icon=None,
    bundle_identifier='com.pdd.devicemanager',
    info_plist={
        'CFBundleShortVersionString': '1.0.0',
        'CFBundleVersion': '1.0.0',
        'NSHighResolutionCapable': 'True',
        'LSMinimumSystemVersion': '11.0',
        'CFBundleExecutable': 'PDD Device Manager',
    },
)
