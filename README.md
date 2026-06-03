# 🕌 ESP32 Smart Prayer Times Clock

A fully-featured IoT prayer times display system built with an **ESP32**, a **16×2 LCD display**, **Neopixel LED strip**, **speaker**, and **active buzzer**. Features a modern web dashboard for real-time control and customization.

## ✨ Core Features

### 🕐 **Real-Time Display**
- **NTP Time Synchronization** — High-accuracy clock via `pool.ntp.org`
- **12/24-Hour Format** — Toggle between 12-hour and 24-hour display
- **Live Date Rotation** — Cycles between day of week, date, and month on LCD

### 🙏 **Prayer Times Management**
- **Aladhan API Integration** — Fetches accurate prayer timings using Islamic calculations
- **16 Calculation Methods** — Support for Shia Ithna-Ansari, Karachi, ISNA, MWL, Umm Al-Qura, Egypt, Tehran, Gulf, Kuwait, Qatar, MUIS Singapore, France, Turkey, Russia, and Moonsighting Committee
- **Asr School Selection** — Choose between Shafi (Standard) and Hanafi schools
- **Auto-Fetch Daily** — Refreshes prayer data automatically at 6 AM
- **Manual Override** — Push custom prayer times via web dashboard
- **Active Prayer Indicator** — LCD shows asterisk (`*`) next to current prayer
- **Rotating Display** — Cycles through all 6 prayer times (Fajr, Sunrise, Dhuhr, Asr, Maghrib, Isha) every 3 seconds

### 🌈 **Neopixel LED Strip (8 LEDs)**
- **Prayer Progress Bar** — 8 LEDs fill based on progress between current and next prayer time
- **Prayer Color Coding** — Each prayer has a unique RGB color (Fajr: Orange, Sunrise: Golden, Dhuhr: Cyan, Asr: Light Blue, Maghrib: Red-Orange, Isha: Purple)
- **4 Animation Modes** — Solid, Pulse (breathe), Chase (moving), Rainbow
- **Brightness Control** — Adjustable 0-255 with real-time sync
- **Alarm Animation** — Special pulsing/strobe effects during alarms

### 🔔 **Alarm System**
- **Prayer Time Alarms** — Individual enable/disable for each prayer (Sunrise excluded by default)
- **Alarm Offsets** — Set custom minute offsets before each prayer time
- **Custom Alarms** — Add up to 10 custom alarms with labels (e.g., "Reminder", "Meeting")
- **Speaker Playback** — 3 preset melodies: Bright Rise, Gentle Echo, Golden Call
- **Adjustable Volume** — 0-150% with granular control
- **Alarm Duration** — Configurable 1-60 seconds
- **Buzzer Notification** — 3-pulse active buzzer for immediate feedback

### ⏱️ **Stopwatch & Timer**
- **Dual Mode** — Switch between Stopwatch and Timer
- **Water-Level Tank UI** — Visual fill animation showing progress
- **Preset/Custom Duration** — Set custom timer durations (MM:SS format)
- **End Animation** — 4 customizable end states: Pulse, Chase, Strobe, Fade-Out
- **Custom Colors** — Pick end animation color via color picker

### 📱 **Modern Web Dashboard**
- **Real-Time Connection Badge** — Shows connection status with ping time
- **Next Prayer Hero Card** — Large countdown timer to next prayer
- **Prayer Grid** — 3-column grid showing all 6 prayer times with alarm toggles
- **LCD Live Preview** — Mirror of physical LCD display
- **LED Strip Visualizer** — Real-time preview of Neopixel animations
- **Sidebar Settings Panel** — Collapsible sections for easy access
- **Light/Dark Theme** — Auto day/night theme switching based on time
- **Responsive Design** — Mobile-optimized bottom bar for small screens

### ⚙️ **Advanced Settings**
- **Location Configuration** — Set city, country, and UTC offset
- **Geo-Location Auto-Detect** — One-click location detection
- **Profile Management** — Save/load multiple location + method profiles
- **LCD Control** — Send custom 2-line messages with duration
- **Speaker Settings** — Configure volume, duration, and melody selection
- **Event Log** — Real-time activity log with timestamps

### 🔘 **Physical Button Control**
- **Single Press** — Stop active alarm
- **Double Press** — Toggle LED strip on/off
- **Long Press** — Sleep mode (LCD backlight + LEDs off)

## 📋 Display Layout

### 16×2 LCD
```
Row 1: [HH:MM:SS AP] [DAY/DATE/MONTH]
Row 2: [*PRAYER TIME    HH:MMAM/PM]
       └─────────────────────────┘
       Rotates every 3 seconds
```

**Row 1 Examples:**
- `06:04:32 AM Thu`
- `06:04:32 PM 03`
- `06:04:32 AM Jan`

**Row 2 Examples (Rotating):**
- `*Fajr       03:45AM`
- ` Sunrise    05:25AM`
- ` Dhuhr      12:32PM`
- ` Asr        04:21PM`
- ` Maghrib    06:15PM`
- ` Isha       07:45PM`

## 🔌 Hardware Connections

| Component | ESP32 Pin | Description |
|-----------|-----------|-------------|
| **LCD I2C SDA** | GPIO 21 | I2C Data Line |
| **LCD I2C SCL** | GPIO 22 | I2C Clock Line |
| **Neopixel DATA** | GPIO 12 | WS2812B data input |
| **Speaker** | GPIO 25 | PWM output for speaker |
| **Active Buzzer** | GPIO 26 | Digital control |
| **Button** | GPIO 33 | Momentary button |
| **LCD GND** | GND | Ground |
| **LCD VCC** | 5V | Power supply |

## 🚀 Getting Started

