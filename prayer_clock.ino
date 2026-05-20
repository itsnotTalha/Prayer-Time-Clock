#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// ─── config ──────────────────────────────────────────────────────────────────

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── Neopixel & Buzzer ───────────────────────────────────────────────────────

#define NEOPIXEL_PIN    12      // GPIO12 for neopixel
#define NEOPIXEL_COUNT  8       // 8 LEDs
#define SPEAKER_PIN     25      // GPIO25 for speaker
#define BUZZER_PIN      26      // GPIO26 for active buzzer
#define BUTTON_PIN      33      // GPIO33 for momentary button

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Prayer colors (RGB)
uint32_t prayerColors[6] = {
  pixels.Color(255, 100, 0),    // Fajr - Orange
  pixels.Color(255, 200, 0),    // Sunrise - Golden
  pixels.Color(0, 150, 200),    // Dhuhr - Cyan
  pixels.Color(100, 150, 255),  // Asr - Light Blue
  pixels.Color(200, 50, 0),     // Maghrib - Red-Orange
  pixels.Color(100, 50, 255)    // Isha - Purple
};

// Neopixel state
struct {
  bool enabled = true;
  int brightness = 255;
  int animationMode = 0;  // 0=solid, 1=pulse, 2=chase, 3=rainbow
  int alarmAnimMode = 1;  // 0=pulse, 1=chase, 2=strobe
  bool alarmActive = false;
  unsigned long alarmStartTime = 0;
  float progressPercent = 0;
  int currentPrayerColor = 0;
} neoState;

// Speaker state with preset melody
struct {
  bool isPlaying = false;
  int currentNote = 0;
  unsigned long noteStartTime = 0;
  int volume = 100;           // 0-150
  int duration = 30;          // seconds
  unsigned long playStartTime = 0;
} speakerState;

// Preset melody frequencies and durations
const int MAX_MELODY_LEN = 32;
int melody[MAX_MELODY_LEN] = {
  659, 784, 880, 659,
  784, 988, 880, 784
};

int noteDurations[MAX_MELODY_LEN] = {
  250, 250, 400, 250,
  250, 500, 400, 700
};

int melodyLength = 8;

// Buzzer state
struct {
  bool isBuzzing = false;
  unsigned long pulseStartTime = 0;
  int pulseCount = 0;
  int targetPulses = 0;
  bool state = false;
} buzzerState;

// ─── Custom Alarms ────────────────────────────────────────────────────────
#define MAX_ALARMS 10
struct AlarmEntry {
  int hour;
  int minute;
  bool enabled;
  bool valid;
  char label[25];
};
AlarmEntry customAlarms[MAX_ALARMS];
int customAlarmCount = 0;
static int lastAlarmTriggeredMinute = -1;

// LCD state
String lcdRow1 = "                ";
String lcdRow2 = "                ";

const char* WIFI_SSID     = "B-1";
const char* WIFI_PASSWORD = "Shadow@b!";
const char* NTP_SERVER    = "pool.ntp.org";

// Static IP config
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
IPAddress local_IP(192, 168, 0, 69);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

const unsigned long SCROLL_INTERVAL = 3000UL;
const unsigned long CLOCK_INTERVAL  = 1000UL;
const unsigned long WIFI_RETRY_INTERVAL = 10000UL;
const int BUZZ_NOTIFY_MS = 1500;

// ─── dynamic config ──────────────────────────────────────────────────────────

String city        = "Dhaka";
String country     = "Bangladesh";
int    calcMethod  = 1;
int    school      = 1;
int    utcOffsetH  = 6;

bool   lcdBacklight = true;
bool   use12h       = true;

// ─── globals ──────────────────────────────────────────────────────────────────

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
int           lastFetchDay     = -1;
bool          fetchedToday     = false;

bool          timerRunning     = false;
unsigned long timerStartTime   = 0;
unsigned long timerElapsed     = 0;
int           timerMode        = 1;
unsigned long timerDuration    = 300000;
unsigned long timerStartEpoch  = 0;

bool          timerEnded       = false;
unsigned long timerEndStartTime = 0;
int           timerEndAnimMode = 3;
uint32_t      timerEndColor    = 0xFF8000;
unsigned long timerEndDuration = 3000;

bool          wifiWasConnected  = false;
bool          timeSynced        = false;
unsigned long lastWiFiAttempt   = 0;

// ─── IMPROVED BUTTON HANDLING ─────────────────────────────────────────────────
bool          buttonPressed    = false;
unsigned long buttonPressTime  = 0;
int           pressCount       = 0;
unsigned long lastPressEndTime = 0;
const unsigned long BUTTON_DEBOUNCE       = 50;      // debounce in ms
const unsigned long DOUBLE_CLICK_WINDOW   = 400;     // INCREASED from 300ms
const unsigned long LONG_PRESS_TIME       = 1000;    // INCREASED from 800ms for more reliable detection

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

