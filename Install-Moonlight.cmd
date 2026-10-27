@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Moonlight.ps1"
if errorlevel 1 (
  echo.
  echo Installation did not finish. You can still use Start-Moonlight.cmd.
  pause
)
