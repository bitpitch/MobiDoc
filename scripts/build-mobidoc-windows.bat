@echo off
setlocal
call "%~dp0build-windows.bat" %*
if errorlevel 1 exit /b 1
if /I "%~1"=="-Run" exit /b 0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1"
exit /b %errorlevel%