String formatTimer(unsigned long ms) {
  unsigned long secs = ms / 1000;
  int h = secs / 3600;
  int m = (secs % 3600) / 60;
  int s = secs % 60;
  char buf[9];
  if (h > 0) {
    sprintf(buf, "%02d:%02d:%02d", h, m, s);
  } else {
    sprintf(buf, "%02d:%02d", m, s);
  }
  return String(buf);
}

// ─── Neopixel Functions ──────────────────────────────────────────────────────

void neoSetBrightness(int brightness) {
  neoState.brightness = constrain(brightness, 0, 255);
  pixels.setBrightness(neoState.brightness);
  pixels.show();
}

#define PULSE_CYCLE_MS   2000
#define CHASE_CYCLE_MS   6400
#define RAINBOW_CYCLE_MS 12800

void renderLedStrip(int ledsOn, int animationMode, uint32_t color) {
  unsigned long now = millis();
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  
  if (animationMode == 0) {
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
      } else {
        pixels.setPixelColor(i, pixels.Color(10, 10, 10));
      }
    }
  } else if (animationMode == 1) {
    float t = (float)(now % PULSE_CYCLE_MS) / PULSE_CYCLE_MS;
    int brightness = (int)(255.0f * sin(t * 2.0f * M_PI) * 0.5f + 127.5f);
    brightness = constrain(brightness, 50, 255);
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        uint8_t cr = (uint8_t)(r * brightness / 255.0f);
        uint8_t cg = (uint8_t)(g * brightness / 255.0f);
        uint8_t cb = (uint8_t)(b * brightness / 255.0f);
        pixels.setPixelColor(i, pixels.Color(cr, cg, cb));
      } else {
        pixels.setPixelColor(i, pixels.Color(10, 10, 10));
      }
    }
  } else if (animationMode == 2) {
    float t = (float)(now % CHASE_CYCLE_MS) / CHASE_CYCLE_MS;
    int pos = (int)(t * NEOPIXEL_COUNT) % NEOPIXEL_COUNT;
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        if (i == pos) {
          pixels.setPixelColor(i, pixels.Color(r, g, b));
        } else {
          pixels.setPixelColor(i, pixels.Color(r/5, g/5, b/5));
        }
      } else {
        pixels.setPixelColor(i, pixels.Color(10, 10, 10));
      }
    }
  } else if (animationMode == 3) {
    float t = (float)(now % RAINBOW_CYCLE_MS) / RAINBOW_CYCLE_MS;
    int offset = (int)(t * NEOPIXEL_COUNT) % NEOPIXEL_COUNT;
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        uint32_t c = prayerColors[(i + offset) % 6];
        pixels.setPixelColor(i, c);
      } else {
        pixels.setPixelColor(i, pixels.Color(10, 10, 10));
      }
    }
  } else {
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
      } else {
        pixels.setPixelColor(i, pixels.Color(10, 10, 10));
      }
    }
  }
  
  pixels.setBrightness(neoState.brightness);
  pixels.show();
}

void neoSetProgress(float percent, int prayerIdx) {
  neoState.progressPercent = constrain(percent, 0.0f, 100.0f);
  neoState.currentPrayerColor = constrain(prayerIdx, 0, 5);
  int ledsOn = (int)(NEOPIXEL_COUNT * (percent / 100.0f));
  uint32_t color = prayerColors[prayerIdx];
  renderLedStrip(ledsOn, neoState.animationMode, color);
}

void neoSolid(int prayerIdx) {
  uint32_t color = prayerColors[constrain(prayerIdx, 0, 5)];
  renderLedStrip(NEOPIXEL_COUNT, 0, color);
}

void neoAlarmAnimation(unsigned long elapsed) {
  int animMode = neoState.alarmAnimMode;
  
  if (animMode == 0) {
    neoState.brightness = (int)(255.0f * sin((float)elapsed / 500.0f * M_PI));
    neoState.brightness = constrain(neoState.brightness, 50, 255);
    pixels.setBrightness(neoState.brightness);
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 0, 0));
    }
  } else if (animMode == 1) {
    int pos = (int)((float)NEOPIXEL_COUNT * ((elapsed % 800) / 800.0f));
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i == pos) {
        pixels.setPixelColor(i, pixels.Color(255, 100, 0));
      } else {
        pixels.setPixelColor(i, pixels.Color(100, 30, 0));
      }
    }
    pixels.setBrightness(255);
  } else if (animMode == 2) {
    if ((elapsed / 150) % 2 == 0) {
      for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 50, 0));
      }
      pixels.setBrightness(255);
    } else {
      for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
      pixels.setBrightness(0);
    }
  }
  pixels.show();
}

