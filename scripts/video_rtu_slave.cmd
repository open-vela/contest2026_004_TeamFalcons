@echo off
rem Launch the VelaGuard video RS485 slave simulator.
rem Usage: video_rtu_slave.cmd -Port COM11 [-Auto] [-AutoDelaySec 5]
rem Double-click without arguments to list COM ports and prompt for one.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0video_rtu_slave.ps1" %*
if errorlevel 1 pause
endlocal
