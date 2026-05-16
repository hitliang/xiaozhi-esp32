# xiaozhi-esp32 · 小智AI 聊天机器人

基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.2.6，针对 **微雪 ESP32-S3-Touch-AMOLED-1.8** 开发板的个性化定制版本。

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
| 小智AI | WebSocket 语音助手，唤醒词交互，支持 Qwen/DeepSeek 大模型 |
| 设置 | WiFi、电量、固件版本、硬件信息 |
| 天气 | 高德 API 实时天气 + 3 天预报 |
| 姿态仪 | Roll/Pitch 弧表 + 气泡水平仪 |
| 弹球物理 | 重力小球碰撞模拟 |
| 贪吃蛇 | 重力方向操控 |
| 练耳 | 音程识别训练 |
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

### 编译踩坑

常见问题及解决方案参见 [CLAUDE.md](CLAUDE.md)。

## 协议文档

- [WebSocket 通信协议](docs/websocket.md)
- [MQTT + UDP 混合通信协议](docs/mqtt-udp.md)
- [MCP 协议交互流程](docs/mcp-protocol.md)
- [MCP 物联网控制用法](docs/mcp-usage.md)

## 服务器部署

本项目默认接入 [xiaozhi.me](https://xiaozhi.me) 官方服务器。如需自建服务器：

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Python
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Java
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Golang
- [hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang) Golang

## 上游项目

- [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — 小智AI 主仓库，支持 70+ 硬件
- [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) — 自定义唤醒词/字体/表情生成器

## License

MIT
