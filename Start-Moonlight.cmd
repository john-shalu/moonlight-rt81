@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Compatibility.ps1"
if errorlevel 1 (
  echo.
  echo Please keep compatibility-result.json and send it back.
  pause
  exit /b 1
)
start "" "%~dp0MoonlightRT81.exe"