void neoTimerEndAnimation(unsigned long elapsed) {
  int animMode = timerEndAnimMode;
  uint8_t r = (timerEndColor >> 16) & 0xFF;
  uint8_t g = (timerEndColor >> 8) & 0xFF;
  uint8_t b = timerEndColor & 0xFF;
  
  if (animMode == 0) {
    int brightness = (int)(255.0f * sin((float)elapsed / 500.0f * M_PI));
    brightness = constrain(brightness, 50, 255);
    pixels.setBrightness(brightness);
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
  } else if (animMode == 1) {
    int pos = (int)((float)NEOPIXEL_COUNT * ((elapsed % 800) / 800.0f));
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i == pos) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
      } else {
        pixels.setPixelColor(i, pixels.Color(r/3, g/3, b/3));
      }
    }
    pixels.setBrightness(255);
  } else if (animMode == 2) {
    if ((elapsed / 200) % 2 == 0) {
      for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
      }
      pixels.setBrightness(255);
    } else {
      for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
      pixels.setBrightness(0);
    }
  } else if (animMode == 3) {
    float progress = (float)elapsed / (float)timerEndDuration;
    progress = constrain(progress, 0.0f, 1.0f);
    int brightness = (int)(255.0f * (1.0f - progress));
    brightness = constrain(brightness, 0, 255);
    pixels.setBrightness(brightness);
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
  }
  pixels.show();
}

void neoOff() {
  pixels.clear();
  pixels.show();
}

// ─── BUZZER CONTROL (ACTIVE BUZZER) ────────────────────────────────────────

void buzzerPulse(int pulses) {
  buzzerState.targetPulses = pulses;
  buzzerState.pulseCount = 0;
  buzzerState.isBuzzing = true;
  buzzerState.pulseStartTime = millis();
}

void buzzerNotifyMs(int durationMs) {
  int pulses = max(1, durationMs / 200);
  buzzerPulse(pulses);
}

void updateBuzzer() {
  if (!buzzerState.isBuzzing) return;
  
  unsigned long now = millis();
  unsigned long elapsed = now - buzzerState.pulseStartTime;
  
  if (elapsed < 100) { // ON for 100ms
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (elapsed < 200) { // OFF for 100ms
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    buzzerState.pulseCount++;
    buzzerState.pulseStartTime = now;
    if (buzzerState.pulseCount >= buzzerState.targetPulses) {
      buzzerState.isBuzzing = false;
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

// ─── SPEAKER CONTROL (SPEAKER WITH PWM) ───────────────────────────────────────

void startSpeakerAlarm(int volume, int duration) {
  speakerState.volume = constrain(volume, 0, 150);
  speakerState.duration = constrain(duration, 1, 60);
  speakerState.currentNote = 0;
  speakerState.isPlaying = true;
  speakerState.playStartTime = millis();
  speakerState.noteStartTime = millis();
  
  // Configure PWM for speaker
  ledcAttach(SPEAKER_PIN, 4000, 8);  // 4kHz frequency, 8-bit resolution
}

void stopSpeakerAlarm() {
  speakerState.isPlaying = false;
  ledcWriteTone(SPEAKER_PIN, 0);
  ledcDetach(SPEAKER_PIN);
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);
}

void updateSpeaker() {
  if (!speakerState.isPlaying) return;
  
  unsigned long now = millis();
  unsigned long totalPlayTime = now - speakerState.playStartTime;
  
  // Check if total duration exceeded
  if (totalPlayTime >= (unsigned long)speakerState.duration * 1000) {
    stopSpeakerAlarm();
    return;
  }
  
  // Play notes in sequence, looping the melody
  unsigned long noteElapsed = now - speakerState.noteStartTime;
  if (melodyLength <= 0) {
    stopSpeakerAlarm();
    return;
  }

  int noteDuration = noteDurations[speakerState.currentNote];
  
  if (noteElapsed >= noteDuration) {
    // Move to next note
    speakerState.currentNote = (speakerState.currentNote + 1) % melodyLength;
    speakerState.noteStartTime = now;
    
    // Play the note
    int freq = melody[speakerState.currentNote];
    if (freq > 0) {
      ledcWriteTone(SPEAKER_PIN, freq);
      ledcWrite(SPEAKER_PIN, speakerState.volume);
    } else {
      ledcWriteTone(SPEAKER_PIN, 0);
    }
  }
}

// ─── IMPROVED BUTTON HANDLING ─────────────────────────────────────────────────

void handleButtonPress(int type) {
  // type: 1=single, 2=double, 3=long
  if (type == 1) {
    // Single: Stop Alarm
    if (neoState.alarmActive || speakerState.isPlaying) {
      stopSpeakerAlarm();
      neoState.alarmActive = false;
      neoOff();
      updateNeopixelProgressBar();
    }
  } else if (type == 2) {
    // Double: Toggle LED Strip
    neoState.enabled = !neoState.enabled;
    if (neoState.enabled) {
      pixels.setBrightness(neoState.brightness);
      updateNeopixelProgressBar();
    } else {
      neoOff();
    }
  } else if (type == 3) {
    // Long: Toggle Sleep Mode (LCD + LED)
    if (lcdBacklight || neoState.enabled) {
      // Go to sleep
      lcdBacklight = false;
      lcd.noBacklight();
      neoOff();
      neoState.enabled = false;
    } else {
      // Wake up
      lcdBacklight = true;
      lcd.backlight();
      neoState.enabled = true;
      pixels.setBrightness(neoState.brightness);
      updateNeopixelProgressBar();
    }
  }
}

void updateButton() {
  static bool lastState = HIGH;
  static unsigned long lastTime = 0;
  static int count = 0;
  static bool longPressTriggered = false;

  bool currentState = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  // Polling based state machine
  if (currentState != lastState) {
    delay(10); // Simple debounce
    if (digitalRead(BUTTON_PIN) == currentState) {
      if (currentState == LOW) { // Pressed
        lastTime = now;
        longPressTriggered = false;
      } else { // Released
        if (!longPressTriggered) {
          count++;
          lastTime = now;
        }
      }
      lastState = currentState;
    }
  }

  // Check Long Press
  if (currentState == LOW && !longPressTriggered && (now - lastTime > LONG_PRESS_TIME)) {
    handleButtonPress(3);
    longPressTriggered = true;
    count = 0; 
  }

  // Check Single/Double press window
  if (count > 0 && currentState == HIGH && (now - lastTime > DOUBLE_CLICK_WINDOW)) {
    if (count == 1) handleButtonPress(1);
    else if (count >= 2) handleButtonPress(2);
    count = 0;
  }
}

void updateDisplay() {
  String label;
  switch (datePhase) {
    case 0:  label = dayAbbr();   break;
    case 1:  label = dateNum();   break;
    default: label = monthAbbr(); break;
  }
  String row1 = padRight(clockString() + " " + label, 16);

  String row2;
  
  if (timerRunning) {
    unsigned long elapsed = timerElapsed + (millis() - timerStartTime);
    unsigned long displayTime = (timerMode == 1) ? (timerDuration > elapsed ? timerDuration - elapsed : 0) : elapsed;
    String timerStr = formatTimer(displayTime);
    String timerLabel = (timerMode == 0) ? "Stopwatch" : "Timer";
    row2 = padRight(timerLabel + " " + timerStr, 16);
  } else {
    int    active   = activePrayerIdx();
    bool   showStar = (currentPrayerIdx != 1) && (currentPrayerIdx == active);
    String marker   = showStar ? "*" : " ";
    String left     = padRight(marker + String(PRAYER_NAMES[currentPrayerIdx]), 8);
    String right    = padRight(" " + to12h(prayerTimes[currentPrayerIdx]), 8);
    row2     = left + right;
  }

  lcdRow1 = row1;
  lcdRow2 = row2;

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
  
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    pixels.setPixelColor(i, pixels.Color(100, 100, 0));
  }
  pixels.show();

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
      
      for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 255, 0));
      }
      pixels.show();
      delay(500);
      neoOff();
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

