# OraOS

A smartphone-style firmware "OS" for ESP32 boards. Plug in a touch TFT and an SD card and use the board like a tiny phone: a launcher, a pull-down quick-settings shade, a file browser, a music player and games.

It is firmware that behaves like an OS: FreeRTOS provides scheduling, LVGL provides the UI, and the apps and services are built into the image.

**Status:** M0, toolchain and boot banner (work in progress)

## Target hardware

| Item | Current target |
|---|---|
| Board | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) |
| Display | SPI TFT with capacitive touch (TBD) |
| Storage | microSD |
| Audio | I2S DAC (PCM5102A) |

Other ESP32 boards will be supported through board profiles once the S3 build works.

> The ESP32-S3 has BLE only (no Bluetooth Classic), so Bluetooth audio (A2DP) is not available on this target.

## Planned features

- LVGL home screen with an app launcher
- Status bar (time, Wi-Fi, BLE, battery)
- Pull-down shade with Wi-Fi/BLE toggles, brightness and volume
- SD card hot-insert, file manager
- Music player (WAV, MP3, then FLAC) over I2S
- Settings app (Wi-Fi scan and connect, brightness, time)
- Simple games (Snake, 2048, ...)
- OTA updates, crash logs, power management

## Architecture

```
apps/        music, files, settings, games
shell/       status bar, launcher, shade, app manager
services/    storage, audio, network, settings, power, event bus
drivers/     display, touch, sdcard, i2s, backlight, battery
board/       per-board pin maps and capability flags
```

- Apps never call drivers directly. Services talk to the UI through an event bus (FreeRTOS queues).
- Only the UI task touches LVGL.
- Anything board-specific lives in `src/board/` and is selected with a build flag. Code checks capability flags (`board::hasBtClassic`, ...), not chip names.

## Memory model

- **Internal RAM:** DMA buffers, task stacks, radio stacks
- **PSRAM:** audio ring buffer, file indexes, decoded images, large LVGL allocations
- An app creates its LVGL screen on open and deletes it on close

## Project layout

```
oraos/
├── platformio.ini
├── sdkconfig.defaults
├── partitions.csv
├── CMakeLists.txt
└── src/
    ├── CMakeLists.txt
    ├── config.h
    ├── main.cpp
    ├── board/
    └── system/
```

## Build and flash

Requirements: VS Code with the PlatformIO extension.

1. Clone the repo and open it in VS Code.
2. Connect the board to the USB-C port labelled **UART**.
3. Run PlatformIO: **Build**, **Upload**, then **Monitor**.

Expected output at boot: flash 16 MB, about 8 MB PSRAM free.

**Troubleshooting:** if PSRAM shows 0 KB, delete `sdkconfig.esp32s3` and `.pio/`, then rebuild.

## Flash layout (16 MB)

| Partition | Size | Purpose |
|---|---|---|
| nvs | 24 KB | settings |
| otadata / phy_init | 12 KB | OTA state, RF calibration |
| coredump | 64 KB | crash dumps |
| ota_0 / ota_1 | 5 MB each | dual firmware slots |
| storage | 4 MB | assets (LittleFS) |

## Roadmap

- [x] M0: toolchain, board profile, boot banner
- [ ] M1: display + touch + LVGL running
- [ ] M2: shell (status bar, launcher, app manager, shade)
- [ ] M3: SD storage service + file manager
- [ ] M4: Wi-Fi/BLE services + settings persistence
- [ ] M5: audio service + music player
- [ ] M6: games
- [ ] M7: power management, OTA, logs
- [ ] M8: port to a second board

## Conventions

- Modular code: one `.h` and `.cpp` per feature, namespaced, minimal `main`
- `config.h` and the board header are the single source of truth for pins and constants
- Commit at the end of each milestone

## License

TBD