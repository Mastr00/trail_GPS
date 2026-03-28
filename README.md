# TrailNav GPS Companion

A complete, feature-rich GPS companion device built for the ESP32-S3 microcontroller. This device offers comprehensive navigation and environmental tracking features on a beautiful 2.0" ST7789 TFT display.

## Features

- **GPS Navigation**: Real-time coordinates, speed, and heading. Includes dynamic map rendering and GPX track overlay.
- **Environment Sensing**: Tracks temperature, humidity, and barometric pressure.
- **IMU Interface**: Smooth real-time digital spirit level with visual axis representation.
- **Digital Compass**: Anti-flickering, smooth digital compass showing current heading.
- **Offline Maps**: Renders maps directly from an SD card, allowing navigation without an internet connection.
- **Persistent Storage**: Saves the last known location and device settings so you can resume your adventure immediately.

## Hardware Requirements

- **ESP32-S3** (N16R8 recommended)
- **Display**: 2.0-inch SPI TFT display (e.g., ST7789, 240x320)
- **GPS Module**: NMEA compatible UART GPS module
- **Sensors**: 
  - BMP280 (Pressure/Altitude)
  - AHT20/AHT10 (Temperature/Humidity)
  - MPU6050 (IMU)
- **SD Card Module**: SPI based

## Installation

1. Install the required libraries via the Arduino Library Manager:
   - `TFT_eSPI`
   - `TinyGPSPlus`
2. Configure `TFT_eSPI` using the provided `User_Setup.h`. Ensure all SPI pins match your hardware.
3. Flash the project onto your ESP32-S3.

## License

This project is open-source and free to be configured to your needs!