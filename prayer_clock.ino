#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>

// ─── config ──────────────────────────────────────────────────────────────────

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* WIFI_SSID     = "B-1";
const char* WIFI_PASSWORD = "Shadow@b!";
const char* NTP_SERVER    = "pool.ntp.org";

const unsigned long SCROLL_INTERVAL = 3000UL;
const unsigned long CLOCK_INTERVAL  = 1000UL;

// ─── dynamic config (updatable via dashboard) ─────────────────────────────────

String city        = "Dhaka";
String country     = "Bangladesh";
int    calcMethod  = 1;
int    school      = 1;
int    utcOffsetH  = 6;

bool   lcdBacklight = true;
bool   use12h       = true;

// ─── globals ─────────────────────────────────────────────────────────────────

WebServer server(80);
WiFiUDP   ntpUDP;
NTPClient* timeClient = nullptr;

const char* PRAYER_NAMES[6] = {"Fajr","Sunrise","Dhuhr","Asr","Maghrib","Isha"};
String      prayerTimes[6]  = {"--:--","--:--","--:--","--:--","--:--","--:--"};
bool        timingsFetched  = false;

int           currentPrayerIdx = 0;
int           datePhase        = 0;
unsigned long lastScrollTime   = 0;
unsigned long lastClockUpdate  = 0;
int           lastFetchDay     = -1;   // track which day we last fetched
bool          fetchedToday     = false;

// ─── LCD display helpers ──────────────────────────────────────────────────────

void lcdClear() { lcd.clear(); }

void lcdMsg(const char* line1, const char* line2 = "") {
  lcdClear();
  lcd.setCursor(0, 0); lcd.print(line1);
  if (line2 && strlen(line2) > 0) {
    lcd.setCursor(0, 1); lcd.print(line2);
  }
}

void lcdProgress(const char* label, int step, int total) {
  lcdClear();
  lcd.setCursor(0, 0); lcd.print(label);
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) {
    if (i < step)       lcd.print("=");
    else if (i == step) lcd.print(">");
    else                lcd.print("-");
  }
}

// ─── time helpers ─────────────────────────────────────────────────────────────

int toMinutes(const String& t) {
  if (t.length() < 5 || t[2] != ':') return -1;
  return t.substring(0, 2).toInt() * 60 + t.substring(3, 5).toInt();
}

String to12h(const String& t) {
  if (t == "--:--") return "--:--";
  int h = t.substring(0, 2).toInt();
  int m = t.substring(3, 5).toInt();
  const char* ap = (h < 12) ? "AM" : "PM";
  h = h % 12; if (h == 0) h = 12;
  char buf[9];
  sprintf(buf, "%02d:%02d%s", h, m, ap);
  return String(buf);
}

String clockString() {
  
  if (!timeClient) return "--:--:--";
  int h = timeClient->getHours();
  int m = timeClient->getMinutes();
  int s = timeClient->getSeconds();
  if (use12h) {
    const char* ap = (h < 12) ? " AM" : " PM";
    h = h % 12; if (h == 0) h = 12;
    char buf[13]; sprintf(buf, "%02d:%02d:%02d%s", h, m, s, ap);
    return String(buf);
  } else {
    char buf[9]; sprintf(buf, "%02d:%02d:%02d", h, m, s);
    return String(buf);
  }
}

String dayAbbr() {
  if (!timeClient) return "---";
  const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  return String(days[timeClient->getDay()]);
}

void epochToDate(int& outDay, int& outMonth) {
  unsigned long jdn = (timeClient->getEpochTime() / 86400UL) + 2440588UL;
  unsigned long a   = jdn + 32044;
  unsigned long b   = (4 * a + 3) / 146097;
  unsigned long c   = a - (146097 * b) / 4;
  unsigned long d2  = (4 * c + 3) / 1461;
  unsigned long e   = c - (1461 * d2) / 4;
  unsigned long m2  = (5 * e + 2) / 153;
  outDay   = e - (153 * m2 + 2) / 5 + 1;
  outMonth = m2 + (m2 < 10 ? 3 : -9);
}

