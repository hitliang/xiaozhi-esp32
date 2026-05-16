# Version 2 Partition Table (16MB)

16MB flash partition layout for ESP32-S3-Touch-AMOLED-1.8.

## Layout

| Partition | Size | Purpose |
|-----------|------|---------|
| `nvs` | 16KB | Non-volatile storage |
| `otadata` | 8KB | OTA data |
| `phy_init` | 4KB | PHY initialization |
| `ota_0` | 4MB | Application (active) |
| `ota_1` | 4MB | Application (OTA target) |
| `assets` | 8MB | SPIFFS asset storage |

## Assets Partition

The `assets` partition stores network-loadable content:
- Wake word models
- Fonts (text and icon)
- Sound files and audio effects
- Emoji packs
- Language configuration

All content is updatable via HTTP download without reflashing.

## Migrating from v1

v1 partition table is incompatible with v2. Manual flash is required:
1. Flash the new partition table and firmware
2. Device auto-downloads required assets on first boot
