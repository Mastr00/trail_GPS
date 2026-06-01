<h1 align="center">TrailNav</h1>
<p align="center">An offline-first GPS companion for the ESP32-S3, with onboard maps, GPX overlays, environmental sensing, and an inertial level.</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-ESP32--S3-E7352C">
  <img alt="MCU" src="https://img.shields.io/badge/MCU-N16R8-555">
  <img alt="Framework" src="https://img.shields.io/badge/framework-Arduino-00979D">
  <img alt="Language" src="https://img.shields.io/badge/language-C%2B%2B-00599C">
  <img alt="Display" src="https://img.shields.io/badge/display-ST7789%20240%C3%97320-blueviolet">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-green">
</p>

## Overview

TrailNav is a wearable-class GPS device firmware built on the [Espressif ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3) (N16R8 — 16 MB flash, 8 MB PSRAM). It renders [OpenStreetMap](https://www.openstreetmap.org/) raster tiles directly from an SD card, overlays GPX tracks, and surfaces live telemetry from a GNSS receiver, a BMP280 barometer, an AHT20 hygrometer, and an MPU6050 IMU on a 2.0" ST7789 TFT panel driven by [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI). The codebase is a single Arduino sketch plus six focused modules (GPS, sensors, buttons, SD, map, GPX), with the LRU tile cache, GPX parser, and persistent state engineered to run fully offline.

## Features

- Multi-tile map renderer (2×2 tile window) with a 4-slot LRU tile cache in PSRAM (~512 KB).
- GPX overlay parser supporting up to 10 000 points, loaded from a chunked SD reader.
- Web Mercator projection with safe latitude clamping and panoramic pan via joystick.
- GNSS module: live coordinates, ground speed (with max speed memory), heading, trip distance via Haversine, trip duration, HDOP and satellite count.
- Automatic France CET/CEST time zone handling on GPS-derived UTC.
- Environmental page: temperature, humidity, barometric pressure, barometric altitude with GPS auto-calibration when HDOP < 2 and ≥ 6 satellites are locked.
- IMU page: real-time spirit level with anti-flicker `TFT_eSprite` double buffering and software tare.
- Anti-flicker compass with cardinal labels and shadowed needle, also via `TFT_eSprite`.
- Last-known position persistence in NVS (`Preferences`) so the map renders immediately after a cold boot without a fix.
- SD bus auto-recovery: re-initialization attempted every 10 s if the card is dropped mid-session.
- Edge-triggered button handling with 20 ms debounce on a 7-button input matrix.

## Tech stack

| Layer | Technology |
| --- | --- |
| MCU | [ESP32-S3 N16R8](https://www.espressif.com/en/products/socs/esp32-s3) (240 MHz dual-core Xtensa LX7, 16 MB flash, 8 MB PSRAM) |
| Framework | [Arduino-ESP32](https://github.com/espressif/arduino-esp32) |
| Language | C++ (Arduino dialect) |
| Display driver | [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (ST7789, 40 MHz SPI) |
| GNSS parser | [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) |
| Barometer | [Adafruit BMP280](https://github.com/adafruit/Adafruit_BMP280_Library) |
| Hygrometer | [Adafruit AHTX0](https://github.com/adafruit/Adafruit_AHTX0) |
| IMU | [Electronic Cats MPU6050](https://github.com/ElectronicCats/mpu6050) |
| Storage | [Arduino SD](https://github.com/espressif/arduino-esp32/tree/master/libraries/SD) on dedicated SPI3 bus |
| Persistent state | [Preferences](https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences) (NVS) |
| Map tiles | [OpenStreetMap](https://www.openstreetmap.org/) raster, 256×256 BMP RGB565 |

## Architecture

```
GARMINE_LIKE/
├── GARMINE_LIKE.ino   # Main sketch: UI state machine, page renderers, setup/loop
├── User_Setup.h       # TFT_eSPI configuration (ST7789, pinout, SPI 40 MHz)
├── gps.h / gps.cpp    # NMEA decoding via TinyGPSPlus, trip stats, CET/CEST
├── sensors.h / .cpp   # BMP280 + AHT20 + MPU6050 init, filtering, baro calibration
├── buttons.h / .cpp   # 7-button matrix, edge-triggered debounced input
├── sdcard.h / .cpp    # SPI3 bus init, SD mount, auto-recovery
├── map.h / .cpp       # Web Mercator projection, multi-tile renderer, LRU cache
└── gpx.h / .cpp       # Chunked GPX parser (up to 10 000 points in PSRAM)
```

UI state flow:

```
                ┌──────────────┐
                │   PAGE_MENU  │◀─────────┐
                └──────┬───────┘          │
                       │ SET / MID / RIGHT│ LEFT / RST
              ┌────────┼────────┬─────────┼─────────┐
              ▼        ▼        ▼         ▼         ▼
        PAGE_GPS  PAGE_ENV  PAGE_IMU  PAGE_COMPASS  PAGE_MAP
                                                     │
                                                     └─ UP/DOWN: zoom 13–15
                                                        MID/SET: pan N/S
                                                        L/R:     pan W/E
```

Request flow on the map page:

```
GPS fix ──┐
          ├──▶ Web Mercator (lat,lon,zoom) ──▶ tileX, tileY, pixel offset
NVS  ─────┘                                              │
                                                         ▼
                                          ┌──────────────────────────┐
                                          │ LRU cache (4 × PSRAM)    │
                                          └──────────┬───────────────┘
                                                     │ miss
                                                     ▼
                                          SD: /tiles/Z/X/Y.bmp
                                                     │
                                                     ▼
                                          Line-by-line pushColors → TFT
                                                     │
                                                     ▼
                                          GPX overlay (latLonToScreen)
```

## Getting started

### Prerequisites

| Tool | Minimum version |
| --- | --- |
| [Arduino IDE](https://www.arduino.cc/en/software) | 2.3.0 |
| [Arduino-ESP32 core](https://github.com/espressif/arduino-esp32) | 3.0.0 |
| Python (esptool, board manager bootstrap) | 3.9 |

### Hardware

- ESP32-S3 N16R8 development board (with PSRAM enabled in the board menu).
- 2.0" ST7789 TFT, 240×320, SPI, 4-wire.
- NMEA-compatible UART GNSS module (3.3 V TTL).
- BMP280 (I²C, addr 0x76 or 0x77).
- AHT20 / AHT10 (I²C, addr 0x38).
- MPU6050 (I²C, addr 0x68).
- SPI SD card module, FAT32-formatted card.
- 7 momentary push buttons wired to GPIO with internal pull-ups.

### Pin configuration

These constants are hard-coded in the firmware. If you change the wiring, update them in source and re-flash.

| Peripheral | Signal | GPIO | Defined in |
| --- | --- | --- | --- |
| TFT (ST7789) | MOSI | 11 | `User_Setup.h` |
| TFT | SCLK | 18 | `User_Setup.h` |
| TFT | CS | 10 | `User_Setup.h` |
| TFT | DC | 9 | `User_Setup.h` |
| TFT | RST | 8 | `User_Setup.h` |
| SD card | CS | 5 | [sdcard.h](sdcard.h) |
| SD card | MOSI | 12 | [sdcard.h](sdcard.h) |
| SD card | MISO | 47 | [sdcard.h](sdcard.h) |
| SD card | SCK | 13 | [sdcard.h](sdcard.h) |
| I²C bus (sensors) | SDA | 21 | [GARMINE_LIKE.ino](GARMINE_LIKE.ino) |
| I²C bus (sensors) | SCL | 20 | [GARMINE_LIKE.ino](GARMINE_LIKE.ino) |
| Buttons | UP / DOWN / LEFT / RIGHT | 4 / 3 / 14 / 15 | [buttons.cpp](buttons.cpp) |
| Buttons | MID / SET / RST | 46 / 7 / 6 | [buttons.cpp](buttons.cpp) |

### Installation

```bash
git clone <this-repo> GARMINE_LIKE
cd GARMINE_LIKE
```

1. Install the [Arduino-ESP32](https://github.com/espressif/arduino-esp32) board manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
2. Select board **ESP32S3 Dev Module**, set `PSRAM: OPI PSRAM`, `Flash Size: 16MB`, `Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)` or equivalent.
3. Install the libraries listed below from the Library Manager (or via `arduino-cli lib install`).
4. Copy `User_Setup.h` to your local TFT_eSPI library folder (typically `Arduino/libraries/TFT_eSPI/User_Setup.h`), overwriting the default.
5. Open `GARMINE_LIKE.ino` in the IDE, select the correct serial port, and click **Upload**.

## Library dependencies

| Library | Purpose | Manager target |
| --- | --- | --- |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Display driver, sprites | `TFT_eSPI` |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | NMEA decoder | `TinyGPSPlus` |
| [Adafruit BMP280](https://github.com/adafruit/Adafruit_BMP280_Library) | Pressure / altitude | `Adafruit BMP280 Library` |
| [Adafruit AHTX0](https://github.com/adafruit/Adafruit_AHTX0) | Temperature / humidity | `Adafruit AHTX0` |
| [MPU6050](https://github.com/ElectronicCats/mpu6050) | IMU (Electronic Cats fork) | `MPU6050` |
| Arduino-ESP32 bundled | `SD`, `SPI`, `Wire`, `Preferences`, `HardwareSerial` | shipped with core |

## SD card layout

The firmware expects the following structure on a FAT32-formatted SD card:

```
/
├── tiles/
│   └── <zoom>/<tileX>/<tileY>.bmp     # 256×256 RGB565 BMP, zoom 13–15
└── track.gpx                          # Optional GPX 1.1 file (≤ 10 000 trkpt)
```

Tiles follow the standard XYZ/Slippy Map scheme. Generate them with any [OSM tile downloader](https://wiki.openstreetmap.org/wiki/Tile_servers) and convert to 16-bit BMP (e.g. via ImageMagick: `convert in.png -depth 5 -define bmp:format=bmp3 -type TrueColor BMP3:out.bmp`).

## Testing

There is no automated test harness — the firmware is validated on hardware. The serial console at 115 200 baud emits a structured boot log and per-subsystem status:

```bash
# macOS / Linux
screen /dev/tty.usbmodem* 115200
# Windows
mode COM3 BAUD=115200
```

Each subsystem reports `OK`, `WARN`, or `ERR` on the splash screen during the 2.5 s boot window.

## Flashing

1. Connect the ESP32-S3 over USB-C. On first flash, hold **BOOT** and tap **RESET** to enter download mode.
2. In the Arduino IDE, select **Tools → Board → ESP32S3 Dev Module**.
3. Enable PSRAM: **Tools → PSRAM → OPI PSRAM**.
4. Pick the correct **Port** under **Tools → Port**.
5. Click **Upload**. Total firmware is ~600 KB; the upload takes under 30 s at 921 600 baud.
6. After the splash screen completes (~2.5 s), the device drops into the main menu. Subsystem status badges (`SD`, `SENS`, `FIX`) confirm hardware readiness.

For batch production, `esptool.py write_flash` against the compiled `.bin` artifact (exportable via **Sketch → Export Compiled Binary**) is the supported path.

## Security and robustness

- **No network stack.** Wi-Fi and Bluetooth are not initialized; the device is air-gapped by construction. No OTA endpoint, no telemetry upload.
- **No secrets in firmware.** The build contains no API keys, credentials, or server URLs.
- **Bounded inputs.** The GPX parser caps point count at 10 000 and uses a 4 KB chunked SD reader to avoid byte-by-byte denial of service on malformed files. BMP tile loading rejects non-conforming dimensions, depth, or compression flags before allocating to the cache.
- **Numerical safety.** Latitude is clamped to ±85.05° before the Web Mercator projection to prevent `tan(π/2)` divergence. Longitude wraps cleanly across the antimeridian.
- **Persistence integrity.** Last-known position is written to NVS every 30 s only when a fix is present, preventing flash wear from zeroed coordinates.
- **Bus resilience.** SD initialization sequences 80 dummy clock pulses with CS high (per the SD spec), runs at a conservative 4 MHz, and is retried automatically every 10 s if dropped.
- **Input debounce.** The 7-button matrix uses 20 ms debounce with edge-triggered dispatch, so a stuck or noisy button cannot generate runaway page transitions.

## License

Released under the [MIT License](https://opensource.org/licenses/MIT). Third-party libraries listed in [Library dependencies](#library-dependencies) retain their own licenses.

## Contact

Open an issue on the repository for bug reports, hardware compatibility questions, or feature requests.
