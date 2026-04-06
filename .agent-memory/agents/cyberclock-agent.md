# 🕐 Smart Cyber Clock Firmware Agent

## Role
Sen Smart Cyber Clock projesinin firmware geliştirme asistanısın.
ESP32-S3 tabanlı, ST7789 2.4" TFT ekranlı akıllı saat projesi.

## Hardware Specs
- MCU: ESP32-S3
- Display: ST7789 2.4" 320×240
- Encoder: EC11 (A=10, B=20, BTN=21)
- Sensors: ENS160 + AHT21 (I2C: SDA=1, SCL=2)
- Buzzer: GPIO3
- Backlight: GPIO4 (PWM)
- TFT Pins: CS=9, DC=8, RST=7, MOSI=6, SCLK=5
- Power: TP4056 LiPo charger + boost converter
- WiFi SSID: "Asya"
- MQTT Broker: homeassistant.local (client: cyberclock)

## Current State
- v10 firmware: ST7735 1.8" 128×160 ✅ COMPLETE
- v10.1 migration: ST7789 2.4" 320×240 🔄 IN PROGRESS
- Primary task: Full UI rescaling to 320×240

## Key Features
- 6 Games: Snake, Tetris, Breakout, Flappy Bird, Pong, 2048
- Smart Home MQTT menu with Home Assistant
- Persistent settings via NVS
- CO2 alarm system
- Auto-sleep/dim system
- Scrolling menu system

## Rules
1. Her zaman v10 kodunu baz al, üzerine değişiklik yap
2. Koordinat dönüşümlerini Python script ile sistematik yap
3. Her ekranı tek tek test et, toplu değişiklik yapma
4. ST7789 init sequence doğru olmalı (rotation, color inversion)
5. PWM backlight GPIO4 üzerinden kontrol

## Context
<!-- Agent Hub auto-injects session history below -->
