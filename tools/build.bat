@echo off
REM ============================================================
REM xiaozhi-esp32 Build Script for ESP-IDF v5.5.4
REM
REM Usage (from MSYS2/bash):
REM   cmd.exe /c "set MSYSTEM= && C:\code\xiaozhi-esp32-main\tools\build.bat [command]"
REM
REM Commands: build, clean, fullclean, flash, monitor, menuconfig
REM ============================================================
setlocal enabledelayedexpansion

set MSYSTEM=
set TERM=

set IDF_PATH=d:\program\idf\v5.5.4\esp-idf
set IDF_TOOLS_PATH=C:\Users\reddy\.espressif

call %IDF_PATH%\export.bat >nul 2>nul

cd /d D:\amoled_new

if "%1"=="" (
    set ARGS=build
) else (
    set ARGS=%1 %2 %3 %4
)

echo ===== xiaozhi-esp32: idf.py %ARGS% =====
idf.py %ARGS%
echo ===== DONE (exit code: %ERRORLEVEL%) =====

endlocal
