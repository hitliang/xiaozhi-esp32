# xiaozhi-esp32 · XiaoZhi AI Multi-App Platform

(English | [中文](README_zh.md) | [日本語](README_ja.md))

## Introduction

👉 [Human: Give AI a camera vs AI: Instantly finds out the owner hasn't washed hair for three days【bilibili】](https://www.bilibili.com/video/BV1bpjgzKEhd/)

👉 [Handcraft your AI girlfriend, beginner's guide【bilibili】](https://www.bilibili.com/video/BV1XnmFYLEJN/)

XiaoZhi AI is a voice interaction chatbot that leverages large models like Qwen / DeepSeek, with multi-device control via the MCP protocol. It has now evolved into a **multi-app platform** — adding a home screen + 3×3 app grid on top of the core AI voice assistant.

<img src="docs/mcp-based-graph.jpg" alt="Control everything via MCP" width="320">

### Version Notes

The current v2 version is incompatible with the v1 partition table. OTA upgrade from v1 to v2 is not supported. See [partitions/v2/README.md](partitions/v2/README.md) for details.

All v1 hardware can upgrade to v2 by manually flashing the firmware.

The stable v1 version is 1.9.2. Switch to v1 via `git checkout v1`. The v1 branch is maintained until February 2026.

### Multi-App Launcher

v2.2.6+ introduces a **multi-app launcher system**:

- **Home Screen**: Pure black OLED background, large clock display with date, top bar showing WiFi signal and battery status
- **App Grid**: Swipe left/right to access a 3×3 icon grid, tap to launch apps
- **Global Button**: BOOT button — single click exits app / returns home / enters sleep (black screen)

Built-in apps:

| App | Description | Status |
|------|-------------|--------|
| 🤖 **XiaoZhi AI** | WebSocket voice assistant, auto-listening on entry | ✅ Done |
| ⚙️ **Settings** | WiFi status, IP, signal, battery, firmware, hardware info | ✅ Done |
| 🌤️ **Weather** | Amap API real-time weather + 3-day forecast | 🚧 WIP |
| 📐 **Attitude** | Roll/Pitch arc gauges + bubble level (QMI8658 IMU) | 🚧 Awaiting IMU |
| ⚾ **Ball Physics** | Gravity ball collision simulation (IMU controlled) | 🚧 Awaiting IMU |
| 🐍 **Snake** | Gravity snake game (IMU direction control) | 🚧 Awaiting IMU |
| 🎵 **Audio Test** | 440Hz reference tone | 🚧 WIP |

### Features Implemented

- Wi-Fi / ML307 Cat.1 4G
- **Multi-app launcher**: home screen + app grid + app switching framework
- Offline voice wake-up [ESP-SR](https://github.com/espressif/esp-sr)
- Two communication protocols ([Websocket](docs/websocket.md) or MQTT+UDP)
- OPUS audio codec
- Streaming ASR + LLM + TTS voice interaction
- Speaker recognition [3D Speaker](https://github.com/modelscope/3D-Speaker)
- OLED / LCD display with emoji support
- **AMOLED pure black theme** (OLED-friendly, power-saving)
- Battery display and power management
- Multi-language support (Chinese, English, Japanese, 40+ languages)
- ESP32-C3, ESP32-S3, ESP32-P4 chip platforms
- Device-side MCP for device control (Volume, LED, Servo, GPIO, etc.)
- Cloud-side MCP for extended capabilities (Smart home, PC control, knowledge search, email, etc.)
- Customizable wake words, fonts, emojis, and backgrounds via [Custom Assets Generator](https://github.com/78/xiaozhi-assets-generator)

## Hardware

### DIY Breadboard

See the Feishu document tutorial:

👉 ["XiaoZhi AI Chatbot Encyclopedia"](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb?from=from_copylink)

Breadboard demo:

![Breadboard Demo](docs/v1/wiring2.jpg)

### Supports 70+ Open Source Hardware (Partial List)

- <a href="https://oshwhub.com/li-chuang-kai-fa-ban/li-chuang-shi-zhan-pai-esp32-s3-kai-fa-ban" target="_blank" title="LiChuang ESP32-S3 Development Board">LiChuang ESP32-S3 Dev Board</a>
- <a href="https://github.com/espressif/esp-box" target="_blank" title="Espressif ESP32-S3-BOX3">Espressif ESP32-S3-BOX3</a>
- <a href="https://docs.m5stack.com/zh_CN/core/CoreS3" target="_blank" title="M5Stack CoreS3">M5Stack CoreS3</a>
- <a href="https://docs.m5stack.com/en/atom/Atomic%20Echo%20Base" target="_blank" title="AtomS3R + Echo Base">M5Stack AtomS3R + Echo Base</a>
- <a href="https://gf.bilibili.com/item/detail/1108782064" target="_blank" title="Magic Button 2.4">Magic Button 2.4</a>
- <a href="https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.8.htm" target="_blank" title="Waveshare ESP32-S3-Touch-AMOLED-1.8">Waveshare ESP32-S3-Touch-AMOLED-1.8</a>
- <a href="https://github.com/Xinyuan-LilyGO/T-Circle-S3" target="_blank" title="LILYGO T-Circle-S3">LILYGO T-Circle-S3</a>
- <a href="https://oshwhub.com/tenclass01/xmini_c3" target="_blank" title="XiaGe Mini C3">XiaGe Mini C3</a>
- <a href="https://oshwhub.com/movecall/cuican-ai-pendant-lights-up-y" target="_blank" title="Movecall CuiCan ESP32S3">CuiCan AI Pendant</a>
- <a href="https://github.com/WMnologo/xingzhi-ai" target="_blank" title="WMnologo-Xingzhi-1.54">WMnologo-Xingzhi-1.54TFT</a>
- <a href="https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html" target="_blank" title="SenseCAP Watcher">SenseCAP Watcher</a>
- <a href="https://www.bilibili.com/video/BV1BHJtz6E2S/" target="_blank" title="ESP-HI Low Cost Robot Dog">ESP-HI Robot Dog</a>

<div style="display: flex; justify-content: space-between;">
  <a href="docs/v1/lichuang-s3.jpg" target="_blank" title="LiChuang ESP32-S3 Dev Board">
    <img src="docs/v1/lichuang-s3.jpg" width="240" />
  </a>
  <a href="docs/v1/espbox3.jpg" target="_blank" title="Espressif ESP32-S3-BOX3">
    <img src="docs/v1/espbox3.jpg" width="240" />
  </a>
  <a href="docs/v1/m5cores3.jpg" target="_blank" title="M5Stack CoreS3">
    <img src="docs/v1/m5cores3.jpg" width="240" />
  </a>
  <a href="docs/v1/atoms3r.jpg" target="_blank" title="AtomS3R + Echo Base">
    <img src="docs/v1/atoms3r.jpg" width="240" />
  </a>
  <a href="docs/v1/magiclick.jpg" target="_blank" title="Magic Button 2.4">
    <img src="docs/v1/magiclick.jpg" width="240" />
  </a>
  <a href="docs/v1/waveshare.jpg" target="_blank" title="Waveshare ESP32-S3-Touch-AMOLED-1.8">
    <img src="docs/v1/waveshare.jpg" width="240" />
  </a>
  <a href="docs/v1/lilygo-t-circle-s3.jpg" target="_blank" title="LILYGO T-Circle-S3">
    <img src="docs/v1/lilygo-t-circle-s3.jpg" width="240" />
  </a>
  <a href="docs/v1/xmini-c3.jpg" target="_blank" title="XiaGe Mini C3">
    <img src="docs/v1/xmini-c3.jpg" width="240" />
  </a>
  <a href="docs/v1/movecall-cuican-esp32s3.jpg" target="_blank" title="CuiCan">
    <img src="docs/v1/movecall-cuican-esp32s3.jpg" width="240" />
  </a>
  <a href="docs/v1/wmnologo_xingzhi_1.54.jpg" target="_blank" title="WMnologo-Xingzhi-1.54">
    <img src="docs/v1/wmnologo_xingzhi_1.54.jpg" width="240" />
  </a>
  <a href="docs/v1/sensecap_watcher.jpg" target="_blank" title="SenseCAP Watcher">
    <img src="docs/v1/sensecap_watcher.jpg" width="240" />
  </a>
  <a href="docs/v1/esp-hi.jpg" target="_blank" title="ESP-HI Low Cost Robot Dog">
    <img src="docs/v1/esp-hi.jpg" width="240" />
  </a>
</div>

## Software

### Firmware Flashing

For beginners, use the pre-built firmware without setting up a development environment. The firmware connects to the official [xiaozhi.me](https://xiaozhi.me) server by default.

👉 [Beginner's Firmware Flashing Guide](https://ccnphfhqs21z.feishu.cn/wiki/Zpz4wXBtdimBrLk25WdcXzxcnNS)

### Development Environment

- Cursor or VSCode
- Install ESP-IDF plugin, select SDK version 5.4 or above
- Linux is recommended over Windows for faster compilation and fewer driver issues
- This project uses Google C++ code style

**Windows users**: ESP-IDF v5.5+ no longer supports MSYS2/MinGW. Use the provided build scripts:

```bash
# Build
bash tools/build.sh build

# Flash (specify port)
bash tools/build.sh flash -p COM54
```

### Developer Documentation

- [Custom Board Guide](docs/custom-board.md)
- [MCP Protocol IoT Control Usage](docs/mcp-usage.md)
- [MCP Protocol Interaction Flow](docs/mcp-protocol.md)
- [MQTT + UDP Hybrid Communication Protocol](docs/mqtt-udp.md)
- [WebSocket Communication Protocol](docs/websocket.md)

## Server Configuration

Log in to [xiaozhi.me](https://xiaozhi.me) console to configure your device.

👉 [Backend Tutorial Video](https://www.bilibili.com/video/BV1jUCUY2EKM/)

## Related Projects

Server deployments:

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Python server
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Java server
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Golang server
- [hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang) Golang server

Third-party clients:

- [huangjunsen0406/py-xiaozhi](https://github.com/huangjunsen0406/py-xiaozhi) Python client
- [TOM88812/xiaozhi-android-client](https://github.com/TOM88812/xiaozhi-android-client) Android client
- [100askTeam/xiaozhi-linux](http://github.com/100askTeam/xiaozhi-linux) Linux client
- [78/xiaozhi-sf32](https://github.com/78/xiaozhi-sf32) Bluetooth chip firmware
- [QuecPython/solution-xiaozhiAI](https://github.com/QuecPython/solution-xiaozhiAI) QuecPython firmware

Custom Assets:

- [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) Custom Assets Generator

## About

An open-source ESP32 project under the MIT license. Free for personal and commercial use.

We hope this project helps everyone understand AI hardware development and apply large language models to real hardware devices.

For ideas or suggestions, please raise Issues or join our [Discord](https://discord.gg/C759fGMBcZ) or QQ group: 1011329060

## Star History

<a href="https://star-history.com/#78/xiaozhi-esp32&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date&theme=dark" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
 </picture>
</a>