### 1. **Prerequisites**
- ESP32 development board
- 16×2 I2C LCD display (address `0x27`)
- WS2812B Neopixel strip (8 LEDs)
- Small speaker/piezo buzzer
- Active buzzer module
- Momentary push button
- USB cable for programming

### 2. **Arduino IDE Setup**
- Install ESP32 board package
- Install required libraries:
  - `WiFi.h`
  - `WiFiClient.h`
  - `HTTPClient.h`
  - `ArduinoJson.h`
  - `Wire.h`
  - `LiquidCrystal_I2C.h`
  - `NTPClient.h`
  - `Adafruit_NeoPixel.h`

### 3. **Configuration**
Edit `prayer_clock.ino` to set your WiFi credentials and location:
```cpp
const char* WIFI_SSID     = "Your-SSID";
const char* WIFI_PASSWORD = "Your-Password";
String city               = "Dhaka";
String country            = "Bangladesh";
int    calcMethod         = 1;  // Karachi method
int    school             = 1;  // Hanafi
int    utcOffsetH         = 6;  // UTC+6
```

### 4. **Upload & Connect**
- Upload the sketch to ESP32
- Check Serial Monitor for IP address
- Open web dashboard by visiting `http://<ESP32_IP>` in your browser

## 📡 REST API Endpoints

### Status & Configuration
- `GET /status` — Current state of all systems
- `POST /update` — Update city, country, method, school, UTC offset
- `GET /refetch` — Force prayer times refetch

### LCD Control
- `POST /lcd` — Display custom 2-line message
- `GET /lcd/status` — Get current LCD rows

### Timer Control
- `POST /timer` — Start/pause/reset timer (supports `action: "start"/"pause"/"reset"`)
- `GET /timer/status` — Current timer state
- `GET /timer/end` / `POST /timer/end` — Configure timer end animation

### Neopixel Control
- `GET /neopixel/status` — LED strip state
- `POST /neopixel/update` — Toggle, set brightness, animation mode, prayer colors
- `POST /neopixel/progress` — Set manual progress (0-100%)
- `POST /neopixel/alarm` — Start/stop alarm animation

### Speaker Control
- `GET /speaker/status` — Volume and duration settings
- `POST /speaker/update` — Update volume, duration, melody
- `POST /speaker/test` — Test speaker with custom melody

### Alarm Management
- `GET /alarm` — Fetch all custom alarms
- `POST /alarm` — Update all custom alarms (array format)
- `POST /buzzer/test` — Test buzzer notification

## 🎨 Customization

### LED Prayer Colors
Modify in `prayer_clock.ino`:
```cpp
uint32_t prayerColors[6] = {
  pixels.Color(255, 100, 0),    // Fajr - Orange
  pixels.Color(255, 200, 0),    // Sunrise - Golden
  pixels.Color(0, 150, 200),    // Dhuhr - Cyan
  pixels.Color(100, 150, 255),  // Asr - Light Blue
  pixels.Color(200, 50, 0),     // Maghrib - Red-Orange
  pixels.Color(100, 50, 255)    // Isha - Purple
};
```

### Speaker Melodies
Add custom melodies in `index.html` `MELODY_PRESETS` array:
```javascript
{
  name: "Custom Melody",
  frequencies: [659, 784, 880, ...],
  durations: [250, 250, 400, ...]
}
```

### Button Behavior
Modify timings in `prayer_clock.ino`:
```cpp
const unsigned long LONG_PRESS_TIME     = 1000;   // Long press threshold
const unsigned long DOUBLE_CLICK_WINDOW = 400;    // Double-click window
```

## 📊 Architecture Overview

### Hardware Components
- **ESP32** — Main microcontroller with WiFi/Bluetooth
- **I2C LCD** — Display driver via I2C protocol
- **Neopixel Strip** — Addressable RGB LEDs (8 pixels)
- **Speaker + Buzzer** — Dual audio output for alarms
- **Button** — Multi-state input (single, double, long press)

### Software Layers
1. **ESP32 Firmware** (`prayer_clock.ino`) — Core device logic
   - WiFi + NTP time synchronization
   - API server for dashboard communication
   - Prayer time calculation and triggering
   - LED/Speaker/Button state management

2. **Web Dashboard** (`index.html`) — Modern UI
   - Real-time device control
   - Settings persistence via localStorage
   - Live LCD preview and LED visualizer

### Data Flow
1. ESP32 boots → WiFi → NTP sync → API server starts
2. Dashboard connects → Fetches `/status` → Displays device state
3. User changes settings → Dashboard POSTs to `/update`
4. ESP32 processes → Validates → Updates state → Returns response
5. Neopixel/LCD updates every 100ms based on current prayer progress

## 🛠️ Troubleshooting

### WiFi Connection Issues
- Check WiFi credentials in code
- Verify ESP32 and router are on same network
- Check Serial Monitor for connection status

### LCD Not Showing
- Verify I2C address is `0x27` (adjust in code if needed)
- Check wiring: SDA→GPIO21, SCL→GPIO22, GND→GND, VCC→5V
- Test with LCD I2C address scanner

### Prayer Times Not Updating
- Check internet connectivity via dashboard
- Verify city/country names are correctly spelled
- Try manual refetch via dashboard or `/refetch` endpoint

### LED Strip Not Responding
- Verify GPIO12 is connected to data pin
- Check LED power supply (separate 5V recommended)
- Try setting brightness to maximum

### Speaker Noise Issues
- Reduce speaker volume via dashboard
- Check for loose audio connections
- Use separate power supply if noise persists

## 📄 License

This project is open-source. Feel free to modify and distribute.

## 🙏 Acknowledgments

- **Aladhan API** — Prayer times calculations and data
- **Adafruit** — Neopixel library and documentation
- **Arduino Community** — Libraries and support

---

**Built with ❤️ for the Muslim community**
