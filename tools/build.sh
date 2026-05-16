#!/bin/bash
# ============================================================
# xiaozhi-esp32 Build Script (MSYS2/bash → cmd.exe bridge)
#
# Usage: ./tools/build.sh [build|clean|fullclean|flash|monitor|menuconfig]
# Default: build
#
# This wrapper clears the MSYSTEM variable that ESP-IDF v5.5+
# rejects, then delegates to a batch file via cmd.exe.
# ============================================================

CMD="${1:-build}"
PROJECT_DIR="C:/code/esp32/xiaozhi-esp32"
BAT_FILE="${PROJECT_DIR}/tools/build.bat"

# Clear MSYSTEM for this process tree
export MSYSTEM=""
export TERM=""

cmd.exe /c "set MSYSTEM= && set TERM= && ${BAT_FILE} ${CMD}" 2>&1
exit $?