void ensureWiFi() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      buzzerNotifyMs(BUZZ_NOTIFY_MS);
    }
    return;
  }

  wifiWasConnected = false;
  unsigned long now = millis();
  if (now - lastWiFiAttempt < WIFI_RETRY_INTERVAL) return;
  lastWiFiAttempt = now;

  if (!timeSynced) {
    lcdMsg("WiFi Lost", "Reconnecting...");
  }
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

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

void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

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

void handleTimer() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400); return; }
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }

  if (doc.containsKey("action")) {
    String action = doc["action"].as<String>();
    if (action == "start") {
      if (!timerRunning) {
        if (doc.containsKey("duration")) {
          timerDuration = doc["duration"].as<unsigned long>();
        }
        if (timerMode == 1 && timerDuration == 0) {
          timerDuration = 300000;
        }
        timerElapsed = 0;
        timerRunning = true;
        timerStartTime = millis();
        if (timeClient) timerStartEpoch = timeClient->getEpochTime();
        timerEnded = false;
      }
    } else if (action == "pause") {
      if (timerRunning) {
        timerElapsed += (millis() - timerStartTime);
        timerRunning = false;
        timerStartEpoch = 0;
      }
    } else if (action == "reset") {
      timerRunning = false;
      timerElapsed = 0;
      timerStartTime = 0;
      timerStartEpoch = 0;
      timerEnded = false;
      if (timerMode == 1) {
        timerDuration = 300000;
      }
    } else if (action == "set_mode") {
      timerMode = doc.containsKey("mode") ? doc["mode"].as<int>() : 0;
      timerRunning = false;
      timerElapsed = 0;
      timerStartTime = 0;
      timerStartEpoch = 0;
      timerEnded = false;
      if (timerMode == 0) {
        timerDuration = 0;
      } else {
        timerDuration = 300000;
      }
    }
  }

  unsigned long currentElapsed = timerElapsed + (timerRunning ? (millis() - timerStartTime) : 0);
  unsigned long displayTime = (timerMode == 1) ? (timerDuration > currentElapsed ? timerDuration - currentElapsed : 0) : currentElapsed;
  
  DynamicJsonDocument resp(512);
  resp["running"] = timerRunning;
  resp["elapsed"] = displayTime;
  resp["mode"] = timerMode;
  resp["duration"] = timerDuration;
  resp["startEpoch"] = timerStartEpoch;
  if (timeClient) resp["currentEpoch"] = timeClient->getEpochTime();
  
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handleTimerStatus() {
  addCORS();
  unsigned long currentElapsed = timerElapsed + (timerRunning ? (millis() - timerStartTime) : 0);
  unsigned long displayTime = (timerMode == 1) ? (timerDuration > currentElapsed ? timerDuration - currentElapsed : 0) : currentElapsed;
  
  DynamicJsonDocument doc(512);
  doc["running"] = timerRunning;
  doc["elapsed"] = displayTime;
  doc["mode"] = timerMode;
  doc["duration"] = timerDuration;
  doc["startEpoch"] = timerStartEpoch;
  if (timeClient) doc["currentEpoch"] = timeClient->getEpochTime();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleTimerEnd() {
  addCORS();
  if (!server.hasArg("plain")) { 
    DynamicJsonDocument doc(256);
    doc["animMode"] = timerEndAnimMode;
    doc["color"] = timerEndColor;
    doc["duration"] = timerEndDuration;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
    return;
  }

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }
  
  if (doc.containsKey("animMode")) timerEndAnimMode = doc["animMode"].as<int>();
  if (doc.containsKey("color")) timerEndColor = doc["color"].as<uint32_t>();
  if (doc.containsKey("duration")) timerEndDuration = doc["duration"].as<unsigned long>();
  
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLcdStatus() {
  addCORS();
  DynamicJsonDocument doc(256);
  doc["row1"] = lcdRow1;
  doc["row2"] = lcdRow2;
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleNeopixelStatus() {
  addCORS();
  DynamicJsonDocument doc(768);
  doc["enabled"] = neoState.enabled;
  doc["brightness"] = neoState.brightness;
  doc["animationMode"] = neoState.animationMode;
  doc["alarmAnimMode"] = neoState.alarmAnimMode;
  doc["alarmActive"] = neoState.alarmActive;
  doc["progressPercent"] = (float)neoState.progressPercent;
  doc["currentPrayerColor"] = neoState.currentPrayerColor;
  
  JsonArray colors = doc.createNestedArray("prayerColors");
  for (int i = 0; i < 6; i++) {
    colors.add(prayerColors[i]);
  }
  
  JsonArray ledColors = doc.createNestedArray("ledColors");
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    uint32_t color = pixels.getPixelColor(i);
    char hexColor[8];
    sprintf(hexColor, "#%02X%02X%02X", 
      (color >> 8) & 0xFF,
      color & 0xFF,
      (color >> 16) & 0xFF);
    ledColors.add(hexColor);
  }
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleNeopixelUpdate() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
  }

  if (doc.containsKey("enabled")) {
    neoState.enabled = doc["enabled"].as<bool>();
    if (!neoState.enabled) {
      neoOff();
    }
  }
  if (doc.containsKey("brightness")) neoSetBrightness(doc["brightness"].as<int>());
  if (doc.containsKey("animationMode")) neoState.animationMode = doc["animationMode"].as<int>();
  if (doc.containsKey("alarmAnimMode")) neoState.alarmAnimMode = doc["alarmAnimMode"].as<int>();
  
  if (doc.containsKey("prayerColors")) {
    JsonArray colors = doc["prayerColors"].as<JsonArray>();
    int idx = 0;
    for (JsonVariant c : colors) {
      if (idx < 6 && c.is<uint32_t>()) {
        prayerColors[idx] = c.as<uint32_t>();
      }
      idx++;
    }
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleNeopixelProgress() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400); return; }
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }
  
  float percent = doc.containsKey("percent") ? doc["percent"].as<float>() : 0;
  int prayerIdx = doc.containsKey("prayerIdx") ? doc["prayerIdx"].as<int>() : 0;
  
  neoSetProgress(percent, prayerIdx);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleNeopixelAlarm() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400); return; }
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }
  
  String action = doc.containsKey("action") ? doc["action"].as<String>() : "";
  
  if (action == "start") {
    neoState.alarmActive = true;
    neoState.alarmStartTime = millis();
    startSpeakerAlarm(speakerState.volume, speakerState.duration);
    buzzerPulse(3);
    server.send(200, "application/json", "{\"ok\":true,\"status\":\"alarm started\"}");
  } else if (action == "stop") {
    neoState.alarmActive = false;
    stopSpeakerAlarm();
    neoOff();
    server.send(200, "application/json", "{\"ok\":true,\"status\":\"alarm stopped\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown action\"}");
  }
}

