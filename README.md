# TTGO T-Display ESP32 Fan Controller

Small ESP32 (TTGO T-Display) sketch that drives a thermistor-controlled fan with RGB status LED and a TFT UI. A built-in HTTP API exposes controls for auto/manual mode, RGB color, fan speed, and the temperature threshold.

## Features
- Reads an NTC 10K B3950 thermistor on ADC pin 36 and smooths readings.
- Auto mode maps temperature delta to fan duty cycle and LED color (green/yellow/orange/red).
- Manual mode lets clients pick fan speed (0-100%) and RGB color (0-255 per channel).
- 1.14" TFT (TFT_eSPI) shows current temperature, mode, and fan speed bar.
- Simple JSON HTTP API on port 80 for integration or testing with curl/Postman.

## Hardware (TTGO T-Display)
- Thermistor: ADC pin 36, series resistor 10k.
- Fan PWM: pin 25 (5 kHz, 8-bit).
- RGB LED: R=13, G=15, B=2 (PWM via ledc).
- TFT: handled by TFT_eSPI with the TTGO T-Display setup (ST7789 240x135).

## Setup
1) Copy Wi-Fi config: `cp config_wifi.h.template config_wifi.h` then fill `ssid` and `password`.
2) Arduino IDE: install libraries `ArduinoJson`, `TFT_eSPI`, `Button2` (included locally), and use the ESP32 board package. Select board "TTGO T1" or "ESP32 Dev Module" and set upload speed 115200.
3) TFT_eSPI config: in `libraries/TFT_eSPI/User_Setup_Select.h`, select the TTGO T-Display setup (usually `#include <User_Setups/Setup25_TTGO_T_Display.h>`). Adjust if your display uses a different pinout.
4) Wire the thermistor as a voltage divider to 3.3V with 10k series resistor to GND, sense on pin 36. Connect the fan MOSFET/driver to PWM pin 25. Wire the common-anode/cathode RGB LED to pins 13/15/2 accordingly.
5) Flash: open `main.ino`, verify, and upload. Serial monitor at 115200 for logs.

## HTTP API (port 80)
- GET `/mode` → `{ "mode": "auto" | "manual" }`
- PUT `/mode` body `{ "mode": "auto" | "manual" }`
- GET `/fan/status` → `{ mode, speed, color, temperature }`
- PUT `/fan/manual` body `{ "speed": 0-100, "color": "R,G,B" }` (requires `mode=manual`)
- PUT `/fan/threshold` body `{ "threshold": 0-50 }`
- GET `/temperature` → `{ "temperature": number }`
- PUT `/temperature` body `{ "value": -20 to 60 }` (test override; real sensor loop replaces it)

Example curl (manual mode):
```sh
curl -X PUT http://<device-ip>/mode -H "Content-Type: application/json" -d '{"mode":"manual"}'
curl -X PUT http://<device-ip>/fan/manual -H "Content-Type: application/json" -d '{"speed":70,"color":"255,120,40"}'
```

## Behavior
- Auto mode: compares temperature to `autoThreshold` (default 25 C) and sets fan/LED tiers (0/30/60/100%).
- Manual mode: holds your requested speed/color until you switch back to auto.
- Display refreshes when temp/mode/speed change; temperature is smoothed every second.

## Development notes
- PWM uses `ledcAttach`/`ledcWrite` from ESP32 Arduino Core 2.x/3.x.
- Serial debug logs can be disabled by setting `DEBUG` to `false` in `main.ino`.
- Keep `config_wifi.h` untracked; use the provided template for local secrets.
