@echo off
set FILE=%SystemRoot%\System32\notepad.exe
cd /d "%~dp0x64\Debug"
echo === First scan ===
client.exe scan "%FILE%" --priority high
echo.
echo === Second scan: cache fast path ===
client.exe scan "%FILE%" --priority high
echo.
echo === Stress 20 jobs ===
client.exe stress "%FILE%" --count 20 --priority low
echo.
echo === Telemetry ===
client.exe telemetry
pause
