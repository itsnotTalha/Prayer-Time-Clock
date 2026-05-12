
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
#define BUZZER_PIN1     27      // GPIO27 for buzzer 1
#define BUZZER_PIN2     26      // GPIO26 for buzzer 2
#define BUTTON_PIN      33      // GPIO35 for momentary button

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
  int alarmFreq = 2000;   // Hz (max 5000)
  int alarmMelody = 0;    // Melody index
  int alarmDuration = 4;  // Duration in seconds
  bool alarmActive = false;
  unsigned long alarmStartTime = 0;
  float progressPercent = 0;
  int currentPrayerColor = 0;
} neoState;

// Buzzer state
struct {
  bool active = false;
  unsigned long startTime = 0;
  int frequency = 2000; // Default to 2kHz
} buzzerState;

// ─── MELODY PLAYBACK STATE (Non-blocking) ────────────────────────────────────
struct {
  bool isPlaying = false;
  int melodyIdx = 0;
  int baseFreq = 1000;
  int repeat = 0;
  int currentRepeat = 0;
  int currentNote = 0;
  unsigned long startTime = 0;
  unsigned long maxDuration = 0;  // in ms
  unsigned long noteStartTime = 0;
  unsigned long totalElapsed = 0;
} melodyState;

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

// ─── BUZZER & MELODY FUNCTIONS (NON-BLOCKING) ────────────────────────────────

typedef struct {
  int freq1;
  int duration;
  int freq2;
} MelodyNote;

const MelodyNote ALARM_MELODIES[5][8] = {
  // Classic Beep
  { {1000,200,0}, {1200,200,0}, {1000,200,0}, {1500,400,0}, {0,0,0} },
  // Ascending
  { {800,150,0}, {1000,150,0}, {1200,150,0}, {1500,400,0}, {0,0,0} },
  // Descending
  { {2000,150,0}, {1500,150,0}, {1200,150,0}, {900,400,0}, {0,0,0} },
  // Chirp
  { {1000,80,0}, {2000,80,0}, {1000,80,0}, {2000,80,0}, {1000,300,0}, {0,0,0} },
  // Twin Buzzer
  { {1000,100,1500}, {1200,100,1700}, {1000,100,1500}, {1200,300,1700}, {0,0,0} }
};

