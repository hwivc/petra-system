@echo off
title Petra Virtual ePaper Display (800x480)
cd /d "%~dp0"
echo ===================================================
echo   Petra Virtual ePaper Display (PlastiBytes)
echo ===================================================
echo.
echo Starting virtual display window and LAN services...
python run_virtual_display.py %*
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Virtual display exited with error code %ERRORLEVEL%.
    pause
)