void handleSpeakerStatus() {
  addCORS();
  DynamicJsonDocument doc(256);
  doc["volume"] = speakerState.volume;
  doc["duration"] = speakerState.duration;
  doc["isPlaying"] = speakerState.isPlaying;
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSpeakerUpdate() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
  }

  if (doc.containsKey("volume")) {
    speakerState.volume = constrain(doc["volume"].as<int>(), 0, 150);
  }
  if (doc.containsKey("duration")) {
    speakerState.duration = constrain(doc["duration"].as<int>(), 1, 60);
  }

  if (doc.containsKey("melody") && doc.containsKey("noteDurations")) {
    JsonArray m = doc["melody"].as<JsonArray>();
    JsonArray d = doc["noteDurations"].as<JsonArray>();
    if (!m.isNull() && !d.isNull() && m.size() == d.size() && m.size() > 0) {
      int len = (int)m.size();
      if (len > MAX_MELODY_LEN) len = MAX_MELODY_LEN;
      melodyLength = len;
      for (int i = 0; i < len; i++) {
        melody[i] = m[i].as<int>();
        noteDurations[i] = d[i].as<int>();
      }
    }
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSpeakerTest() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400); return; }
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }
  
  int volume = doc.containsKey("volume") ? doc["volume"].as<int>() : 100;
  int duration = doc.containsKey("duration") ? doc["duration"].as<int>() : 30;

  if (doc.containsKey("melody") && doc.containsKey("noteDurations")) {
    JsonArray m = doc["melody"].as<JsonArray>();
    JsonArray d = doc["noteDurations"].as<JsonArray>();
    if (!m.isNull() && !d.isNull() && m.size() == d.size() && m.size() > 0) {
      int len = (int)m.size();
      if (len > MAX_MELODY_LEN) len = MAX_MELODY_LEN;
      melodyLength = len;
      for (int i = 0; i < len; i++) {
        melody[i] = m[i].as<int>();
        noteDurations[i] = d[i].as<int>();
      }
    }
  }
  
  startSpeakerAlarm(volume, duration);
  server.send(200, "application/json", "{\"ok\":true,\"status\":\"speaker test started\"}");
}