String dateNum() {
  int d, m; epochToDate(d, m);
  char buf[3]; sprintf(buf, "%02d", d); return String(buf);
}

String monthAbbr() {
  int d, m; epochToDate(d, m);
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec"};
  return String(months[m - 1]);
}

int activePrayerIdx() {
  if (!timeClient) return 0;
  int now = timeClient->getHours() * 60 + timeClient->getMinutes();
  const int markable[] = {0, 2, 3, 4, 5};
  for (int k = 0; k < 5; k++) {
    int i     = markable[k];
    int inext = markable[(k + 1) % 5];
    int curr  = toMinutes(prayerTimes[i]);
    int next  = toMinutes(prayerTimes[inext]);
    if (curr < 0 || next < 0) continue;
    if (next < curr) { if (now >= curr || now < next) return i; }
    else             { if (now >= curr && now < next) return i; }
  }
  return 0;
}

String padRight(String s, int len) {
  while ((int)s.length() < len) s += ' ';
  return s.substring(0, len);
}

// ─── display ─────────────────────────────────────────────────────────────────

void updateDisplay() {
  String label;
  switch (datePhase) {
    case 0:  label = dayAbbr();   break;
    case 1:  label = dateNum();   break;
    default: label = monthAbbr(); break;
  }
  String row1 = padRight(clockString() + " " + label, 16);

  int    active   = activePrayerIdx();
  bool   showStar = (currentPrayerIdx != 1) && (currentPrayerIdx == active);
  String marker   = showStar ? "*" : " ";
  String left     = padRight(marker + String(PRAYER_NAMES[currentPrayerIdx]), 8);
  String right    = padRight(" " + to12h(prayerTimes[currentPrayerIdx]), 8);
  String row2     = left + right;

  lcd.setCursor(0, 0); lcd.print(row1);
  lcd.setCursor(0, 1); lcd.print(row2);
}

// ─── fetch ────────────────────────────────────────────────────────────────────

String cleanTime(const String& raw) {
  if (raw.length() < 5 || raw[2] != ':') return "--:--";
  return raw.substring(0, 5);
}

String buildApiUrl() {
  return "https://api.aladhan.com/v1/timingsByCity?city=" + city +
         "&country=" + country +
         "&method=" + String(calcMethod) +
         "&school=" + String(school);
}

bool fetchPrayerTimes() {
  if (WiFi.status() != WL_CONNECTED) return false;

  lcdMsg("Fetching", "Prayer Times...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, buildApiUrl());

  int code = http.GET();
  bool ok = false;

  if (code == HTTP_CODE_OK) {
    lcdMsg("Parsing", "API Response...");
    DynamicJsonDocument doc(12288);
    if (!deserializeJson(doc, http.getString())) {
      JsonObject t   = doc["data"]["timings"];
      prayerTimes[0] = cleanTime(t["Fajr"].as<String>());
      prayerTimes[1] = cleanTime(t["Sunrise"].as<String>());
      prayerTimes[2] = cleanTime(t["Dhuhr"].as<String>());
      prayerTimes[3] = cleanTime(t["Asr"].as<String>());
      prayerTimes[4] = cleanTime(t["Maghrib"].as<String>());
      prayerTimes[5] = cleanTime(t["Isha"].as<String>());
      timingsFetched = true;
      ok = true;
      lcdMsg("Prayer Times", "Updated!");
      delay(800);
    } else {
      lcdMsg("Error", "JSON Parse Fail");
      delay(1500);
    }
  } else {
    lcdMsg("Error", ("HTTP " + String(code)).c_str());
    delay(1500);
  }
  http.end();
  return ok;
}

// ─── reconnect WiFi helper ────────────────────────────────────────────────────

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  lcdMsg("WiFi Lost", "Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    attempt++;
    lcdProgress("Reconnecting", attempt % 16, 16);
  }
  lcdMsg("WiFi OK", WiFi.localIP().toString().c_str());
  delay(1000);
}

