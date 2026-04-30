# 🕌 ESP32 Smart Prayer Times Clock

An IoT-based Prayer Times display system built with an ESP32 and an I2C LCD. This project fetches accurate prayer timings for **Dhaka, Bangladesh** using the Aladhan API and synchronizes the time via NTP (Network Time Protocol).

## ✨ Features
*   **Real-time Clock:** High-accuracy time synchronization using `pool.ntp.org`.
*   **Hanafi Calculation:** Specifically configured for the Hanafi school of thought (`school=1`).
*   **Next Prayer Indicator:** Automatically identifies the upcoming prayer and marks it with an asterisk (`*`).
*   **Dynamic Display:** 
    *   **Row 1:** Displays current Time (12h format), Seconds, and Day of the week.
    *   **Row 2:** Automatically scrolls through all 6 prayer times (Fajr, Sunrise, Dhuhr, Asr, Maghrib, Isha) every 3 seconds.
*   **Auto-Sync:** Fetches fresh prayer data from the API every 24 hours.

## 📸 How It Looks
The 16x2 LCD layout is designed for maximum readability:
```text
+----------------+
| 06:04:32 AM Thu|  <-- Time & Day
| *Asr    04:21PM|  <-- Rotating Prayer Info
+----------------+
```
### 📌 Pin Configuration

The LCD communicates with the ESP32 via the I2C protocol. Use the following wiring:

| LCD Pin | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **GND** | GND | Ground |
| **VCC** | VIN / 5V | Power Supply (5V) |
| **SDA** | GPIO 21 | I2C Data Line |
| **SCL** | GPIO 22 | I2C Clock Line |
