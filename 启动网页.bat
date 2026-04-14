@echo off
title PDD Controller
cd /d "%~dp0"
echo ==================================================
echo  PDD Data Collection Controller
echo ==================================================
echo.
pip install flask >nul 2>&1
echo Starting service...
python pdd_controller.py
pause