// ─── NTP rebuild helper (when UTC offset changes) ─────────────────────────────

void rebuildNTP() {
  if (timeClient) {
    timeClient->end();
    delete timeClient;
  }
  timeClient = new NTPClient(ntpUDP, NTP_SERVER, utcOffsetH * 3600);
  timeClient->begin();
  int attempt = 0;
  while (!timeClient->update() && attempt < 15) {
    attempt++;
    delay(500);
  }
}

// ─── CORS helper ──────────────────────────────────────────────────────────────

void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ─── Web API handlers ─────────────────────────────────────────────────────────

void handleOptions() {
  addCORS();
  server.send(204);
}

void handleStatus() {
  addCORS();
  DynamicJsonDocument doc(1024);
  doc["city"]        = city;
  doc["country"]     = country;
  doc["method"]      = calcMethod;
  doc["school"]      = school;
  doc["utcOffsetH"]  = utcOffsetH;
  doc["backlight"]   = lcdBacklight;
  doc["use12h"]      = use12h;
  doc["fetched"]     = timingsFetched;
  doc["time"]        = clockString();
  doc["day"]         = dayAbbr();
  doc["date"]        = dateNum();
  doc["month"]       = monthAbbr();
  doc["activePrayer"]= activePrayerIdx();

  JsonArray arr = doc.createNestedArray("prayers");
  for (int i = 0; i < 6; i++) {
    JsonObject p = arr.createNestedObject();
    p["name"] = PRAYER_NAMES[i];
    p["time"] = prayerTimes[i];
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleUpdate() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }

  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
  }

  bool refetch    = false;
  bool rebuildNtp = false;

  if (doc.containsKey("city"))       { city       = doc["city"].as<String>();       refetch = true; }
  if (doc.containsKey("country"))    { country    = doc["country"].as<String>();    refetch = true; }
  if (doc.containsKey("method"))     { calcMethod = doc["method"].as<int>();        refetch = true; }
  if (doc.containsKey("school"))     { school     = doc["school"].as<int>();        refetch = true; }
  if (doc.containsKey("utcOffsetH")) { utcOffsetH = doc["utcOffsetH"].as<int>();   rebuildNtp = true; }

  if (doc.containsKey("backlight")) {
    lcdBacklight = doc["backlight"].as<bool>();
    if (lcdBacklight) lcd.backlight(); else lcd.noBacklight();
  }
  if (doc.containsKey("use12h")) {
    use12h = doc["use12h"].as<bool>();
  }

  // Accept manually pushed prayer times
  if (doc.containsKey("prayers")) {
    JsonArray arr = doc["prayers"].as<JsonArray>();
    int idx = 0;
    for (JsonObject p : arr) {
      if (idx < 6 && p.containsKey("time")) {
        prayerTimes[idx] = cleanTime(p["time"].as<String>());
      }
      idx++;
    }
    timingsFetched = true;
    refetch = false;
  }

  if (rebuildNtp) rebuildNTP();
  if (refetch) {
    fetchedToday = false;
    fetchPrayerTimes();
    fetchedToday = true;
    if (timeClient) lastFetchDay = timeClient->getDay();
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRefetch() {
  addCORS();
  bool ok = fetchPrayerTimes();
  if (ok) {
    fetchedToday = true;
    if (timeClient) lastFetchDay = timeClient->getDay();
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(500, "application/json", "{\"error\":\"fetch failed\"}");
  }
}

void handleLcdMsg() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400); return; }
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }
  String l1 = doc.containsKey("line1") ? doc["line1"].as<String>() : "";
  String l2 = doc.containsKey("line2") ? doc["line2"].as<String>() : "";
  lcdMsg(l1.c_str(), l2.c_str());
  delay(doc.containsKey("ms") ? (int)doc["ms"] : 2000);
  server.send(200, "application/json", "{\"ok\":true}");
}

