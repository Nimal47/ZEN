# ZEN — Ambient Desk Intelligent System (ADIS)

> An IoT-based smart desk system that monitors student productivity through real-time presence detection and device usage tracking.

---

## What It Does

ZEN sits on your desk and silently tracks how focused you actually are:

- **Presence Detection** — A PIR sensor continuously checks if you're at your desk
- **Phone Distraction Tracking** — An RFID tag on your phone logs every time you pick it up, giving you a distraction count and duration
- **On-Desk Interface** — A TFT display with button navigation gives you a timer, stopwatch, and reminder alerts without needing your phone
- **Productivity Analytics** — All sensor data is logged to Firebase and visualized in a companion app (in progress)

---

## Hardware Components

| Component | Model |
|---|---|
| Microcontroller | ESP32 DOIT DevKit V1 |
| Display | TFT ST7735 / ST7789 |
| OLED | SSD1306 128×64 |
| RFID Module | MFRC522 |
| Presence Sensor | PIR Motion Sensor |
| Power | LiPo Battery |
| Buttons | 2× Tactile Push Buttons |

---

## Pin Configuration

### TFT Display (SPI)
| TFT Pin | ESP32 GPIO |
|---|---|
| CS | 5 |
| DC | 2 |
| RST | 4 |
| SCK | 18 (VSPI default) |
| MOSI | 23 (VSPI default) |

### RFID — MFRC522 (SPI, shared bus)
| RFID Pin | ESP32 GPIO |
|---|---|
| CS (SDA) | 21 |
| RST | 22 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |

### OLED — SSD1306 (I2C)
| OLED Pin | ESP32 GPIO |
|---|---|
| SDA | 25 |
| SCL | 26 |

### Other
| Component | ESP32 GPIO |
|---|---|
| PIR Sensor OUT | 27 |
| Button — Mode | 32 |
| Button — Timer | 33 |
| PIR VCC | VIN |
| Battery + | VIN |

---

## Project Structure

```
ZEN/
├── src/                  # Main firmware source code
├── include/              # Header files
├── lib/                  # Local libraries
├── test/                 # PlatformIO unit tests (simulated/virtual)
├── hardware_tests/       # Real-world hardware validation scripts
├── 3D_MODEL/             # 3D printable enclosure files
├── platformio.ini        # PlatformIO build config
└── secret.example.h      # Template for WiFi & Firebase credentials
```

> **`test/` vs `hardware_tests/`** — `test/` is for PlatformIO's native unit testing framework (edge case simulation). `hardware_tests/` contains scripts used to verify real hardware behavior during development.

---

## Getting Started

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 board drivers installed
- A Firebase project with Realtime Database enabled

### 2. Clone the Repo

```bash
git clone https://github.com/Nimal47/ZEN.git
cd ZEN
```

### 3. Set Up Credentials

Copy the example secrets file and fill in your own values:

```bash
cp secret.example.h include/secret.h
```

Then open `include/secret.h` and fill in your WiFi and Firebase details. **Never commit `secret.h`** — it's already in `.gitignore`.

### 4. Flash to ESP32

```bash
pio run --target upload
```

Monitor serial output:

```bash
pio device monitor
```

---

## Firebase Setup

1. Go to [Firebase Console](https://console.firebase.google.com/) and create a new project
2. Enable **Realtime Database** (start in test mode for development)
3. Go to **Project Settings → Service Accounts** and note your database URL
4. Go to **Project Settings → General** and copy your **Web API Key**
5. Fill these into `include/secret.h`

---

## Dependencies (auto-installed by PlatformIO)

| Library | Purpose |
|---|---|
| Adafruit GFX Library | Graphics primitives |
| Adafruit ST7735/ST7789 | TFT display driver |
| Adafruit SSD1306 | OLED display driver |
| MFRC522 | RFID module |
| ArduinoJson | JSON serialization |
| Firebase Arduino Client Library | Firebase Realtime Database |

---

## Companion App

A mobile companion app that visualizes your productivity data from Firebase is currently in development. It will show daily distraction counts, focus session durations, and trend graphs.

---

## License

This project is licensed under the [MIT License](LICENSE).
