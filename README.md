# Smart Glass Helmet — Tunnel Worker Edition

![ESP32-S3](https://img.shields.io/badge/ESP32--S3-E7352C?style=flat&logo=espressif&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=flat&logo=cplusplus&logoColor=white)
![BLE](https://img.shields.io/badge/BLE-0082FC?style=flat&logo=bluetooth&logoColor=white)
![Firebase](https://img.shields.io/badge/Firebase-FFCA28?style=flat&logo=firebase&logoColor=black)

A wearable safety system for underground/tunnel workers, built on an ESP32-S3. The helmet-mounted visor continuously monitors ambient temperature, humidity, and obstacle distance, and surfaces SAFE / CAUTION / DANGER alerts through an on-visor OLED display, BLE notifications, and a Firebase Realtime Database feed for remote monitoring.

**Role:** Team Leader & Embedded Systems Engineer — directed a 6-person team from architecture through simulated-conditions end-to-end testing (May 2026). Migrated the platform from ESP32-C3 to ESP32-S3 mid-project to resolve a simultaneous BLE/Wi-Fi operation constraint.

## Features

- **Environmental sensing** — DHT11 for temperature/humidity, HC-SR04 ultrasonic sensor for obstacle distance.
- **On-visor OLED display** (SSD1306, 128x64) — live readings with a blinking matrix-style border on CAUTION/DANGER.
- **BLE UART (Nordic UART Service)** — broadcasts live status to a paired app/device (device name: `Tunnel Worker 1`).
- **Wi-Fi with multi-network fallback** — tries a configurable list of known networks until one connects.
- **Firebase Realtime Database logging** — pushes the latest snapshot and an append-only history of readings via REST.
- **NTP time sync** for accurate timestamps on logged data.
- **Three-tier alerting** (SAFE / CAUTION / DANGER) driven by configurable temperature, humidity, and distance thresholds.

## Hardware

| Component      | Notes                          |
|-----------------|---------------------------------|
| Board           | ESP32-S3                       |
| Temp/Humidity   | DHT11 (pin 4)                  |
| Distance        | HC-SR04 (TRIG: 15, ECHO: 16)   |
| Display         | SSD1306 OLED, I2C (SDA: 8, SCL: 9, addr 0x3C) |

## Getting Started

### 1. Install dependencies (Arduino IDE / arduino-cli)

- ESP32 board package
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `DHT sensor library`
- `NimBLE-Arduino`

### 2. Configure Wi-Fi credentials

Wi-Fi networks are kept out of source control. Copy the example file and fill in your own network(s):

```bash
cp wifi_credentials.example.h wifi_credentials.h
```

Edit `wifi_credentials.h`:

```cpp
WiFiCredential wifiCredentials[] = {
  {"YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD"},
};
```

`wifi_credentials.h` is gitignored and will not be committed.

### 3. Configure Firebase

The Firebase Realtime Database URL and device ID are set near the top of [tunnelWorker_smartGlass_Helmet.ino](tunnelWorker_smartGlass_Helmet.ino):

```cpp
#define FIREBASE_DATABASE_URL "https://<your-project>-default-rtdb.<region>.firebasedatabase.app"
#define DEVICE_ID "tunnel-worker-1"
#define DEVICE_NAME "Tunnel Worker 1"
```

Update these to point at your own Firebase project, and set RTDB rules appropriately for your use case.

### 4. Flash

Open `tunnelWorker_smartGlass_Helmet.ino` in the Arduino IDE, select your ESP32-S3 board/port, and upload.

## Alert Thresholds

| Condition   | Caution | Danger |
|-------------|---------|--------|
| Temperature | ≥ 32°C  | ≥ 35°C |
| Humidity    | ≥ 70%   | ≥ 85%  |
| Distance    | ≤ 70cm  | ≤ 30cm |

## Data Written to Firebase

Path: `helmetLogs/devices/<DEVICE_ID>/latest` and `.../history`

```json
{
  "deviceName": "Tunnel Worker 1",
  "temperatureC": 28.5,
  "humidityPct": 65.0,
  "distanceCM": 120.3,
  "dhtOk": true,
  "ultrasonicOk": true,
  "bleConnected": false,
  "status": "SAFE",
  "alert": "NORMAL OPERATION",
  "timestamp": 1717689600000
}
```

## Project Docs

See [SmartGlass-FinalLabExam.pdf](SmartGlass-FinalLabExam.pdf) for the full lab exam writeup. Demo/testing footage: [YouTube](https://www.youtube.com/@galangrenielf.3168).