void handleBuzzerTest() {
  addCORS();
  int durationMs = BUZZ_NOTIFY_MS;
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(128);
    if (!deserializeJson(doc, server.arg("plain"))) {
      if (doc.containsKey("duration")) {
        durationMs = constrain(doc["duration"].as<int>(), 100, 5000);
      }
    }
  }
  buzzerNotifyMs(durationMs);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleAlarmGet() {
  addCORS();
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("alarms");
  for (int i = 0; i < customAlarmCount; i++) {
    JsonObject a = arr.createNestedObject();
    char timeBuf[6];
    sprintf(timeBuf, "%02d:%02d", customAlarms[i].hour, customAlarms[i].minute);
    a["time"]    = timeBuf;
    a["label"]   = customAlarms[i].label;
    a["enabled"] = customAlarms[i].enabled;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAlarmPost() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

  if (doc.containsKey("alarms")) {
    JsonArray arr = doc["alarms"].as<JsonArray>();
    customAlarmCount = 0;
    for (JsonObject a : arr) {
      if (customAlarmCount >= MAX_ALARMS) break;
      String t = a["time"].as<String>();
      if (t.length() < 5 || t[2] != ':') continue;
      customAlarms[customAlarmCount].hour    = t.substring(0, 2).toInt();
      customAlarms[customAlarmCount].minute  = t.substring(3, 5).toInt();
      customAlarms[customAlarmCount].enabled = a.containsKey("enabled") ? a["enabled"].as<bool>() : true;
      customAlarms[customAlarmCount].valid   = true;
      String lbl = a.containsKey("label") ? a["label"].as<String>() : "";
      lbl.toCharArray(customAlarms[customAlarmCount].label, 25);
      customAlarmCount++;
    }
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"missing alarms array\"}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);
  delay(100);
  
  lcd.init();
  lcd.backlight();
  lcdMsg("System Boot", "Initializing...");
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  buzzerNotifyMs(BUZZ_NOTIFY_MS); // Boot notify
  
  delay(1000);

  pixels.begin();
  pixels.setBrightness(255);
  neoOff();
  
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);
  
  pinMode(BUTTON_PIN, INPUT);

  Serial.println("\n========== BOOT START ==========");
  lcdMsg("WiFi Setup", "Connecting...");

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
    lcdMsg("WiFi Config", "Failed");
    delay(2000);
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    attempt++;
    lcdProgress("WiFi Connect", attempt % 16, 16);

    int ledsOn = attempt % (NEOPIXEL_COUNT + 1);
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        pixels.setPixelColor(i, pixels.Color(0, 100, 255));
      } else {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
    }
    pixels.show();

    Serial.print(".");
    if (attempt % 40 == 0) {
      Serial.println("\nRetrying WiFi...");
      WiFi.disconnect();
      delay(500);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  Serial.println("\n✓ WiFi Connected! IP: " + WiFi.localIP().toString());
  lcdMsg("WiFi OK", WiFi.localIP().toString().c_str());
  buzzerNotifyMs(BUZZ_NOTIFY_MS); // WiFi connected notify
  
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 255, 0));
  }
  pixels.show();
  delay(500);
  neoOff();
  delay(1500);

  lcdMsg("NTP Sync", "Starting...");
  timeClient = new NTPClient(ntpUDP, NTP_SERVER, utcOffsetH * 3600);
  timeClient->begin();

  int ntpAttempt = 0;
  while (!timeClient->update()) {
    ntpAttempt++;
    lcdProgress("NTP Sync", ntpAttempt % 15, 15);
    
    int ledsOn = ntpAttempt % (NEOPIXEL_COUNT + 1);
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      if (i < ledsOn) {
        pixels.setPixelColor(i, pixels.Color(255, 150, 0));
      } else {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
    }
    pixels.show();
    
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\n✓ NTP OK: " + clockString());
  lcdMsg("NTP OK", clockString().c_str());
  timeSynced = true;
  
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 255, 0));
  }
  pixels.show();
  delay(500);
  neoOff();
  delay(1500);

  lcdMsg("Prayer Times", "Fetching...");
  while (!fetchPrayerTimes()) {
    lcdMsg("Fetch Failed", "Retrying 10s...");
    delay(10000);
    ensureWiFi();
  }
  fetchedToday = true;
  lastFetchDay = timeClient->getDay();

  server.on("/alarm",       HTTP_GET,     handleAlarmGet);
  server.on("/alarm",       HTTP_POST,    handleAlarmPost);
  server.on("/alarm",       HTTP_OPTIONS, handleOptions);
  server.on("/status",  HTTP_GET,     handleStatus);
  server.on("/update",  HTTP_POST,    handleUpdate);
  server.on("/refetch", HTTP_GET,     handleRefetch);
  server.on("/lcd",     HTTP_POST,    handleLcdMsg);
  server.on("/lcd/status", HTTP_GET,  handleLcdStatus);
  server.on("/timer",   HTTP_POST,    handleTimer);
  server.on("/timer/status", HTTP_GET, handleTimerStatus);
  server.on("/timer/end", HTTP_GET,   handleTimerEnd);
  server.on("/timer/end", HTTP_POST,  handleTimerEnd);
  server.on("/neopixel/status", HTTP_GET, handleNeopixelStatus);
  server.on("/neopixel/update", HTTP_POST, handleNeopixelUpdate);
  server.on("/neopixel/progress", HTTP_POST, handleNeopixelProgress);
  server.on("/neopixel/alarm", HTTP_POST, handleNeopixelAlarm);
  server.on("/speaker/status", HTTP_GET, handleSpeakerStatus);
  server.on("/speaker/update", HTTP_POST, handleSpeakerUpdate);
  server.on("/speaker/test", HTTP_POST, handleSpeakerTest);
  server.on("/buzzer/test", HTTP_POST, handleBuzzerTest);
  server.on("/status",  HTTP_OPTIONS, handleOptions);
  server.on("/update",  HTTP_OPTIONS, handleOptions);
  server.on("/refetch", HTTP_OPTIONS, handleOptions);
  server.on("/lcd",     HTTP_OPTIONS, handleOptions);
  server.on("/lcd/status", HTTP_OPTIONS, handleOptions);
  server.on("/timer",   HTTP_OPTIONS, handleOptions);
  server.on("/timer/status", HTTP_OPTIONS, handleOptions);
  server.on("/timer/end", HTTP_OPTIONS, handleOptions);
  server.on("/neopixel/status", HTTP_OPTIONS, handleOptions);
  server.on("/neopixel/update", HTTP_OPTIONS, handleOptions);
  server.on("/neopixel/progress", HTTP_OPTIONS, handleOptions);
  server.on("/neopixel/alarm", HTTP_OPTIONS, handleOptions);
  server.on("/speaker/status", HTTP_OPTIONS, handleOptions);
  server.on("/speaker/update", HTTP_OPTIONS, handleOptions);
  server.on("/speaker/test", HTTP_OPTIONS, handleOptions);
  server.on("/buzzer/test", HTTP_OPTIONS, handleOptions);
  server.begin();

  Serial.println("✓ Web server started");
  Serial.println("========== BOOT COMPLETE ==========\n");
  lcd.clear();
}

