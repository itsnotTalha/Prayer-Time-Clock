#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
const char* ssid     = "Your_Wifi_name";
const char* password = "Your_Wifi_Password";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 6 * 3600);

String prayerNames[6] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
String prayerTimes[6] = {"--:--", "--:--", "--:--", "--:--", "--:--", "--:--"};

unsigned long lastFetchTime   = 0;
unsigned long lastScrollTime  = 0;
unsigned long lastClockUpdate = 0;

const unsigned long FETCH_INTERVAL  = 24UL * 60 * 60 * 1000;
const unsigned long SCROLL_INTERVAL = 3000;
const unsigned long CLOCK_INTERVAL  = 1000;

int currentPrayerIdx = 0;

// ─── helpers ────────────────────────────────────────────────────────────────

String padRight(String s, int len) {
  while ((int)s.length() < len) s += ' ';
  return s.substring(0, len);
}

String getFormattedTime12h() {
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  int s = timeClient.getSeconds();
  String ampm = (h < 12) ? " AM" : " PM";
  h = h % 12;
  if (h == 0) h = 12;
  char buf[12];
  sprintf(buf, "%02d:%02d:%02d%s", h, m, s, ampm.c_str());
  return String(buf);  // e.g. "06:04:32AM"
}

int getNextPrayerIdx() {
  int nowMins = timeClient.getHours() * 60 + timeClient.getMinutes();

  for (int i = 0; i < 6; i++) {
    if (prayerTimes[i] == "--:--") continue;

    // Parse current prayer time
    int hCurr = prayerTimes[i].substring(0, 2).toInt();
    int mCurr = prayerTimes[i].substring(3, 5).toInt();

    // Parse next prayer time (wrap to Fajr after Isha)
    int nextI = (i + 1) % 6;
    int hNext = prayerTimes[nextI].substring(0, 2).toInt();
    int mNext = prayerTimes[nextI].substring(3, 5).toInt();

    int currStart = hCurr * 60 + mCurr;
    int nextStart = hNext * 60 + mNext;

    // Handle midnight wrap (Isha window goes past midnight into next day)
    if (nextStart < currStart) {
      if (nowMins >= currStart || nowMins < nextStart) return i;
    } else {
      if (nowMins >= currStart && nowMins < nextStart) return i;
    }
  }

  return 0; // fallback
}

String getDayAbbr() {
  String days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  return days[timeClient.getDay()];
}

// ─── display ────────────────────────────────────────────────────────────────

void updateDisplay() {
  // Row 1
  String row1 = padRight(getFormattedTime12h() + " " + getDayAbbr(), 16);
  lcd.setCursor(0, 0);
  lcd.print(row1);

  // Row 2
  int nextIdx = getNextPrayerIdx();
  String marker = (currentPrayerIdx == nextIdx) ? "*" : " ";

  String left = padRight(marker + prayerNames[currentPrayerIdx], 8);
  String t = prayerTimes[currentPrayerIdx];
  String right = padRight(" " + t, 8);

  lcd.setCursor(0, 1);
  lcd.print(left + right);
}

// ─── setup ──────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);
  lcd.init();
  lcd.backlight();

  lcd.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1000);

  timeClient.begin();
  while (!timeClient.update()) {
    Serial.println("Waiting for NTP...");
    delay(1000);
  }
  Serial.println("NTP synchronized");

  fetchPrayerTimes();
  lastFetchTime = millis();

  lcd.clear();
}

// ─── loop ───────────────────────────────────────────────────────────────────

void loop() {
  timeClient.update();
  unsigned long now = millis();

  if (now - lastFetchTime > FETCH_INTERVAL) {
    fetchPrayerTimes();
    lastFetchTime = now;
  }

  if (now - lastClockUpdate > CLOCK_INTERVAL) {
    lastClockUpdate = now;
    updateDisplay();
  }

  if (now - lastScrollTime > SCROLL_INTERVAL) {
    lastScrollTime = now;
    currentPrayerIdx = (currentPrayerIdx + 1) % 6;
    updateDisplay();
  }
}

// ─── fetch ──────────────────────────────────────────────────────────────────

void fetchPrayerTimes() {
  Serial.println("=== FETCHING PRAYER TIMES ===");
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear(); lcd.print("WiFi Error"); return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String url = "https://api.aladhan.com/v1/timingsByCity?city=Dhaka&country=Bangladesh&method=2&school=1";
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.print("HTTP Code: "); Serial.println(httpCode);

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(12288);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON ERROR: "); Serial.println(error.c_str());
      lcd.clear(); lcd.print("JSON Error");
    } else {
      JsonObject timings = doc["data"]["timings"];
      prayerTimes[0] = cleanTime(timings["Fajr"].as<String>());
      prayerTimes[1] = cleanTime(timings["Sunrise"].as<String>());
      prayerTimes[2] = cleanTime(timings["Dhuhr"].as<String>());
      prayerTimes[3] = cleanTime(timings["Asr"].as<String>());
      prayerTimes[4] = cleanTime(timings["Maghrib"].as<String>());
      prayerTimes[5] = cleanTime(timings["Isha"].as<String>());
      Serial.println("Prayer times updated.");
    }
  } else {
    Serial.print("HTTP FAILED: "); Serial.println(http.errorToString(httpCode).c_str());
    lcd.clear(); lcd.print("API Failed");
  }
  http.end();
}

String cleanTime(String t) {
  if (t.length() < 5) return "--:--";
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  String ampm = (h < 12) ? "AM" : "PM";
  h = h % 12;
  if (h == 0) h = 12;
  char buf[9];
  sprintf(buf, "%02d:%02d%s", h, m, ampm.c_str());
  return String(buf);  // e.g. "06:32AM"
}
