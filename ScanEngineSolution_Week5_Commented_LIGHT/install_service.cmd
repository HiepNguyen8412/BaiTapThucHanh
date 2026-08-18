@echo off
net session >nul 2>&1
if errorlevel 1 (
  echo Please run this script as Administrator.
  pause
  exit /b 1
)
cd /d "%~dp0x64\Debug"
ScanService.exe install
sc start AvScanService
sc query AvScanService
pause