void buzzerStop() {
  buzzerState.active = false;
  noTone(BUZZER_PIN1);
  noTone(BUZZER_PIN2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// *** NEW: Non-blocking melody playback ***
// ═══════════════════════════════════════════════════════════════════════════════
void startMelodyAsync(int melodyIdx, int baseFreq, int repeat, int totalDurationSec) {
  melodyIdx = constrain(melodyIdx, 0, 4);
  melodyState.isPlaying = true;
  melodyState.melodyIdx = melodyIdx;
  melodyState.baseFreq = baseFreq;
  melodyState.repeat = repeat;
  melodyState.currentRepeat = 0;
  melodyState.currentNote = 0;
  melodyState.startTime = millis();
  melodyState.maxDuration = totalDurationSec > 0 ? totalDurationSec * 1000UL : 0;
  melodyState.noteStartTime = millis();
  melodyState.totalElapsed = 0;
}

void updateMelodyPlayback() {
  if (!melodyState.isPlaying) return;
  
  unsigned long now = millis();
  unsigned long elapsed = now - melodyState.startTime;
  
  // Check if max duration exceeded
  if (melodyState.maxDuration > 0 && elapsed >= melodyState.maxDuration) {
    melodyState.isPlaying = false;
    noTone(BUZZER_PIN1);
    noTone(BUZZER_PIN2);
    return;
  }
  
  // Get current note
  MelodyNote note = ALARM_MELODIES[melodyState.melodyIdx][melodyState.currentNote];
  
  // Check if note finished
  if (note.freq1 == 0 || note.duration == 0) {
    // Move to next note or repeat
    melodyState.currentNote++;
    
    // Find if there are more notes
    if (melodyState.currentNote >= 8 || ALARM_MELODIES[melodyState.melodyIdx][melodyState.currentNote].freq1 == 0) {
      melodyState.currentRepeat++;
      if (melodyState.currentRepeat >= melodyState.repeat) {
        melodyState.isPlaying = false;
        noTone(BUZZER_PIN1);
        noTone(BUZZER_PIN2);
        return;
      }
      melodyState.currentNote = 0;
    }
    melodyState.noteStartTime = now;
    return;
  }
  
  // Check if current note duration exceeded
  unsigned long noteDuration = now - melodyState.noteStartTime;
  if (noteDuration > (unsigned long)note.duration + 30) {
    // Note + gap finished
    noTone(BUZZER_PIN1);
    noTone(BUZZER_PIN2);
    melodyState.noteStartTime = now;
    melodyState.currentNote++;
    return;
  }
  
  // Still playing this note
  if (noteDuration <= (unsigned long)note.duration) {
    int f1 = note.freq1 > 0 ? note.freq1 : melodyState.baseFreq;
    int f2 = note.freq2 > 0 ? note.freq2 : 0;
    tone(BUZZER_PIN1, f1);
    if (f2) tone(BUZZER_PIN2, f2);
  }
}

void buzzerStart(int frequency) {
  frequency = constrain(frequency, 100, 5000);
  buzzerState.active = true;
  buzzerState.frequency = frequency;
  buzzerState.startTime = millis();
}

void buzzerUpdate() {
  if (!buzzerState.active) return;
  tone(BUZZER_PIN1, buzzerState.frequency);
  tone(BUZZER_PIN2, buzzerState.frequency);
}

void buzzerTest(int frequency, unsigned long durationMs) {
  int dur = (neoState.alarmDuration > 0) ? neoState.alarmDuration : 4;
  startMelodyAsync(neoState.alarmMelody, frequency, 1, dur);
}

// ─── IMPROVED BUTTON HANDLING ─────────────────────────────────────────────────

void handleButtonPress(int pressType) {
  Serial.print("Button pressed: type=");
  Serial.println(pressType);
  
  if (pressType == 1) {
    // Single press: toggle LED strip
    if (!neoState.enabled) {
      neoState.enabled = true;
      pixels.setBrightness(neoState.brightness);
      updateNeopixelProgressBar();
    } else {
      neoOff();
      neoState.enabled = false;
    }
  } else if (pressType == 2) {
    // Double press: stop alarm
    Serial.println("Double press - stopping alarm");
    melodyState.isPlaying = false;
    neoState.alarmActive = false;
    buzzerStop();
    neoOff();
  } else if (pressType == 3) {
    // Long press: toggle sleep mode
    Serial.println("Long press - toggling sleep mode");
    if (!neoState.enabled && !lcdBacklight) {
      // Wake up
      neoState.enabled = true;
      pixels.setBrightness(neoState.brightness);
      updateNeopixelProgressBar();
      lcdBacklight = true;
      lcd.backlight();
    } else {
      // Sleep
      neoOff();
      neoState.enabled = false;
      lcd.noBacklight();
      lcdBacklight = false;
    }
  }
}

void updateButtonState() {
  bool buttonState = !digitalRead(BUTTON_PIN);  // LOW = pressed
  unsigned long now = millis();
  
  if (buttonState && !buttonPressed) {
    // Button just pressed
    buttonPressed = true;
    buttonPressTime = now;
    pressCount = 0;  // Reset counter on new press
  } else if (!buttonState && buttonPressed) {
    // Button just released
    unsigned long pressDuration = now - buttonPressTime;
    buttonPressed = false;
    lastPressEndTime = now;
    
    if (pressDuration >= LONG_PRESS_TIME) {
      // Long press detected
      handleButtonPress(3);
      pressCount = 0;
    } else {
      // Short press - increment counter
      pressCount++;
    }
  }
  
  // Finalize single/double press
  if (pressCount > 0 && !buttonPressed && (now - lastPressEndTime) >= DOUBLE_CLICK_WINDOW) {
    if (pressCount == 1) {
      handleButtonPress(1);
    } else if (pressCount >= 2) {
      handleButtonPress(2);
    }
    pressCount = 0;
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
  doc["alarmFreq"] = neoState.alarmFreq;
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
  if (doc.containsKey("alarmFreq")) neoState.alarmFreq = constrain(doc["alarmFreq"].as<int>(), 100, 5000);
  if (doc.containsKey("alarmMelody")) neoState.alarmMelody = constrain(doc["alarmMelody"].as<int>(), 0, 4);
  if (doc.containsKey("alarmDuration")) neoState.alarmDuration = constrain(doc["alarmDuration"].as<int>(), 1, 30);
  
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
    // *** FIXED: Use async melody instead of blocking playMelody() ***
    neoState.alarmActive = true;
    neoState.alarmStartTime = millis();
    int dur = (neoState.alarmDuration > 0) ? neoState.alarmDuration : 4;
    startMelodyAsync(neoState.alarmMelody, neoState.alarmFreq, 2, dur);
    server.send(200, "application/json", "{\"ok\":true,\"status\":\"alarm started\"}");
  } else if (action == "stop") {
    neoState.alarmActive = false;
    melodyState.isPlaying = false;
    buzzerStop();
    neoOff();
    server.send(200, "application/json", "{\"ok\":true,\"status\":\"alarm stopped\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown action\"}");
  }
}

void handleBuzzerTest() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400); return; }
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }
  
  int freq = doc.containsKey("frequency") ? doc["frequency"].as<int>() : 1000;
  unsigned long dur = doc.containsKey("duration") ? doc["duration"].as<unsigned long>() : 500;
  
  freq = constrain(freq, 100, 2000);
  dur = constrain(dur, 100, 5000);
  
  buzzerTest(freq, dur);
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);
  delay(100);
  
  lcd.init();
  lcd.backlight();
  lcdMsg("System Boot", "Initializing...");
  delay(1000);

  pixels.begin();
  pixels.setBrightness(255);
  neoOff();
  
  pinMode(BUZZER_PIN1, OUTPUT);
  pinMode(BUZZER_PIN2, OUTPUT);
  noTone(BUZZER_PIN1);
  noTone(BUZZER_PIN2);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);

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
  
  // *** UPDATE MELODY PLAYBACK (non-blocking) ***
  updateMelodyPlayback();
  
  updateButtonState();
  
  if (timerRunning && timerMode == 1) {
    unsigned long currentElapsed = timerElapsed + (millis() - timerStartTime);
    if (currentElapsed >= timerDuration && !timerEnded) {
      timerEnded = true;
      timerRunning = false;
      timerEndStartTime = millis();
      buzzerStart(1500);
    }
  }
  
  if (timerEnded && neoState.enabled) {
    unsigned long elapsed = millis() - timerEndStartTime;
    if (elapsed < timerEndDuration) {
      neoTimerEndAnimation(elapsed);
      buzzerUpdate();
    } else {
      timerEnded = false;
      buzzerStop();
      updateNeopixelProgressBar();
    }
  }
  
  if (neoState.alarmActive && neoState.enabled) {
    unsigned long elapsed = millis() - neoState.alarmStartTime;
    if (elapsed < 30000) {
      neoAlarmAnimation(elapsed);
    } else {
      neoState.alarmActive = false;
      melodyState.isPlaying = false;
      neoOff();
      buzzerStop();
    }
  }
  
  ensureWiFi();

  if (timeClient) timeClient->update();
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
