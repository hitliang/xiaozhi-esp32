# CLAUDE.md - xiaozhi-esp32

## 编译环境

- **IDF_PATH**: `C:\esp\v5.5.4\esp-idf` (ESP-IDF v5.5.4)
- **IDF_TOOLS_PATH**: `C:\Users\john\.espressif`
- **Target**: `esp32s3` (Waveshare ESP32-S3-Touch-AMOLED-1.8)
- **编译命令**: `bash tools/build.sh [build|flash|clean|fullclean|monitor]`

## 已验证的开发机环境

| 工具 | 版本 | 来源 |
|------|------|------|
| CMake | 3.30.2 | ESP-IDF 内置 (`%IDF_TOOLS_PATH%\tools\cmake\3.30.2`) |
| pyparsing | 3.1.4 | pip (需锁定此版本) |
| Python | 3.12.4 | ESP-IDF venv |
| xtensa-esp-elf-gcc | 14.2.0 | ESP-IDF tools |

## 编译踩坑记录

### 1. CMake 版本不兼容

ESP-IDF v5.5.4 不支持系统安装的 CMake 4.x。`build.bat` 中必须将 ESP-IDF 自带的 CMake 3.30.2 加到 PATH 最前面：

```bat
set PATH=%IDF_TOOLS_PATH%\tools\cmake\3.30.2\bin;%PATH%
```

### 2. pyparsing 版本必须锁定

IDF v5.5.4 要求 `pyparsing>=3.1.0,<3.3`。但实测：
- `3.2.x` — 导致 `ldgen` 工具解析 `.a` 文件时 `IndexError`
- `3.0.x` — 不满足 IDF 最低版本要求，`export.bat` 失败
- `3.1.4` — **唯一验证可用的版本**（全量编译时 ldgen 可能仍报错，增量编译正常）

```bash
pip install pyparsing==3.1.4
```

如果全量编译（`fullclean`）遇到 ldgen 报错 `Expected 'In archive'`，再执行一次增量编译即可通过。

### 3. 必须显式设置 IDF_TARGET=esp32s3

默认 target 是 `esp32`，需在 `build.bat` 中设置：
```bat
set IDF_TARGET=esp32s3
```

### 4. build 脚本路径均为硬编码

`tools/build.bat` 和 `tools/build.sh` 中的 IDF_PATH、IDF_TOOLS_PATH、项目路径都是硬编码的，换环境需手动修改。

### 5. MSYS2/MINGW 不兼容

ESP-IDF v5.5+ 会拒绝 MSYS2/MinGW 环境。`build.sh` 已处理此问题：清除 MSYSTEM 环境变量后通过 `cmd.exe` 调用 `build.bat`。
