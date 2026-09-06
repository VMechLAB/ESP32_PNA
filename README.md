# ESP32 Pocket Network Analyzer

![Version](https://img.shields.io/badge/version-0.1-blue)
![ESP32](https://img.shields.io/badge/ESP32-WiFi%2FBLE%2BNRF24-green)

little handheld gadget that scans wifi, bluetooth, and 2.4GHz RF junk around you. ESP32 + tiny OLED screen + NRF24 module, controlled with 4 buttons. basically a pocket wifi/bt sniffer.

## What it does
- scans wifi APs - names, MAC, channel, signal strength, encryption
- finds BLE devices nearby - name, MAC, signal strength
- sweeps all 126 NRF24 channels and tells you which ones got activity
- shows everything on a little 128x64 OLED screen
- 4 buttons to move around (up/down/select/back)
- can run off a 3.7V lipo battery if you want it portable
- small enough to 3D print a case for it

## What you need
| Part            | Spec                       |
|----------------------|------------------------------------|
| Brain              | ESP32-WROOM-32 (DevKit V1)         |
| Screen              | 0.96" OLED SSD1306, I2C (128x64)   |
| RF module         | NRF24L01+ (get one with an antenna) |
| Buttons              | 4x tactile buttons (6x6mm)      |
| Battery                | 3.7V lipo (1000mAh) + TP4056 charger |
| SD card (optional)     | not used yet, wired up for later    |

## Wiring
| Part | ESP32 Pin |
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
| SD CS (unused rn) | GPIO15 |

## How to use it
1. turn it on
2. up/down to move through menu
3. hit select to start scanning
4. scroll through the results
5. back or select to go back to the menu

## Setup
- get Arduino IDE or PlatformIO
- install the U8g2 and RF24 libraries (BLE is already built in)
- open `analyzer.ino`, pick "ESP32 Dev Module" as your board
- plug it in and upload

## Stuff to add later
- log scans to an SD card as CSV
- graph showing RSSI over time
- web dashboard, have the ESP32 host its own AP
- STL files for a case

## License
Hell no. Do what you want with this.