// ─── setup ────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);

  lcd.init();
  lcd.backlight();
  lcdMsg("System Boot", "Initializing...");
  delay(1000);

  // ── WiFi: keep retrying forever ──────────────────────────────────────────
  Serial.println("\n========== BOOT START ==========");
  lcdMsg("WiFi Setup", "Connecting...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    attempt++;
    lcdProgress("WiFi Connect", attempt % 16, 16);
    Serial.print(".");
    if (attempt % 40 == 0) {                        // retry every 20 s
      Serial.println("\nRetrying WiFi...");
      WiFi.disconnect();
      delay(500);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  Serial.println("\n✓ WiFi Connected! IP: " + WiFi.localIP().toString());
  lcdMsg("WiFi OK", WiFi.localIP().toString().c_str());
  delay(1500);

  // ── NTP ──────────────────────────────────────────────────────────────────
  lcdMsg("NTP Sync", "Starting...");
  timeClient = new NTPClient(ntpUDP, NTP_SERVER, utcOffsetH * 3600);
  timeClient->begin();

  int ntpAttempt = 0;
  while (!timeClient->update()) {
    ntpAttempt++;
    lcdProgress("NTP Sync", ntpAttempt % 15, 15);
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\n✓ NTP OK: " + clockString());
  lcdMsg("NTP OK", clockString().c_str());
  delay(1500);

  // ── Prayer Times: keep retrying until fetched ────────────────────────────
  lcdMsg("Prayer Times", "Fetching...");
  while (!fetchPrayerTimes()) {
    lcdMsg("Fetch Failed", "Retrying 10s...");
    delay(10000);
    ensureWiFi();
  }
  fetchedToday = true;
  lastFetchDay = timeClient->getDay();

  // ── Web Server ────────────────────────────────────────────────────────────
  server.on("/status",  HTTP_GET,     handleStatus);
  server.on("/update",  HTTP_POST,    handleUpdate);
  server.on("/refetch", HTTP_GET,     handleRefetch);
  server.on("/lcd",     HTTP_POST,    handleLcdMsg);
  server.on("/status",  HTTP_OPTIONS, handleOptions);
  server.on("/update",  HTTP_OPTIONS, handleOptions);
  server.on("/refetch", HTTP_OPTIONS, handleOptions);
  server.on("/lcd",     HTTP_OPTIONS, handleOptions);
  server.begin();

  Serial.println("✓ Web server started");
  Serial.println("========== BOOT COMPLETE ==========\n");
  lcd.clear();
}

// ─── loop ─────────────────────────────────────────────────────────────────────

void loop() {
  server.handleClient();
  ensureWiFi();

  if (timeClient) timeClient->update();
  unsigned long now = millis();

  // ── Daily re-fetch at 06:00 ───────────────────────────────────────────────
  if (timeClient) {
    int h   = timeClient->getHours();
    int day = timeClient->getDay();
    if (h == 6 && day != lastFetchDay) {
      if (fetchPrayerTimes()) {
        fetchedToday = true;
        lastFetchDay = day;
      }
    }
  }

  // ── Clock update every second ─────────────────────────────────────────────
  if (now - lastClockUpdate > CLOCK_INTERVAL) {
    lastClockUpdate = now;
    if (timeClient) {
      int sec = timeClient->getSeconds();
      datePhase = (sec / 3) % 3;
    }
    updateDisplay();
  }

  // ── Scroll prayer every 3 seconds ─────────────────────────────────────────
  if (now - lastScrollTime > SCROLL_INTERVAL) {
    lastScrollTime   = now;
    currentPrayerIdx = (currentPrayerIdx + 1) % 6;
    updateDisplay();
  }
}