void updateNeopixelProgressBar() {
  if (!timeClient || !timingsFetched) return;
  
  int active = activePrayerIdx();
  int now = timeClient->getHours() * 60 + timeClient->getMinutes();
  
  int curr = toMinutes(prayerTimes[active]);
  if (curr < 0) return;
  
  int nextIdx = (active == 5) ? 0 : active + 1;
  if (nextIdx == 1 && active == 0) nextIdx = 2;
  
  int next = toMinutes(prayerTimes[nextIdx]);
  if (next < 0) return;
  
  float progress = 0.0f;
  
  if (next <= curr) {
    if (now >= curr) {
      progress = ((float)(now - curr) / (1440.0f - curr + next)) * 100.0f;
    } else {
      progress = ((float)(1440 + now - curr) / (1440.0f - curr + next)) * 100.0f;
    }
  } else {
    progress = ((float)(now - curr) / (float)(next - curr)) * 100.0f;
  }
  
  progress = constrain(progress, 0.0f, 100.0f);
  neoSetProgress(progress, active);
}

void updateNeopixelTimerBar() {
  if (!timerRunning) return;
  
  unsigned long currentElapsed = timerElapsed + (millis() - timerStartTime);
  float progress = 0.0f;
  
  if (timerMode == 1) {
    unsigned long remaining = (timerDuration > currentElapsed) 
      ? (timerDuration - currentElapsed) 
      : 0;
    progress = (timerDuration > 0) 
      ? ((float)remaining / (float)timerDuration) * 100.0f 
      : 0.0f;
  } else {
    progress = (currentElapsed > 0) ? (currentElapsed / 1000.0f) : 0.0f;
    progress = constrain(progress, 0.0f, 100.0f);
  }
  
  progress = constrain(progress, 0.0f, 100.0f);
  
  int ledsOn = (int)(ceil(NEOPIXEL_COUNT * (progress / 100.0f)));
  uint32_t timerColor = pixels.Color(0, 200, 255);
  
  renderLedStrip(ledsOn, neoState.animationMode, timerColor);
  
  neoState.progressPercent = progress;
}

