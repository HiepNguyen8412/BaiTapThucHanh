@echo off
setlocal
set FILE=%~1
if "%FILE%"=="" set FILE=%SystemRoot%\win.ini

set CLIENT=%~dp0x64\Debug\client.exe
if not exist "%CLIENT%" set CLIENT=%~dp0x64\Release\client.exe

if not exist "%CLIENT%" (
  echo client.exe not found. Build the solution first.
  exit /b 1
)

echo [1] First scan - expected cache MISS
"%CLIENT%" scan "%FILE%" --priority high

echo.
echo [2] Second scan - expected CACHE HIT
"%CLIENT%" scan "%FILE%" --priority high

echo.
echo [3] Stress 20 jobs
"%CLIENT%" stress "%FILE%" --count 20 --priority low

endlocal
