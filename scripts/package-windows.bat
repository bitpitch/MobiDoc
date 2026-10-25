@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1" %*
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" echo Pacchettizzazione non riuscita. Controlla D:\CODICE\INSTALLER\WINDOWS\MOBIDOC\.build-windows\package.log
if "%RESULT%"=="0" echo Pacchetto MobiDoc creato in D:\CODICE\INSTALLER\WINDOWS\MOBIDOC
pause
exit /b %RESULT%