void loop() {
  server.handleClient();
  
  updateSpeaker();
  updateBuzzer();
  updateButton();
  
  if (timerRunning && timerMode == 1) {
    unsigned long currentElapsed = timerElapsed + (millis() - timerStartTime);
    if (currentElapsed >= timerDuration && !timerEnded) {
      timerEnded = true;
      timerRunning = false;
      timerEndStartTime = millis();
      buzzerPulse(5);
    }
  }
  
  if (timerEnded && neoState.enabled) {
    unsigned long elapsed = millis() - timerEndStartTime;
    if (elapsed < timerEndDuration) {
      neoTimerEndAnimation(elapsed);
    } else {
      timerEnded = false;
      updateNeopixelProgressBar();
    }
  }
  
  if (neoState.alarmActive && neoState.enabled) {
    unsigned long elapsed = millis() - neoState.alarmStartTime;
    if (elapsed < 600000) { // Limit to 10 mins
      neoAlarmAnimation(elapsed);
    } else {
      neoState.alarmActive = false;
      stopSpeakerAlarm();
      neoOff();
    }
  }
  
  ensureWiFi();

  if (timeClient && timeClient->update()) {
    timeSynced = true;
  }
  unsigned long now = millis();

  if (timeClient) {
    int h   = timeClient->getHours();
    int day = timeClient->getDay();
    if (h == 6 && day != lastFetchDay) {
      if (fetchPrayerTimes()) {
        fetchedToday = true;
        lastFetchDay = day;
      }
    }

    // Prayer Trigger logic
    int now_m = timeClient->getHours() * 60 + timeClient->getMinutes();
    int now_s = timeClient->getSeconds();
    static int lastTriggeredMinute = -1;
    if (now_s == 0 && now_m != lastTriggeredMinute) {
      for (int i = 0; i < 6; i++) {
        if (i == 1) continue; // Skip sunrise
        if (toMinutes(prayerTimes[i]) == now_m) {
          buzzerNotifyMs(BUZZ_NOTIFY_MS); // Notify on prayer time start
          lastTriggeredMinute = now_m;
          break;
        }
      }
    }

    // Custom alarm trigger
    if (now_s == 0 && now_m != lastAlarmTriggeredMinute) {
      int curH = timeClient->getHours();
      int curMin = timeClient->getMinutes();
      for (int i = 0; i < customAlarmCount; i++) {
        if (customAlarms[i].enabled && customAlarms[i].valid &&
            customAlarms[i].hour == curH && customAlarms[i].minute == curMin) {
          startSpeakerAlarm(speakerState.volume, speakerState.duration);
          buzzerPulse(5);
          neoState.alarmActive = true;
          neoState.alarmStartTime = millis();
          lastAlarmTriggeredMinute = now_m;
          break;
        }
      }
    }
  }

  static unsigned long lastUpdate = 0;
  if (now - lastUpdate > 100) {
    lastUpdate = now;
    
    if (now - lastClockUpdate > CLOCK_INTERVAL) {
      lastClockUpdate = now;
      if (timeClient) {
        int sec = timeClient->getSeconds();
        datePhase = (sec / 3) % 3;
      }
      updateDisplay();
    }

    if (neoState.enabled && !timerEnded && !neoState.alarmActive) {
      if (timerRunning) {
        updateNeopixelTimerBar();
      } else {
        updateNeopixelProgressBar();
      }
    }
  }

  if (now - lastScrollTime > SCROLL_INTERVAL) {
    lastScrollTime   = now;
    currentPrayerIdx = (currentPrayerIdx + 1) % 6;
    updateDisplay();
  }
}
