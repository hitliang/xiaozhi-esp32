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

REM Use ESP-IDF bundled cmake (3.30.2) instead of system cmake (4.x)
set PATH=!IDF_TOOLS_PATH!\tools\cmake\3.30.2\bin;!PATH!
set CMAKE_EXECUTABLE=!IDF_TOOLS_PATH!\tools\cmake\3.30.2\bin\cmake.exe

set IDF_TARGET=esp32s3

call !IDF_PATH!\export.bat >nul 2>nul

cd /d C:\code\esp32\xiaozhi-esp32

if "%1"=="" (
    set ARGS=build
) else (
    set ARGS=%1 %2 %3 %4
)

echo ===== xiaozhi-esp32: idf.py %ARGS% =====
idf.py %ARGS%
echo ===== DONE (exit code: %ERRORLEVEL%) =====

endlocal
