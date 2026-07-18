# xiaozhi-esp32 · 小智AI 聊天机器人

基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 定制，当前版本 **2.3.0**，针对 **微雪 ESP32-S3-Touch-AMOLED-1.8** 开发板优化。

## 硬件

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-S3 (QFN56) |
| PSRAM | 8MB (Octal) |
| Flash | 16MB |
| 屏幕 | 1.8" AMOLED 触屏 |
| IMU | QMI8658 (可选) |
| 音频 | I2S 麦克风 + 功放 |

👉 [微雪官方购买链接](https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.8.htm)

## 内置应用

| 应用 | 功能 |
|------|------|
| 小智AI | WebSocket 语音助手，支持 LLM 驱动的 21 种动态表情 |
| 设置 | WiFi、电量、固件版本、硬件信息 |
| 天气 | 高德 API 实时天气 + 3 天预报 |
| 姿态仪 | 20 FPS 气泡水平仪、圆形限位、水平判定、一键归零 |
| 弹球物理 | 重力小球碰撞模拟 |
| 练耳 | 高低/相同音高答题、计分、连胜、三级自适应难度 |
| 节拍器 | BPM / 拍号 / 语音数拍 |

## 编译与烧录

### 环境要求

- **IDF**: ESP-IDF v5.5.4 (`C:\esp\v5.5.4\esp-idf`)
- **芯片目标**: `esp32s3`
- **Windows 注意**: ESP-IDF v5.5+ 不支持 MSYS2/MinGW，需用项目自带脚本

### 编译

```bash
bash tools/build.sh build
```

### 烧录

```bash
bash tools/build.sh -p COM端口 flash
```

2.2.x 及更早版本尚未接入自建 OTA，需要通过 USB 烧录一次 2.3.0。
从 2.3.0 开始，设备每次联网会检查自建 OTA 服务，后续版本可直接在线升级。

### OTA 安全机制

- 固件写入未运行的 OTA 分区，不覆盖当前可用版本；
- 下载前后核对 Content-Length 和服务端声明大小；
- 流式校验 SHA-256；
- 核对 ESP 应用项目名和内嵌版本；
- 新固件未成功启动时自动回滚；
- OTA 服务不可用时快速放行，不影响语音功能。

### 编译踩坑

常见问题及解决方案参见 [CLAUDE.md](CLAUDE.md)。

## 协议文档

- [WebSocket 通信协议](docs/websocket.md)
- [MQTT + UDP 混合通信协议](docs/mqtt-udp.md)
- [MCP 协议交互流程](docs/mcp-protocol.md)
- [MCP 物联网控制用法](docs/mcp-usage.md)
- [Reddy OTA 升级与回滚](docs/reddy-ota.md)

## 服务器部署

Reddy 定制服务端源码位于本仓库的
[`reddy-server`](https://github.com/hitliang/xiaozhi-esp32/tree/reddy-server)
分支，包含提示词与记忆优化、LLM 表情协议、管理页面和 OTA 发布服务。

其他可选服务端：

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Python
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Java
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Golang
- [hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang) Golang

## 上游项目

- [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — 小智AI 主仓库，支持 70+ 硬件
- [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) — 自定义唤醒词/字体/表情生成器

## License

MIT
