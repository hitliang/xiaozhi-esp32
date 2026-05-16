# xiaozhi-esp32 · XiaoZhi AI Chatbot

(English | [中文](README_zh.md))

A customized fork of [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.2.6, targeting the **Waveshare ESP32-S3-Touch-AMOLED-1.8** board.

## Hardware

| Item | Spec |
|------|------|
| MCU | ESP32-S3 (QFN56) |
| PSRAM | 8MB (Octal) |
| Flash | 16MB |
| Display | 1.8" AMOLED touch |
| IMU | QMI8658 (optional) |
| Audio | I2S mic + amplifier |

## Built-in Apps

| App | Description |
|------|-------------|
| XiaoZhi AI | WebSocket voice assistant with wake word, supports Qwen/DeepSeek |
| Settings | WiFi, battery, firmware version, hardware info |
| Weather | Amap API real-time weather + 3-day forecast |
| Attitude | Roll/Pitch arc gauges + bubble level |
| Ball Physics | Gravity ball collision simulation |
| Snake | Gravity-direction snake game |
| Ear Trainer | Musical interval recognition |
| Metronome | BPM / time signatures / voice counting |

## Build & Flash

### Requirements

- **IDF**: ESP-IDF v5.5.4
- **Target**: `esp32s3`
- **Windows**: ESP-IDF v5.5+ rejects MSYS2/MinGW — use the bundled build scripts

### Build

```bash
bash tools/build.sh build
```

### Flash

```bash
bash tools/build.sh -p COMx flash
```

### Common pitfalls

See [CLAUDE.md](CLAUDE.md) for known build issues and fixes.

## Protocol Docs

- [WebSocket Protocol](docs/websocket.md)
- [MQTT + UDP Protocol](docs/mqtt-udp.md)
- [MCP Protocol Flow](docs/mcp-protocol.md)
- [MCP IoT Usage](docs/mcp-usage.md)

## Server

Default server: [xiaozhi.me](https://xiaozhi.me). Self-hosted alternatives:

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Python
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Java
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Golang
- [hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang) Golang

## Upstream

- [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — Main repo, supports 70+ hardware boards
- [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) — Custom assets generator

## License

MIT
