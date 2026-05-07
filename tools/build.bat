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

set IDF_PATH=C:\esp\v5.5.4\esp-idf
set IDF_TOOLS_PATH=C:\Users\john\.espressif

call %IDF_PATH%\export.bat >nul 2>nul

cd /d C:\code\xiaozhi-esp32-main

if "%1"=="" (
    set CMD=build
) else (
    set CMD=%1
)

echo ===== xiaozhi-esp32: idf.py %CMD% =====
idf.py %CMD%
echo ===== DONE (exit code: %ERRORLEVEL%) =====

endlocal
