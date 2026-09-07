# ESP32 Pocket Network Analyzer

A handheld diagnostic tool that scans Wi‑Fi, BLE, and 2.4 GHz RF activity using an ESP32, 0.96" OLED, and NRF24L01+ module. All controlled with four tactile buttons.

## Features
- **Wi‑Fi AP scanning** – SSID, BSSID, channel, RSSI, encryption type
- **BLE device discovery** – device name, MAC address, RSSI
- **NRF24 channel activity** – sweeps 126 channels (0‑125) and reports active ones
- **0.96" OLED display** (128×64, I²C) – clear menu and results
- **Four‑button navigation** – UP, DOWN, SELECT, BACK (debounced)
- **Battery ready** – can be powered from a 3.7 V Li‑Po (with optional charger)
- **Compact** – fits in a 3D‑printed pocket enclosure

## Hardware
| Component            | Model / Spec                       |
|----------------------|------------------------------------|
| MCU                  | ESP32‑WROOM‑32 (DevKit V1)         |
| Display              | 0.96" OLED SSD1306, I²C (128×64)   |
| RF module            | NRF24L01+ (with SMA or PCB antenna)|
| Buttons              | 4× tactile momentary (6×6 mm)      |
| Power                | 3.7 V Li‑Po (1000 mAh) + TP4056    |
| Optional storage     | Micro SD card (SPI) – for future   |

## Pinout (ESP32 → Peripherals)
| Peripheral | ESP32 Pin |
|------------|-----------|
| OLED SDA   | GPIO21    |
| OLED SCL   | GPIO22    |
| NRF24 CE   | GPIO4     |
| NRF24 CSN  | GPIO5     |
| NRF24 MOSI | GPIO23    |
| NRF24 MISO | GPIO19    |
| NRF24 SCK  | GPIO18    |
| Button UP  | GPIO34    |
| Button DOWN| GPIO35    |
| Button SEL | GPIO32    |
| Button BACK| GPIO33    |
| (SD CS)    | (GPIO15)* |

> *SD card is not used in the current firmware, but pin is reserved.

## How to Use
1. Power on the device.
2. Use **UP/DOWN** to navigate the menu.
3. Press **SELECT** to start a scan.
4. Scroll through results with **UP/DOWN**.
5. Press **BACK** or **SELECT** to return to the main menu.

## Building the Firmware
- Install [Arduino IDE](https://www.arduino.cc/) or PlatformIO.
- Install libraries: `U8g2`, `RF24`, and `BLE` (built‑in).
- Open `analyzer.ino`, set your board to "ESP32 Dev Module".
- Upload via USB.

## Future Upgrades
- SD‑card logging to CSV
- RSSI history graph
- Web dashboard (ESP32 as AP)
- 3D‑printed enclosure (STL files coming)

## License
No licence use it to your liking.
