#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>

// ─── config ──────────────────────────────────────────────────────────────────

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* WIFI_SSID     = "WIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASS";
const char* NTP_SERVER    = "pool.ntp.org";
const int   UTC_OFFSET_S  = 6 * 3600;
const char* API_URL = "https://api.aladhan.com/v1/timingsByCity"
                      "?city=Dhaka&country=Bangladesh&method=1&school=1";

const unsigned long FETCH_INTERVAL  = 24UL * 60UL * 60UL * 1000UL;
const unsigned long SCROLL_INTERVAL = 3000UL;
const unsigned long CLOCK_INTERVAL  = 1000UL;

// ─── globals ─────────────────────────────────────────────────────────────────

WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, UTC_OFFSET_S);

const char* PRAYER_NAMES[6] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
String      prayerTimes[6]  = {"--:--","--:--","--:--","--:--","--:--","--:--"};

int           currentPrayerIdx = 0;
unsigned long lastFetchTime    = 0;
unsigned long lastScrollTime   = 0;
unsigned long lastClockUpdate  = 0;

// ─── time helpers ─────────────────────────────────────────────────────────────

int toMinutes(const String& t) {
  if (t.length() < 5 || t[2] != ':') return -1;
  return t.substring(0, 2).toInt() * 60 + t.substring(3, 5).toInt();
}

// 24h "HH:MM" → "hh:mmAM/PM"  (7 chars)
String to12h(const String& t) {
  if (t == "--:--") return "--:--";
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  const char* ap = (h < 12) ? "AM" : "PM";
  h = h % 12;
  if (h == 0) h = 12;
  char buf[9];
  sprintf(buf, "%02d:%02d%s", h, m, ap);
  return String(buf);
}

// NTP → "hh:mm:ssAM/PM"  (12 chars — note NO space before AM/PM)
String clockString() {
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  int s = timeClient.getSeconds();
  const char* ap = (h < 12) ? " AM" : " PM";
  h = h % 12;
  if (h == 0) h = 12;
  char buf[13];
  sprintf(buf, "%02d:%02d:%02d%s", h, m, s, ap);
  return String(buf);   // "06:04:32AM"  10 chars
}

String dayAbbr() {
  const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  return String(days[timeClient.getDay()]);
}

int activePrayerIdx() {
  int now = timeClient.getHours() * 60 + timeClient.getMinutes();
  const int markable[] = {0, 2, 3, 4, 5};
  for (int k = 0; k < 5; k++) {
    int i    = markable[k];
    int inext = markable[(k + 1) % 5];   // next markable prayer
    int curr = toMinutes(prayerTimes[i]);
    int next = toMinutes(prayerTimes[inext]);
    if (curr < 0 || next < 0) continue;
    if (next < curr) {                    // midnight wrap (Isha → Fajr)
      if (now >= curr || now < next) return i;
    } else {
      if (now >= curr && now < next) return i;
    }
  }
  return 0; // fallback to Fajr
}

// ─── string helper ────────────────────────────────────────────────────────────

String padRight(String s, int len) {
  while ((int)s.length() < len) s += ' ';
  return s.substring(0, len);
}

// ─── display ─────────────────────────────────────────────────────────────────

void updateDisplay() {
  String row1 = padRight(clockString() + " " + dayAbbr(), 16);

  int    active = activePrayerIdx();
  bool   showStar = (currentPrayerIdx != 1) && (currentPrayerIdx == active);
  String marker   = showStar ? "*" : " ";

  String left  = padRight(marker + String(PRAYER_NAMES[currentPrayerIdx]), 8);
  String right  = padRight(" " + to12h(prayerTimes[currentPrayerIdx]), 8);
  String row2   = left + right;   // exactly 16 chars

  lcd.setCursor(0, 0); lcd.print(row1);
  lcd.setCursor(0, 1); lcd.print(row2);
}

// ─── fetch ────────────────────────────────────────────────────────────────────

// Keep only "HH:MM" 24h — API may append " (UTC+06)" which we discard
String cleanTime(const String& raw) {
  if (raw.length() < 5 || raw[2] != ':') return "--:--";
  return raw.substring(0, 5);
}

void fetchPrayerTimes() {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear(); lcd.print("WiFi Error"); return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, API_URL);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    DynamicJsonDocument doc(12288);
    if (!deserializeJson(doc, http.getString())) {
      JsonObject t   = doc["data"]["timings"];
      prayerTimes[0] = cleanTime(t["Fajr"].as<String>());
      prayerTimes[1] = cleanTime(t["Sunrise"].as<String>());
      prayerTimes[2] = cleanTime(t["Dhuhr"].as<String>());
      prayerTimes[3] = cleanTime(t["Asr"].as<String>());
      prayerTimes[4] = cleanTime(t["Maghrib"].as<String>());
      prayerTimes[5] = cleanTime(t["Isha"].as<String>());
    } else {
      lcd.clear(); lcd.print("JSON Error");
    }
  } else {
    lcd.clear(); lcd.print("API Failed");
  }
  http.end();
}

// ─── setup ────────────────────────────────────────────────────────────────────

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  lcd.clear();
  lcd.print("WiFi Connected");
  delay(800);

  timeClient.begin();
  while (!timeClient.update()) delay(1000);

  fetchPrayerTimes();
  lastFetchTime = millis();

  lcd.clear();
}

// ─── loop ─────────────────────────────────────────────────────────────────────

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
    lastScrollTime   = now;
    currentPrayerIdx = (currentPrayerIdx + 1) % 6;
    updateDisplay();
  }
}
