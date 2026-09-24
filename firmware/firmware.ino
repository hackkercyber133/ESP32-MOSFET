
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <time.h>

#include <Wire.h>
#include <esp_random.h>
#include <CH224X_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "FluxGarage_RoboEyes.h"

Preferences prefs;

String deviceId;
String bleName;
bool deviceConnected = false;
bool peltierOn = false;
String netMode;

String computeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  // Pakai seluruh 48-bit MAC hardware, bukan hanya 24-bit terakhir.
  // Enam digit masih cukup sering terlihat unik, tetapi bisa bentrok pada
  // dua board atau tertukar oleh cache nama BLE Android.
  char buf[13];
  snprintf(buf, sizeof(buf), "%012llX",
           (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
  return String(buf);
}

#define SDA_PIN      5
#define SCL_PIN      6
#define PG_PIN       7

#define OLED_ADDR   0x3C
#define OLED_WIDTH  128
#define OLED_HEIGHT 32
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;

U8G2_FOR_ADAFRUIT_GFX u8f;

RoboEyes<Adafruit_SSD1306> roboEyes(oled);

GFXcanvas1 oledLogoCanvas(84, 10);

int oledSession = 0;
unsigned long lastOledSessionSwitch = 0;
unsigned long lastOledClockTick = 0;

int oledEyeMood = -1;

unsigned long oledForcedUntil = 0;
const unsigned long OLED_INTERACTION_SHOW_MS = 3000;

String oledCustomText = "";
int oledMarqueeX = 0;
unsigned long lastOledMarqueeStep = 0;
const unsigned long OLED_MARQUEE_FPS_MS = 30;

int oledTextSpeedPercent = 50;

void setOledTextSpeed(int percent) {
  if (percent < 1) percent = 1;
  if (percent > 100) percent = 100;
  oledTextSpeedPercent = percent;
  if (prefs.getInt("oledTextSpeed", -1) != oledTextSpeedPercent) {
    prefs.putInt("oledTextSpeed", oledTextSpeedPercent);
  }
}

int oledMarqueeStepPx() {
  return map(oledTextSpeedPercent, 1, 100, 1, 6);
}

#define OLED_TEXT_MAX_LEN 60

int fanSpeedPercent = 100;
volatile uint32_t fanTachPulseCount = 0;
unsigned int fanRpm = 0;
unsigned long lastFanRpmCalc = 0;

int ledSpeedPercent = 50;

void setLedSpeed(int percent) {
  if (percent < 1) percent = 1;
  if (percent > 100) percent = 100;
  ledSpeedPercent = percent;
  if (prefs.getInt("ledSpeed", -1) != ledSpeedPercent) {
    prefs.putInt("ledSpeed", ledSpeedPercent);
  }
}

unsigned long ledStepDelay(unsigned long baseMs) {
  float factor = (110.0f - (float)ledSpeedPercent) / 60.0f;
  unsigned long scaled = (unsigned long)((float)baseMs * factor);
  if (scaled < 4) scaled = 4;
  return scaled;
}

#define PIN_LED_DATA 4
#define NUM_LEDS 30
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);
uint32_t wheelColor(byte pos);

String ledMode = "off";
String lastLedEffect = "running";
unsigned long lastLedStep = 0;
uint16_t rainbowStep = 0;
int bouncePos = 0;
int bounceDir = 1;
float colorwavePhase = 0;
uint8_t confettiFade[NUM_LEDS];

uint8_t customR = 255, customG = 255, customB = 255;

String customPattern = "solid";
int ledBrightnessPercent = 31;

uint32_t ledColorScaled(uint8_t intensity) {
  return strip.Color(
    ((uint16_t)customR * intensity) / 255,
    ((uint16_t)customG * intensity) / 255,
    ((uint16_t)customB * intensity) / 255
  );
}

#define WLED_EFFECT_COUNT 100

// 100 mode dari firmware baru: 5 mesin animasi x 20 palet.
// ID publik tetap wled001 ... wled100 supaya kompatibel dengan aplikasi lama.
const uint32_t PALETTE_FIRE[]      = {0xFF0000, 0xFF4500, 0xFFA500, 0xFFFF00};
const uint32_t PALETTE_ICE[]       = {0x001133, 0x0066CC, 0x66CCFF, 0xFFFFFF};
const uint32_t PALETTE_OCEAN[]     = {0x001F3F, 0x0074D9, 0x39CCCC, 0x7FDBFF};
const uint32_t PALETTE_FOREST[]    = {0x013220, 0x228B22, 0x6B8E23, 0x9ACD32};
const uint32_t PALETTE_SUNSET[]    = {0xFF4500, 0xFF6347, 0xFFD700, 0xFF1493};
const uint32_t PALETTE_PARTY[]     = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF};
const uint32_t PALETTE_RED[]       = {0xFF0000, 0x800000};
const uint32_t PALETTE_GREEN[]     = {0x00FF00, 0x006400};
const uint32_t PALETTE_BLUE[]      = {0x0000FF, 0x000080};
const uint32_t PALETTE_PURPLE[]    = {0x800080, 0x4B0082, 0xEE82EE};
const uint32_t PALETTE_WARMWHITE[] = {0xFFE4B5, 0xFFDAB9, 0xFFFFFF};
const uint32_t PALETTE_COOLWHITE[] = {0xE0FFFF, 0xFFFFFF, 0xADD8E6};
const uint32_t PALETTE_PASTEL[]    = {0xFFD1DC, 0xE6E6FA, 0xC1FFC1, 0xB0E0E6};
const uint32_t PALETTE_REDBLUE[]   = {0xFF0000, 0x0000FF};
const uint32_t PALETTE_PINKCYAN[]  = {0xFF1493, 0x00FFFF};
const uint32_t PALETTE_GOLD[]      = {0xFFD700, 0xB8860B};
const uint32_t PALETTE_NEON[]      = {0x39FF14, 0xFF073A, 0x04D9FF, 0xFE01B1};
const uint32_t PALETTE_CANDY[]     = {0xFF69B4, 0xFFB6C1, 0xFFFFFF};
const uint32_t PALETTE_MONO[]      = {0xFFFFFF};

struct PaletteInfo { const uint32_t* colors; uint8_t count; };
const PaletteInfo WLED_PALETTES[] = {
  { nullptr,             0 },
  { PALETTE_FIRE,        4 },
  { PALETTE_ICE,         4 },
  { PALETTE_OCEAN,       4 },
  { PALETTE_FOREST,      4 },
  { PALETTE_SUNSET,      4 },
  { PALETTE_PARTY,       6 },
  { PALETTE_RED,         2 },
  { PALETTE_GREEN,       2 },
  { PALETTE_BLUE,        2 },
  { PALETTE_PURPLE,      3 },
  { PALETTE_WARMWHITE,   3 },
  { PALETTE_COOLWHITE,   3 },
  { PALETTE_PASTEL,      4 },
  { PALETTE_REDBLUE,     2 },
  { PALETTE_PINKCYAN,    2 },
  { PALETTE_GOLD,        2 },
  { PALETTE_NEON,        4 },
  { PALETTE_CANDY,       3 },
  { PALETTE_MONO,        1 },
};

const uint16_t WLED_ENGINE_SPEED_MS[5] = {60, 25, 20, 50, 30};
uint8_t wledColorR[WLED_EFFECT_COUNT];
uint8_t wledColorG[WLED_EFFECT_COUNT];
uint8_t wledColorB[WLED_EFFECT_COUNT];

void loadWledColors() {
  // Versi 2 memakai putih sebagai tint awal, sehingga palet dari firmware
  // baru tampil utuh. Warna custom tetap disimpan terpisah untuk tiap mode.
  uint8_t buf[WLED_EFFECT_COUNT * 3];
  size_t got = prefs.getBytes("wledColorsV2", buf, sizeof(buf));
  if (got == sizeof(buf)) {
    for (int i = 0; i < WLED_EFFECT_COUNT; i++) {
      wledColorR[i] = buf[i * 3];
      wledColorG[i] = buf[i * 3 + 1];
      wledColorB[i] = buf[i * 3 + 2];
    }
    return;
  }
  for (int i = 0; i < WLED_EFFECT_COUNT; i++) {
    wledColorR[i] = 255;
    wledColorG[i] = 255;
    wledColorB[i] = 255;
  }
  for (int i = 0; i < WLED_EFFECT_COUNT; i++) {
    buf[i * 3] = 255;
    buf[i * 3 + 1] = 255;
    buf[i * 3 + 2] = 255;
  }
  prefs.putBytes("wledColorsV2", buf, sizeof(buf));
  // Hapus format lama agar warna default lama tidak muncul lagi setelah update.
  prefs.remove("wledColors");
}

void saveWledColors() {
  uint8_t buf[WLED_EFFECT_COUNT * 3];
  for (int i = 0; i < WLED_EFFECT_COUNT; i++) {
    buf[i * 3] = wledColorR[i];
    buf[i * 3 + 1] = wledColorG[i];
    buf[i * 3 + 2] = wledColorB[i];
  }
  prefs.putBytes("wledColorsV2", buf, sizeof(buf));
}

bool getWledModeInfo(int effectId, int8_t& engine, int8_t& palette, uint16_t& speedMs) {
  if (effectId < 1 || effectId > WLED_EFFECT_COUNT) return false;
  const int slot = effectId - 1;
  engine = slot / 20;
  palette = slot % 20;
  speedMs = WLED_ENGINE_SPEED_MS[engine];
  return true;
}

uint32_t wledPaletteColor(int paletteIdx, uint8_t pos) {
  if (paletteIdx <= 0 || paletteIdx >= (int)(sizeof(WLED_PALETTES) / sizeof(WLED_PALETTES[0]))) {
    return wheelColor(pos);
  }
  const PaletteInfo& pal = WLED_PALETTES[paletteIdx];
  if (pal.count == 0) return wheelColor(pos);
  if (pal.count == 1) return pal.colors[0];

  float scaled = (pos / 256.0f) * pal.count;
  int idxA = ((int)scaled) % pal.count;
  int idxB = (idxA + 1) % pal.count;
  float frac = scaled - (int)scaled;
  uint32_t ca = pal.colors[idxA], cb = pal.colors[idxB];
  uint8_t ra = (ca >> 16) & 0xFF, ga = (ca >> 8) & 0xFF, ba = ca & 0xFF;
  uint8_t rb = (cb >> 16) & 0xFF, gb = (cb >> 8) & 0xFF, bb = cb & 0xFF;
  return strip.Color(
    ra + (rb - ra) * frac,
    ga + (gb - ga) * frac,
    ba + (bb - ba) * frac
  );
}

// Warna dari color wheel menjadi tint untuk palet mode, bukan mengganti mesin
// animasinya. Dengan begitu warna bisa dicustom tanpa menghilangkan gerakan.
uint32_t wledColorAt(int effectId, uint8_t pos, uint8_t intensity) {
  int8_t engine, palette;
  uint16_t speedMs;
  if (!getWledModeInfo(effectId, engine, palette, speedMs)) return strip.Color(0, 0, 0);
  uint32_t base = wledPaletteColor(palette, pos);
  uint8_t r = ((base >> 16) & 0xFF) * wledColorR[effectId - 1] / 255;
  uint8_t g = ((base >> 8) & 0xFF) * wledColorG[effectId - 1] / 255;
  uint8_t b = (base & 0xFF) * wledColorB[effectId - 1] / 255;
  return strip.Color(
    ((uint16_t)r * intensity) / 255,
    ((uint16_t)g * intensity) / 255,
    ((uint16_t)b * intensity) / 255
  );
}

void setWledColor(int effectId, uint8_t r, uint8_t g, uint8_t b) {
  int idx = effectId - 1;
  if (idx < 0 || idx >= WLED_EFFECT_COUNT) return;
  wledColorR[idx] = r;
  wledColorG[idx] = g;
  wledColorB[idx] = b;
  saveWledColors();
}

void applyLedMode(String mode);

void setLedBrightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  ledBrightnessPercent = percent;
  strip.setBrightness((uint8_t)map(percent, 0, 100, 0, 255));
  strip.show();
  if (prefs.getInt("ledBright", -1) != ledBrightnessPercent) {
    prefs.putInt("ledBright", ledBrightnessPercent);
  }
}

void setCustomColor(uint8_t r, uint8_t g, uint8_t b) {

  customR = r;
  customG = g;
  customB = b;

  uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (prefs.getUInt("customColor", 0xFFFFFFu) != packed) {
    prefs.putUInt("customColor", packed);
  }

  if (ledMode == "static" || (ledMode == "custom" && customPattern == "solid")) {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, ledColorScaled(255));
    }
    strip.show();
  }
}

bool isValidCustomPattern(String p) {
  return p == "solid" || p == "chaseRight" || p == "chaseLeft" || p == "pingpong" ||
         p == "pulse" || p == "breathe" || p == "rainbow" || p == "rainbowRight" ||
         p == "rainbowLeft" || p == "spinCW" || p == "spinCCW" || p == "theaterChase" ||
         p == "cometRight" || p == "cometLeft" || p == "sparkle" || p == "twinkle" ||
         p == "wipeRight" || p == "wipeLeft" || p == "centerOut" || p == "edgeIn";
}

void setCustomPattern(String pattern) {
  if (!isValidCustomPattern(pattern)) return;
  customPattern = pattern;
  if (prefs.getString("customPattern", "") != customPattern) {
    prefs.putString("customPattern", customPattern);
  }
  if (ledMode == "custom") {
    applyLedMode("custom");
  }
}

#define FAN_PWM_PIN   2
#define FAN_TACH_PIN  3
#define FAN_PWM_CHANNEL   0
#define FAN_PWM_FREQ_HZ   25000
#define FAN_PWM_RESOLUTION 8

void IRAM_ATTR fanTachISR() {
  fanTachPulseCount++;
}

void setFanSpeed(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  fanSpeedPercent = percent;
  uint8_t duty = (uint8_t)map(percent, 0, 100, 0, 255);
  ledcWrite(FAN_PWM_PIN, duty);

  if (prefs.getInt("fanSpeed", -1) != fanSpeedPercent) {
    prefs.putInt("fanSpeed", fanSpeedPercent);
  }
}

void updateFanRpm() {
  noInterrupts();
  uint32_t pulses = fanTachPulseCount;
  fanTachPulseCount = 0;
  interrupts();

  unsigned long elapsedMs = millis() - lastFanRpmCalc;
  lastFanRpmCalc = millis();
  if (elapsedMs == 0) return;

  fanRpm = (unsigned int)((pulses / 2.0) * (60000.0 / elapsedMs));
}

#define PIN_ONBOARD_LED 8
#define BOOT_BTN_PIN 9
#define ONBOARD_LED_ACTIVE_LOW true

void onboardLedWrite(bool on) {
  digitalWrite(PIN_ONBOARD_LED, (ONBOARD_LED_ACTIVE_LOW ? !on : on) ? LOW : HIGH);
}

bool cmdBlinkActive = false;
unsigned long cmdBlinkStart = 0;
const unsigned long CMD_BLINK_MS = 150;

unsigned long lastAppContact = 0;
bool statusBlinkOn = false;
unsigned long lastStatusBlinkToggle = 0;
const unsigned long STATUS_BLINK_INTERVAL_MS = 500;
const unsigned long APP_CONTACT_TIMEOUT_MS = 5000;

void triggerCmdBlink() {
  cmdBlinkActive = true;
  cmdBlinkStart = millis();
}

bool isAppConnected() {
  return deviceConnected || (millis() - lastAppContact < APP_CONTACT_TIMEOUT_MS);
}

void handleStatusLed() {
  if (isAppConnected()) {
    if (cmdBlinkActive) {
      if (millis() - cmdBlinkStart < CMD_BLINK_MS) {
        onboardLedWrite(true);
      } else {
        cmdBlinkActive = false;
        onboardLedWrite(false);
      }
    } else {
      onboardLedWrite(false);
    }
  } else {
    if (millis() - lastStatusBlinkToggle >= STATUS_BLINK_INTERVAL_MS) {
      lastStatusBlinkToggle = millis();
      statusBlinkOn = !statusBlinkOn;
    }
    onboardLedWrite(statusBlinkOn);
  }
}

float currentSetVoltage = 5.0;
unsigned long startMillis = 0;
unsigned long lastPublish = 0;
float current = 0;
float chargerwatt = 0;
bool pgood = 0;
bool ch224aReady = false;
String pdStatus = "CH224A_NOT_READY";

void updatePdStatus() {
  if (!ch224aReady) {
    pdStatus = "CH224A_NOT_READY";
  } else if (pgood) {
    pdStatus = "PD_NEGOTIATED";
  } else {
    pdStatus = "PD_WAITING";
  }
}

void drawOledStatusScreen(const char* label, const String& value, uint8_t valueSize) {
  if (oledCustomText.length() > 0) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  int16_t x1, y1; uint16_t w, h;

  oled.setTextSize(1);
  oled.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor(max(0, (OLED_WIDTH - (int)w) / 2), 2);
  oled.print(label);

  oled.drawFastHLine(0, 13, OLED_WIDTH, SSD1306_WHITE);

  oled.setTextSize(valueSize);
  oled.getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor(max(0, (OLED_WIDTH - (int)w) / 2), valueSize >= 2 ? 17 : 20);
  oled.print(value);

  oled.display();
}

String oledRgbModeLabel(const String& mode) {
  if (mode == "off") return "OFF";
  if (mode == "static") return "STATIC";
  if (mode == "running") return "RUN";
  if (mode == "disco") return "DISCO";
  if (mode == "bounce") return "BOUNCE";
  if (mode == "knight") return "KNIGHT";
  if (mode == "fire") return "FIRE";
  if (mode == "chase") return "CHASE";
  if (mode == "colorwave") return "WAVE";
  if (mode == "custom") return "CUSTOM";
  return mode;
}

String oledUptimeShort() {
  unsigned long runtime = millis() - startMillis;
  long s = runtime / 1000, m = s / 60, h = m / 60;
  char buf[20];
  snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m % 60, s % 60);
  return String(buf);
}

void drawOledRequest() {
  drawOledStatusScreen("REQUEST", String(currentSetVoltage, 0) + "V", 2);
}

void drawOledPd() {
  String v = !ch224aReady ? "OFF" : (pgood ? "GOOD" : "WAIT");
  drawOledStatusScreen("PD", v, 2);
}

void drawOledUptime() {
  drawOledStatusScreen("UPTIME", oledUptimeShort(), 1);
}

void drawOledWatt() {
  drawOledStatusScreen("CHARGER", String(chargerwatt, 1) + "W", 2);
}

void drawOledMode() {
  drawOledStatusScreen("RGB MODE", oledRgbModeLabel(ledMode), 1);
}

void drawOledPeltier() {
  drawOledStatusScreen("PELTIER", peltierOn ? "ON" : "OFF", 2);
}

void drawOledKoneksi() {
  String mode = (netMode == "wifi") ? "WIFI" : "BLE";
  String v = mode + " " + (isAppConnected() ? "OK" : "OFF");
  drawOledStatusScreen("KONEKSI", v, 1);
}

String oledClockString() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "--:--:--";
  struct tm t;
  localtime_r(&now, &t);
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  return String(buf);
}

void drawOledClock() {
  drawOledStatusScreen("JAM", oledClockString(), 2);
}

void triggerOledSnapshot() {
  if (!oledReady) return;
  if (oledCustomText.length() > 0) return;
  oledForcedUntil = millis() + OLED_INTERACTION_SHOW_MS;
}

String truncateUtf8Safe(const String& input, int maxChars) {
  int charCount = 0;
  size_t i = 0;
  while (i < input.length()) {
    if (charCount >= maxChars) return input.substring(0, i);
    uint8_t c = (uint8_t)input[i];
    int len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    i += len;
    charCount++;
  }
  return input;
}

void setOledCustomText(String text) {
  text.trim();

  text = truncateUtf8Safe(text, OLED_TEXT_MAX_LEN);
  oledCustomText = text;
  if (prefs.getString("oledText", "") != oledCustomText) {
    prefs.putString("oledText", oledCustomText);
  }
  oledMarqueeX = OLED_WIDTH;

  oledSession = 0;
  lastOledSessionSwitch = millis();
  oledEyeMood = -1;
  if (oledReady && oledCustomText.length() == 0) {
    oled.clearDisplay();
    oled.display();
  }
}

void drawOledCustomText() {
  oled.clearDisplay();

  bool hasHangul = false;
  {
    const char* s = oledCustomText.c_str();
    size_t len = oledCustomText.length();
    size_t i = 0;
    while (i < len) {
      uint8_t c = (uint8_t)s[i];
      uint32_t codepoint = c;
      int seqLen = 1;
      if ((c & 0xE0) == 0xC0 && i + 1 < len) {
        codepoint = ((c & 0x1F) << 6) | ((uint8_t)s[i + 1] & 0x3F);
        seqLen = 2;
      } else if ((c & 0xF0) == 0xE0 && i + 2 < len) {
        codepoint = ((c & 0x0F) << 12) | (((uint8_t)s[i + 1] & 0x3F) << 6) | ((uint8_t)s[i + 2] & 0x3F);
        seqLen = 3;
      } else if ((c & 0xF8) == 0xF0 && i + 3 < len) {
        codepoint = ((c & 0x07) << 18) | (((uint8_t)s[i + 1] & 0x3F) << 12) | (((uint8_t)s[i + 2] & 0x3F) << 6) | ((uint8_t)s[i + 3] & 0x3F);
        seqLen = 4;
      }
      if (codepoint >= 0xAC00 && codepoint <= 0xD7A3) {
        hasHangul = true;
        break;
      }
      i += seqLen;
    }
  }
  u8f.setFont(hasHangul ? u8g2_font_unifont_t_korean1 : u8g2_font_unifont_t_japanese3);
  u8f.setForegroundColor(SSD1306_WHITE);
  u8f.setBackgroundColor(SSD1306_BLACK);

  int w = u8f.getUTF8Width(oledCustomText.c_str());
  int ascent = u8f.getFontAscent();
  int descent = u8f.getFontDescent();
  int textH = ascent - descent;
  int baselineY = (OLED_HEIGHT - textH) / 2 + ascent;

  u8f.setCursor(oledMarqueeX, baselineY);
  u8f.print(oledCustomText);
  oled.display();

  oledMarqueeX -= oledMarqueeStepPx();
  if (oledMarqueeX < -w) oledMarqueeX = OLED_WIDTH;
}

void playOledBootAnimation() {
  oledLogoCanvas.fillScreen(0);
  oledLogoCanvas.setTextColor(1);
  oledLogoCanvas.setTextSize(1);
  oledLogoCanvas.setCursor(0, 1);
  oledLogoCanvas.print("VP CONTROLLER");

  const int cw = oledLogoCanvas.width();
  const int ch = oledLogoCanvas.height();
  const unsigned long GROW_MS = 900;
  const unsigned long SETTLE_MS = 400;
  const unsigned long HOLD_MS = 800;
  unsigned long start = millis();

  while (true) {
    unsigned long t = millis() - start;
    float scale;
    if (t < GROW_MS) {
      float p = (float)t / (float)GROW_MS;
      p = 1.0f - pow(1.0f - p, 3.0f);
      scale = 0.15f + p * (1.2f - 0.15f);
    } else if (t < GROW_MS + SETTLE_MS) {
      float p = (float)(t - GROW_MS) / (float)SETTLE_MS;
      p = 1.0f - (1.0f - p) * (1.0f - p);
      scale = 1.2f - p * (1.2f - 1.0f);
    } else if (t < GROW_MS + SETTLE_MS + HOLD_MS) {
      scale = 1.0f;
    } else {
      break;
    }

    oled.clearDisplay();
    int destW = (int)(cw * scale);
    int destH = (int)(ch * scale);
    int offX = (OLED_WIDTH - destW) / 2;
    int offY = (OLED_HEIGHT - destH) / 2;
    int blockW = max(1, (int)ceil(scale));
    int blockH = max(1, (int)ceil(scale));

    for (int y = 0; y < ch; y++) {
      for (int x = 0; x < cw; x++) {
        if (oledLogoCanvas.getPixel(x, y)) {
          int dx = offX + (int)(x * scale);
          int dy = offY + (int)(y * scale);
          oled.fillRect(dx, dy, blockW, blockH, SSD1306_WHITE);
        }
      }
    }
    oled.display();
  }

  delay(150);
}

void updateOledRobotEyes(unsigned long sesiElapsed) {
  int mood;
  if (sesiElapsed < 7000) mood = 0;
  else if (sesiElapsed < 9500) mood = 1;
  else if (sesiElapsed < 12000) mood = 2;
  else mood = 3;

  if (mood != oledEyeMood) {
    oledEyeMood = mood;
    if (mood == 0) { roboEyes.setMood(DEFAULT); }
    else if (mood == 1) { roboEyes.setMood(HAPPY); roboEyes.anim_laugh(); }
    else if (mood == 2) { roboEyes.setMood(ANGRY); }
    else { roboEyes.setMood(TIRED); }
  }

  roboEyes.update();
}

void updateOled() {
  if (!oledReady) return;
  unsigned long now = millis();

  if (oledCustomText.length() > 0) {
    if (now - lastOledMarqueeStep >= OLED_MARQUEE_FPS_MS) {
      lastOledMarqueeStep = now;
      drawOledCustomText();
    }
    return;
  }

  if (now < oledForcedUntil) return;

  if (oledSession != 0) {
    oledSession = 0;
    oledEyeMood = -1;
    lastOledSessionSwitch = now;
  }
  updateOledRobotEyes(now - lastOledSessionSwitch);
}

#define CH224_ADDR_PRIMARY   0x23
#define CH224_ADDR_SECONDARY 0x22

CH224X_I2C* CH224X1 = nullptr;
uint8_t ch224Addr = CH224_ADDR_PRIMARY;

bool i2cDeviceAcks(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool ch224Begin() {
  if (CH224X1 != nullptr) {
    delete CH224X1;
    CH224X1 = nullptr;
  }

  uint8_t addr = i2cDeviceAcks(CH224_ADDR_PRIMARY) ? CH224_ADDR_PRIMARY
               : i2cDeviceAcks(CH224_ADDR_SECONDARY) ? CH224_ADDR_SECONDARY
               : 0;

  if (addr == 0) return false;

  CH224X1 = new CH224X_I2C(Wire, addr, PG_PIN);
  ch224Addr = addr;
  bool beginOk = CH224X1->begin();
  if (!beginOk) {
    Serial.println("CH224A ACK di I2C tapi begin() library gagal - tetap dipakai (kemungkinan PG pin belum stabil).");
  }
  return true;
}

void scanI2CBus() {
  Serial.println("Scanning I2C bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("  Perangkat I2C ditemukan di alamat 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  Tidak ada perangkat I2C terdeteksi sama sekali di bus! Cek wiring/pull-up SDA-SCL.");
  } else {
    Serial.print("  Total perangkat I2C ditemukan: ");
    Serial.println(found);
  }
}

#define PELTIER_PIN 10

void setPeltier(bool on) {
  peltierOn = on;
  digitalWrite(PELTIER_PIN, on ? HIGH : LOW);
  if (prefs.getBool("peltierOn", false) != peltierOn) {
    prefs.putBool("peltierOn", peltierOn);
  }
}

String savedSsid;
String savedPass;
bool wifiControlActive = false;
bool configApActive = false;

#define AP_SSID "ESP32-Config"
#define AP_PASS "12345678"

WebServer server(80);

#define HTTP_AUTH_USER "admin01"
String httpAuthPass;

String loadOrCreateHttpAuthPass() {
  String p = prefs.getString("httpAuthPass", "");
  if (p.length() == 0) {
    const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    char buf[13];
    for (int i = 0; i < 12; i++) {
      uint32_t r = esp_random();
      buf[i] = charset[r % (sizeof(charset) - 1)];
    }
    buf[12] = '\0';
    p = String(buf);
    prefs.putString("httpAuthPass", p);
    Serial.println("Password HTTP unik dibuat untuk device ini.");
  }
  return p;
}

bool checkHttpAuth() {
  if (!server.authenticate(HTTP_AUTH_USER, httpAuthPass.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}
WiFiUDP udp;
#define UDP_BEACON_PORT 47269
unsigned long lastBeacon = 0;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
volatile bool bleWritePending = false;
portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;

#define BLE_RX_BUFFER_SIZE 3072
char bleRxAssembly[BLE_RX_BUFFER_SIZE];
uint16_t bleRxAssemblyLen = 0;
uint8_t bleRxExpectedTotal = 0;
uint8_t bleRxReceivedCount = 0;

char bleCommandBuf[BLE_RX_BUFFER_SIZE] = {0};
volatile uint16_t bleCommandLen = 0;

bool authPassSentThisSession = false;

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    authPassSentThisSession = false;
    Serial.println("BLE: Terhubung ke App!");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.println("BLE: Terputus, me-restart advertising...");
    NimBLEDevice::getAdvertising()->start();
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (connInfo.isEncrypted()) {
      Serial.println("BLE: Koneksi terenkripsi & ter-bonding.");
    } else {
      Serial.println("BLE: Autentikasi gagal / tidak terenkripsi.");
    }
  }
};

class MyCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    std::string value = characteristic->getValue();
    if (value.size() < 2) return;
    uint8_t idx = (uint8_t)value[0];
    uint8_t total = (uint8_t)value[1];
    if (total == 0) total = 1;
    size_t payloadLen = value.size() - 2;

    portENTER_CRITICAL(&bleMux);
    if (idx == 0) {
      bleRxAssemblyLen = 0;
      bleRxExpectedTotal = total;
      bleRxReceivedCount = 0;
    }
    if (idx != bleRxReceivedCount || total != bleRxExpectedTotal) {

      bleRxAssemblyLen = 0;
      bleRxExpectedTotal = 0;
      bleRxReceivedCount = 0;
      portEXIT_CRITICAL(&bleMux);
      return;
    }
    if (bleRxAssemblyLen + payloadLen < BLE_RX_BUFFER_SIZE) {
      memcpy(bleRxAssembly + bleRxAssemblyLen, value.data() + 2, payloadLen);
      bleRxAssemblyLen += payloadLen;
    }
    bleRxReceivedCount++;
    if (bleRxReceivedCount >= bleRxExpectedTotal) {
      size_t finalLen = min((size_t)bleRxAssemblyLen, (size_t)BLE_RX_BUFFER_SIZE - 1);
      memcpy(bleCommandBuf, bleRxAssembly, finalLen);
      bleCommandBuf[finalLen] = '\0';
      bleCommandLen = (uint16_t)finalLen;
      bleWritePending = true;
      bleRxAssemblyLen = 0;
      bleRxExpectedTotal = 0;
      bleRxReceivedCount = 0;
    }
    portEXIT_CRITICAL(&bleMux);
  }
};

void applyVoltage(float volt) {
  if (volt >= 13.5) volt = 15.0;
  else if (volt >= 10.5) volt = 12.0;
  else if (volt >= 7.0) volt = 9.0;
  else volt = 5.0;

  currentSetVoltage = volt;

  if (!ch224aReady || CH224X1 == nullptr) {
    if (prefs.getFloat("voltage", -1.0) != currentSetVoltage) {
      prefs.putFloat("voltage", currentSetVoltage);
    }
    updatePdStatus();
    return;
  }

  if (volt == 9.0) {
    CH224X1->setVoltage(1);
  } else if (volt == 12.0) {
    CH224X1->setVoltage(2);
  } else if (volt == 15.0) {
    CH224X1->setVoltage(3);
  } else {

    CH224X1->setVoltage(0);
  }
  if (prefs.getFloat("voltage", -1.0) != currentSetVoltage) {
    prefs.putFloat("voltage", currentSetVoltage);
  }
}

uint32_t wheelColor(byte pos) {
  pos = 255 - pos;
  if (pos < 85) return strip.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) { pos -= 85; return strip.Color(0, pos * 3, 255 - pos * 3); }
  pos -= 170;
  return strip.Color(pos * 3, 255 - pos * 3, 0);
}

void applyLedMode(String mode) {
  if (mode != "off" && mode != "static" && mode != "running" &&
      mode != "disco" && mode != "bounce" && mode != "knight" &&
      mode != "fire" && mode != "chase" && mode != "colorwave" &&
      mode != "custom" && !(mode.startsWith("wled") && mode.substring(4).toInt() >= 1 &&
                            mode.substring(4).toInt() <= 100)) return;
  ledMode = mode;
  if (mode != "off") lastLedEffect = mode;

  if (mode == "off") {
    strip.clear();
    strip.show();
  } else if (mode == "static") {

    for (int i = 0; i < NUM_LEDS; i++) {
      int hue = (i * 256 / NUM_LEDS) & 255;
      strip.setPixelColor(i, wheelColor(hue));
    }
    strip.show();
  } else if (mode == "custom") {
    bouncePos = 0;
    bounceDir = 1;
    colorwavePhase = 0;
    rainbowStep = 0;
    if (customPattern == "solid") {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(customR, customG, customB));
      }
      strip.show();
    }

  } else if (mode == "bounce" || mode == "knight") {
    bouncePos = 0;
    bounceDir = 1;
  } else if (mode == "chase") {
    bouncePos = 0;
  } else if (mode == "colorwave") {
    colorwavePhase = 0;
  } else if (mode.startsWith("wled")) {
    rainbowStep = 0;
    bouncePos = 0;
    bounceDir = 1;
    colorwavePhase = 0;
    lastLedStep = 0;
    memset(confettiFade, 0, sizeof(confettiFade));
  }

  if (prefs.getString("ledMode", "") != ledMode) {
    prefs.putString("ledMode", ledMode);
  }
}

void handleWledEffect(uint16_t effectId) {
  int8_t engine, palette;
  uint16_t baseSpeedMs;
  if (!getWledModeInfo(effectId, engine, palette, baseSpeedMs)) return;
  if (millis() - lastLedStep < ledStepDelay(baseSpeedMs)) return;
  lastLedStep = millis();

  const uint32_t frame = millis() / ledStepDelay(baseSpeedMs);
  switch (engine) {
    case 0: { // Static: pola warna tetap sepanjang strip.
      for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t pos = (i * 256 / NUM_LEDS) & 255;
        strip.setPixelColor(i, wledColorAt(effectId, pos, 255));
      }
      break;
    }
    case 1: { // Rainbow Run: palet bergerak dari LED ke LED.
      for (int i = 0; i < NUM_LEDS; i++) {
        int pos = ((i * 256 / NUM_LEDS) + frame * 3) & 255;
        strip.setPixelColor(i, wledColorAt(effectId, pos, 255));
      }
      break;
    }
    case 2: { // Rainbow Cycle: seluruh strip bergeser warna bersama-sama.
      uint32_t c = wledColorAt(effectId, (frame * 2) & 255, 255);
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, c);
      break;
    }
    case 3: { // Disco: setiap frame membuat warna acak dari palet.
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, wledColorAt(effectId, random(0, 256), 255));
      }
      break;
    }
    case 4: { // Confetti: titik-titik cahaya muncul dan memudar.
      for (int i = 0; i < NUM_LEDS; i++) {
        confettiFade[i] = (confettiFade[i] > 12) ? confettiFade[i] - 12 : 0;
      }
      if (random(0, 10) < 6) {
        confettiFade[random(0, NUM_LEDS)] = 255;
      }
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, wledColorAt(effectId, (i * 40) & 255, confettiFade[i]));
      }
      break;
    }
  }
  strip.show();
}

void handleCustomPattern() {
  if (millis() - lastLedStep < ledStepDelay(30)) return;
  lastLedStep = millis();

  if (customPattern == "chaseRight" || customPattern == "chaseLeft") {

    const int blockLen = 3;
    const int dir = customPattern == "chaseRight" ? 1 : -1;
    strip.clear();
    for (int t = 0; t < blockLen; t++) {
      int pos = ((bouncePos + t * dir) % NUM_LEDS + NUM_LEDS) % NUM_LEDS;
      strip.setPixelColor(pos, ledColorScaled(255));
    }
    strip.show();
    bouncePos = (bouncePos + dir + NUM_LEDS) % NUM_LEDS;

  } else if (customPattern == "pingpong") {
    strip.clear();
    const int tailLen = 5;
    for (int t = 0; t < tailLen; t++) {
      int pos = bouncePos - (bounceDir * t);
      if (pos >= 0 && pos < NUM_LEDS) {
        int fade = 255 - (t * (255 / tailLen));
        strip.setPixelColor(pos, ledColorScaled((uint8_t)fade));
      }
    }
    strip.show();
    bouncePos += bounceDir;
    if (bouncePos >= NUM_LEDS - 1 || bouncePos <= 0) bounceDir = -bounceDir;

  } else if (customPattern == "pulse") {

    colorwavePhase += 0.15;
    float t = fmod(colorwavePhase, 6.28318f);
    float wave = t < 1.2f ? sinf(t * 2.6f) : 0.0f;
    uint8_t intensity = (uint8_t)(30 + max(0.0f, wave) * 225);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, ledColorScaled(intensity));
    strip.show();

  } else if (customPattern == "breathe") {

    colorwavePhase += 0.035;
    float wave = (sinf(colorwavePhase) + 1.0f) / 2.0f;
    uint8_t intensity = (uint8_t)(15 + wave * 240);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, ledColorScaled(intensity));
    strip.show();

  } else if (customPattern == "rainbow") {
    rainbowStep = (rainbowStep + 2) % 256;
    uint32_t c = wheelColor((uint8_t)rainbowStep);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, c);
    strip.show();

  } else if (customPattern == "rainbowRight" || customPattern == "rainbowLeft") {
    const int dir = customPattern == "rainbowRight" ? 1 : -1;
    for (int i = 0; i < NUM_LEDS; i++) {
      int hue = ((i * 256 / NUM_LEDS) + rainbowStep) & 255;
      strip.setPixelColor(i, wheelColor((uint8_t)hue));
    }
    strip.show();
    rainbowStep = (rainbowStep + dir * 3 + 256) % 256;

  } else if (customPattern == "spinCW" || customPattern == "spinCCW") {
    const int dir = customPattern == "spinCW" ? 1 : -1;
    strip.clear();
    const int tailLen = 6;
    for (int t = 0; t < tailLen; t++) {
      int pos = ((bouncePos - t * dir) % NUM_LEDS + NUM_LEDS * 2) % NUM_LEDS;
      int fade = 255 - (t * (255 / tailLen));
      strip.setPixelColor(pos, ledColorScaled((uint8_t)fade));
    }
    strip.show();
    bouncePos = (bouncePos + dir + NUM_LEDS) % NUM_LEDS;

  } else if (customPattern == "theaterChase") {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, ((i + bouncePos) % 3 == 0) ? ledColorScaled(255) : ledColorScaled(0));
    }
    strip.show();
    bouncePos = (bouncePos + 1) % 3;

  } else if (customPattern == "cometRight" || customPattern == "cometLeft") {

    const int dir = customPattern == "cometRight" ? 1 : -1;
    strip.clear();
    const int tailLen = 8;
    for (int t = 0; t < tailLen; t++) {
      int pos = ((bouncePos - t * dir) % NUM_LEDS + NUM_LEDS * 2) % NUM_LEDS;
      int fade = 255 - (t * (255 / tailLen));
      strip.setPixelColor(pos, ledColorScaled((uint8_t)(fade > 0 ? fade : 0)));
    }
    strip.show();
    bouncePos = (bouncePos + dir + NUM_LEDS) % NUM_LEDS;

  } else if (customPattern == "sparkle") {
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t intensity = (random(0, 100) < 20) ? 255 : random(10, 60);
      strip.setPixelColor(i, ledColorScaled(intensity));
    }
    strip.show();

  } else if (customPattern == "twinkle") {

    rainbowStep = (rainbowStep + 1) % 60000;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t phase = (uint8_t)((rainbowStep * 3 + i * 29) % 255);
      float wave = (sinf(phase * 0.0246f) + 1.0f) / 2.0f;
      uint8_t intensity = (uint8_t)(10 + wave * wave * 245);
      strip.setPixelColor(i, ledColorScaled(intensity));
    }
    strip.show();

  } else if (customPattern == "wipeRight" || customPattern == "wipeLeft") {
    const int cycle = NUM_LEDS * 2;
    int count = bouncePos < NUM_LEDS ? bouncePos + 1 : (cycle - bouncePos);
    strip.clear();
    for (int n = 0; n < count; n++) {
      int idx = customPattern == "wipeRight" ? n : (NUM_LEDS - 1 - n);
      strip.setPixelColor(idx, ledColorScaled(255));
    }
    strip.show();
    bouncePos = (bouncePos + 1) % cycle;

  } else if (customPattern == "centerOut" || customPattern == "edgeIn") {
    const int half = (NUM_LEDS + 1) / 2;
    const int cycle = half * 2;
    int radius = bouncePos < half ? bouncePos + 1 : (cycle - bouncePos);
    const int center = NUM_LEDS / 2;
    strip.clear();
    if (customPattern == "centerOut") {
      for (int n = 0; n < radius; n++) {
        int a = center - n, b = center + n;
        if (a >= 0 && a < NUM_LEDS) strip.setPixelColor(a, ledColorScaled(255));
        if (b >= 0 && b < NUM_LEDS) strip.setPixelColor(b, ledColorScaled(255));
      }
    } else {
      for (int n = 0; n < radius; n++) {
        if (n < NUM_LEDS) strip.setPixelColor(n, ledColorScaled(255));
        int b = NUM_LEDS - 1 - n;
        if (b >= 0) strip.setPixelColor(b, ledColorScaled(255));
      }
    }
    strip.show();
    bouncePos = (bouncePos + 1) % cycle;
  }
}

void handleLedAnimation() {
  if (ledMode == "running") {
    if (millis() - lastLedStep < ledStepDelay(20)) return;
    lastLedStep = millis();

    for (int i = 0; i < NUM_LEDS; i++) {
      int hue = ((i * 256 / NUM_LEDS) + rainbowStep) & 255;
      strip.setPixelColor(i, wheelColor(hue));
    }
    strip.show();
    rainbowStep += 3;
    if (rainbowStep >= 256) rainbowStep = 0;
  } else if (ledMode == "disco") {
    if (millis() - lastLedStep < ledStepDelay(120)) return;
    lastLedStep = millis();

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(random(0, 256), random(0, 256), random(0, 256)));
    }
    strip.show();
  } else if (ledMode == "bounce") {
    if (millis() - lastLedStep < ledStepDelay(30)) return;
    lastLedStep = millis();

    strip.clear();
    const int tailLen = 4;
    for (int t = 0; t < tailLen; t++) {
      int pos = bouncePos - (bounceDir * t);
      if (pos >= 0 && pos < NUM_LEDS) {
        int fade = 255 - (t * (255 / tailLen));
        uint32_t c = wheelColor((bouncePos * 8) & 255);
        uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * fade / 255);
        uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * fade / 255);
        uint8_t b = (uint8_t)((c & 0xFF) * fade / 255);
        strip.setPixelColor(pos, strip.Color(r, g, b));
      }
    }
    strip.show();
    bouncePos += bounceDir;
    if (bouncePos >= NUM_LEDS - 1 || bouncePos <= 0) bounceDir = -bounceDir;
  } else if (ledMode == "knight") {

    if (millis() - lastLedStep < ledStepDelay(30)) return;
    lastLedStep = millis();
    strip.clear();
    const int tailLen = 5;
    for (int t = 0; t < tailLen; t++) {
      int pos = bouncePos - (bounceDir * t);
      if (pos >= 0 && pos < NUM_LEDS) {
        int fade = 255 - (t * (255 / tailLen));
        strip.setPixelColor(pos, ledColorScaled((uint8_t)fade));
      }
    }
    strip.show();
    bouncePos += bounceDir;
    if (bouncePos >= NUM_LEDS - 1 || bouncePos <= 0) bounceDir = -bounceDir;
  } else if (ledMode == "fire") {

    if (millis() - lastLedStep < ledStepDelay(60)) return;
    lastLedStep = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int flicker = random(140, 256);
      uint8_t intensity = (uint8_t)(flicker * random(55, 101) / 100);
      strip.setPixelColor(i, ledColorScaled(intensity));
    }
    strip.show();
  } else if (ledMode == "chase") {

    if (millis() - lastLedStep < ledStepDelay(40)) return;
    lastLedStep = millis();
    strip.clear();
    strip.setPixelColor(bouncePos, ledColorScaled(255));
    strip.show();
    bouncePos = (bouncePos + 1) % NUM_LEDS;
  } else if (ledMode == "colorwave") {

    if (millis() - lastLedStep < ledStepDelay(30)) return;
    lastLedStep = millis();
    colorwavePhase += 0.06;

    for (int i = 0; i < NUM_LEDS; i++) {
      float wave = (sinf(i * 0.35f + colorwavePhase) + 1.0f) / 2.0f;
      uint8_t intensity = (uint8_t)(35 + wave * 220);
      strip.setPixelColor(i, ledColorScaled(intensity));
    }
    strip.show();
  } else if (ledMode == "custom" && customPattern != "solid") {
    handleCustomPattern();
  } else if (ledMode.startsWith("wled")) {
    const int effectId = ledMode.substring(4).toInt();
    if (effectId >= 1 && effectId <= 100) handleWledEffect(effectId);
  }
}

String buildStatusJson(bool includeSecret) {
  unsigned long runtime = millis() - startMillis;
  long s = runtime / 1000, m = s / 60, h = m / 60;
  String uptime = String(h) + ":" + String(m % 60) + ":" + String(s % 60);

  JsonDocument doc;
  doc["deviceId"] = deviceId;
  doc["setVoltage"] = currentSetVoltage;
  doc["ledMode"] = ledMode;
  doc["uptime"] = uptime;
  doc["chargerWatt"] = chargerwatt;
  doc["powerGood"] = pgood;
  doc["ch224aReady"] = ch224aReady;

  doc["pdStatus"] = pdStatus;
  doc["fanSpeed"] = fanSpeedPercent;
  doc["ledSpeed"] = ledSpeedPercent;
  doc["ledBrightness"] = ledBrightnessPercent;
  char customHex[7];
  snprintf(customHex, sizeof(customHex), "%02X%02X%02X", customR, customG, customB);
  doc["customColor"] = customHex;
  doc["customPattern"] = customPattern;
  if (ledMode.startsWith("wled")) {
    int effectId = ledMode.substring(4).toInt();
    if (effectId >= 1 && effectId <= 100) {
      int idx = effectId - 1;
      char wledHex[7];
      snprintf(wledHex, sizeof(wledHex), "%02X%02X%02X", wledColorR[idx], wledColorG[idx], wledColorB[idx]);
      doc["wledColor"] = wledHex;
    }
  }
  doc["oledText"] = oledCustomText;
  doc["oledTextSpeed"] = oledTextSpeedPercent;
  doc["fanRpm"] = fanRpm;
  doc["peltier"] = peltierOn;
  doc["netMode"] = netMode;
  doc["configApActive"] = configApActive;
  doc["wifiConnected"] = wifiControlActive;
  if (wifiControlActive) {
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
  }

  if (includeSecret) doc["httpAuthPass"] = httpAuthPass;
  String jsonStr;
  serializeJson(doc, jsonStr);
  return jsonStr;
}

#define BLE_CHUNK_SIZE 480

void publishStatusBLE() {
  if (!deviceConnected || pCharacteristic == nullptr) return;

  String jsonStr = buildStatusJson(!authPassSentThisSession);
  if (!authPassSentThisSession) authPassSentThisSession = true;

  size_t total = jsonStr.length();
  size_t numChunks = (total + BLE_CHUNK_SIZE - 1) / BLE_CHUNK_SIZE;
  if (numChunks == 0) numChunks = 1;
  if (numChunks > 255) numChunks = 255;

  for (size_t i = 0; i < numChunks; i++) {
    size_t start = i * BLE_CHUNK_SIZE;
    size_t len = min((size_t)BLE_CHUNK_SIZE, total - start);
    uint8_t packet[BLE_CHUNK_SIZE + 2];
    packet[0] = (uint8_t)i;
    packet[1] = (uint8_t)numChunks;
    memcpy(packet + 2, jsonStr.c_str() + start, len);
    pCharacteristic->setValue(packet, len + 2);
    pCharacteristic->notify();

    if (numChunks > 1) delay(55);
  }
}

#define MAX_SCHEDULES 40
struct ScheduleRule {
  String id;
  float voltage;
  uint8_t hour;
  uint8_t minute;
  uint8_t daysMask;
  bool enabled;
  int16_t lastFiredYday;
};
ScheduleRule schedules[MAX_SCHEDULES];
int scheduleCount = 0;

int dartWeekdayFromTm(int tm_wday) {
  return (tm_wday == 0) ? 7 : tm_wday;
}

void setupNtpTime() {

  configTzTime("WIB-7", "pool.ntp.org", "time.google.com", "id.pool.ntp.org");
  Serial.println("Sinkronisasi waktu NTP dimulai (WIB, UTC+7)...");
}

void saveSchedulesToPrefs() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < scheduleCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = schedules[i].id;
    o["voltage"] = schedules[i].voltage;
    o["hour"] = schedules[i].hour;
    o["minute"] = schedules[i].minute;
    o["daysMask"] = schedules[i].daysMask;
    o["enabled"] = schedules[i].enabled;
    o["lastFiredYday"] = schedules[i].lastFiredYday;
  }
  String out;
  serializeJson(doc, out);
  prefs.putString("schedules", out);
}

void loadSchedulesFromPrefs() {
  String raw = prefs.getString("schedules", "[]");
  JsonDocument doc;
  if (deserializeJson(doc, raw)) { scheduleCount = 0; return; }
  scheduleCount = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (scheduleCount >= MAX_SCHEDULES) break;
    ScheduleRule& r = schedules[scheduleCount];
    r.id = o["id"] | "";
    r.voltage = o["voltage"] | 5.0;
    r.hour = o["hour"] | 0;
    r.minute = o["minute"] | 0;
    r.daysMask = o["daysMask"] | 0;
    r.enabled = o["enabled"] | true;
    r.lastFiredYday = o["lastFiredYday"] | -1;
    scheduleCount++;
  }
  Serial.print("Jadwal otomatis dimuat dari NVS: ");
  Serial.print(scheduleCount);
  Serial.println(" aturan.");
}

void applySchedulesFromJson(JsonArray arr) {
  static ScheduleRule oldSchedules[MAX_SCHEDULES];
  int oldCount = scheduleCount;
  for (int i = 0; i < oldCount; i++) oldSchedules[i] = schedules[i];

  scheduleCount = 0;
  for (JsonObject o : arr) {
    if (scheduleCount >= MAX_SCHEDULES) break;
    ScheduleRule r;
    r.id = o["id"] | String(scheduleCount);
    r.voltage = o["voltage"] | 5.0;
    r.hour = o["hour"] | 0;
    r.minute = o["minute"] | 0;
    r.enabled = o["enabled"] | true;
    r.daysMask = 0;
    if (o["days"].is<JsonArray>()) {
      for (JsonVariant d : o["days"].as<JsonArray>()) {
        int dv = d.as<int>();
        if (dv >= 1 && dv <= 7) r.daysMask |= (1 << (dv - 1));
      }
    }

    r.lastFiredYday = -1;
    for (int j = 0; j < oldCount; j++) {
      if (oldSchedules[j].id == r.id) { r.lastFiredYday = oldSchedules[j].lastFiredYday; break; }
    }
    schedules[scheduleCount] = r;
    scheduleCount++;
  }
  saveSchedulesToPrefs();
  Serial.print("Jadwal otomatis diperbarui dari app: ");
  Serial.print(scheduleCount);
  Serial.println(" aturan tersimpan ke NVS.");
}

void checkSchedulesAutonomous() {
  if (scheduleCount == 0) return;
  time_t now = time(nullptr);
  if (now < 1700000000) return;
  struct tm t;
  localtime_r(&now, &t);

  static int lastCheckedMinuteKey = -1;
  int minuteKey = t.tm_hour * 60 + t.tm_min;
  if (minuteKey == lastCheckedMinuteKey) return;
  lastCheckedMinuteKey = minuteKey;

  int weekdayDart = dartWeekdayFromTm(t.tm_wday);
  bool changed = false;
  for (int i = 0; i < scheduleCount; i++) {
    ScheduleRule& r = schedules[i];
    if (!r.enabled) continue;
    if (!(r.daysMask & (1 << (weekdayDart - 1)))) continue;
    if (r.hour != t.tm_hour || r.minute != t.tm_min) continue;
    if (r.lastFiredYday == t.tm_yday) continue;
    r.lastFiredYday = t.tm_yday;
    changed = true;
    if (ch224aReady) {
      applyVoltage(r.voltage);
      triggerCmdBlink();
      Serial.print("Jadwal otomatis terpicu MANDIRI (tanpa app terhubung) -> ");
      Serial.print(r.voltage);
      Serial.println("V");
    }
  }
  if (changed) saveSchedulesToPrefs();
}

void processCommandJson(const String& cmd) {
  JsonDocument doc;
  if (deserializeJson(doc, cmd)) return;
  if (doc["voltage"].is<float>() || doc["voltage"].is<int>()) {
    float v = doc["voltage"].as<float>();
    Serial.print("Perintah voltase diterima dari app: ");
    Serial.println(v);
    applyVoltage(v);
    drawOledRequest();
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["ledMode"].is<const char*>()) {
    applyLedMode(doc["ledMode"].as<String>());
    drawOledMode();
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["ledSpeed"].is<int>()) {
    setLedSpeed(doc["ledSpeed"]);
    drawOledStatusScreen("LED SPEED", String(doc["ledSpeed"].as<int>()) + "%", 2);
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["ledBrightness"].is<int>()) {
    setLedBrightness(doc["ledBrightness"]);
    drawOledStatusScreen("BRIGHTNESS", String(doc["ledBrightness"].as<int>()) + "%", 2);
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["customColor"].is<const char*>()) {
    String hex = doc["customColor"].as<String>();
    hex.trim();
    if (hex.startsWith("#")) hex = hex.substring(1);
    if (hex.length() == 6) {
      long val = strtol(hex.c_str(), nullptr, 16);
      setCustomColor((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
      drawOledMode();
      triggerOledSnapshot();
      triggerCmdBlink();
    }
  }
  if (doc["customPattern"].is<const char*>()) {
    setCustomPattern(doc["customPattern"].as<String>());
    drawOledMode();
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["wledColor"].is<const char*>()) {

    String hex = doc["wledColor"].as<String>();
    hex.trim();
    if (hex.startsWith("#")) hex = hex.substring(1);
    if (hex.length() == 6 && ledMode.startsWith("wled")) {
      int effectId = ledMode.substring(4).toInt();
      if (effectId >= 1 && effectId <= 100) {
        long val = strtol(hex.c_str(), nullptr, 16);
        setWledColor(effectId, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
        drawOledMode();
        triggerOledSnapshot();
        triggerCmdBlink();
      }
    }
  }
  if (doc["oledText"].is<const char*>()) {

    setOledCustomText(doc["oledText"].as<String>());
    triggerCmdBlink();
  }
  if (doc["oledTextSpeed"].is<int>()) {
    setOledTextSpeed(doc["oledTextSpeed"]);
    triggerCmdBlink();
  }
  if (doc["fanSpeed"].is<int>()) {
    setFanSpeed(doc["fanSpeed"]);
    drawOledStatusScreen("FAN SPEED", String(doc["fanSpeed"].as<int>()) + "%", 2);
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["peltier"].is<bool>()) {
    setPeltier(doc["peltier"]);
    drawOledPeltier();
    triggerOledSnapshot();
    triggerCmdBlink();
  }
  if (doc["schedules"].is<JsonArray>()) {
    applySchedulesFromJson(doc["schedules"].as<JsonArray>());
  }
  if (!doc["setTime"].isNull()) {

    long epoch = doc["setTime"].as<long>();
    if (epoch > 1000000000L) {
      struct timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      setenv("TZ", "WIB-7", 1);
      tzset();
      Serial.println("Waktu disinkronkan dari app (untuk jadwal otomatis).");
    }
  }

  if (doc["requestStatus"] == true) {
    publishStatusBLE();
  }
}

void handleScanWifi() {
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
    server.send(200, "text/plain", "scanning");
    return;
  }
  if (n == -1) {
    server.send(200, "text/plain", "scanning");
    return;
  }
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
  String out;
  serializeJson(doc, out);
  WiFi.scanDelete();
  server.send(200, "application/json", out);
}

void handleSetWifi() {
  if (!configApActive && !checkHttpAuth()) return;
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "text/plain", "missing ssid/password");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("password");
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("netMode", "wifi");
  JsonDocument doc;
  doc["result"] = "OK";
  doc["deviceId"] = deviceId;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
  Serial.println("Kredensial WiFi disimpan (" + ssid + "), restart untuk masuk mode WiFi...");
  delay(400);
  ESP.restart();
}

static const char SETUP_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Setup WiFi - Vladimir Putin</title>
<style>
  :root{
    --bg:#05070c; --card:#0d1220; --border:#1c2436; --accent:#22d3ee; --accent2:#a78bfa;
    --text:#eef1fb; --faint:#7b869c; --ok:#34e0a1; --danger:#ff5d6c;
  }
  *{box-sizing:border-box;}
  html,body{margin:0;padding:0;}
  body{
    background:
      radial-gradient(circle at 1px 1px, rgba(255,255,255,.05) 1px, transparent 0) 0 0/22px 22px,
      linear-gradient(160deg,#05070c,#080b16 55%,#05070c);
    color:var(--text); font-family:-apple-system,"Segoe UI",Roboto,Arial,sans-serif;
    padding:22px 16px 40px; min-height:100vh;
  }
  .head{display:flex; align-items:center; gap:12px; margin:4px 0 22px;}
  .head .icon{
    width:42px; height:42px; border-radius:12px; flex-shrink:0;
    background:linear-gradient(135deg, rgba(34,211,238,.15), rgba(167,139,250,.15));
    border:1px solid rgba(34,211,238,.4);
    display:flex; align-items:center; justify-content:center;
    box-shadow:0 0 18px rgba(34,211,238,.25);
  }
  .head .icon svg{width:22px;height:22px;}
  .head h1{
    font-size:16px; margin:0; letter-spacing:1.5px; font-weight:800; text-transform:uppercase;
    background:linear-gradient(90deg,var(--accent),var(--accent2));
    -webkit-background-clip:text; background-clip:text; color:transparent;
  }
  .head .tag{font-size:10.5px; letter-spacing:1px; color:var(--faint); text-transform:uppercase; margin-top:2px;}

  .card{
    position:relative; background:var(--card); border:1px solid var(--border); border-radius:14px;
    padding:18px; margin-bottom:14px; animation:rise .45s ease both;
  }
  .card::before, .card::after{
    content:""; position:absolute; width:14px; height:14px; border:1.5px solid rgba(34,211,238,.55);
    opacity:.8;
  }
  .card::before{ top:-1px; left:-1px; border-right:none; border-bottom:none; border-top-left-radius:8px; }
  .card::after{ bottom:-1px; right:-1px; border-left:none; border-top:none; border-bottom-right-radius:8px; }
  @keyframes rise{ from{opacity:0; transform:translateY(8px);} to{opacity:1; transform:translateY(0);} }

  .label{font-size:10.5px; letter-spacing:1.2px; text-transform:uppercase; color:var(--faint); margin-bottom:10px; display:flex; align-items:center; gap:6px;}
  .label svg{width:13px;height:13px; opacity:.8;}

  .row{display:flex; align-items:center; justify-content:space-between; padding:9px 0; border-bottom:1px dashed rgba(255,255,255,.06);}
  .row:last-child{border-bottom:none;}
  .row .k{color:var(--faint); font-size:12.5px;}
  .row .v{font-family:"SF Mono",Consolas,monospace; font-weight:700; letter-spacing:1.5px; font-size:13.5px; color:var(--accent);}

  .hint{color:var(--faint); font-size:11.5px; line-height:1.6; margin-top:10px; padding-top:10px; border-top:1px dashed rgba(255,255,255,.06);}

  .netrow{display:flex; align-items:center; justify-content:space-between; padding:8px 0;}
  .netrow .k{color:var(--faint); font-size:12px;}
  .netrow .v{font-size:12.5px; font-weight:600;}

  button{
    width:100%; padding:14px; border-radius:12px; border:none; font-size:13px; font-weight:800;
    letter-spacing:.8px; text-transform:uppercase; cursor:pointer; margin-top:8px;
    display:flex; align-items:center; justify-content:center; gap:8px;
    transition:transform .12s ease, filter .12s ease;
  }
  button:active{ transform:scale(.98); filter:brightness(.92); }
  button svg{width:16px;height:16px;}
  .btn-primary{background:linear-gradient(90deg,var(--accent),var(--accent2)); color:#031018; box-shadow:0 6px 20px rgba(34,211,238,.2);}
  .btn-outline{background:transparent; border:1px solid var(--border); color:var(--text);}
  .btn-outline:active{border-color:var(--accent);}

  input{
    width:100%; padding:13px 14px; border-radius:12px; border:1px solid var(--border);
    background:#080c16; color:var(--text); font-size:13.5px; margin-top:10px;
  }
  input:focus{outline:none; border-color:var(--accent); box-shadow:0 0 0 3px rgba(34,211,238,.12);}
  input::placeholder{color:#4a5468;}

  #wifiList{margin-top:10px;}
  .wifi-item{
    display:flex; justify-content:space-between; align-items:center; padding:12px 14px;
    background:#080c16; border:1px solid var(--border); border-radius:10px; margin-bottom:7px; cursor:pointer;
  }
  .wifi-item:active{border-color:var(--accent);}
  .wifi-item .left{display:flex; align-items:center; gap:10px;}
  .wifi-item .ssid{font-size:13px; font-weight:600;}
  .bars{display:flex; align-items:flex-end; gap:2px; height:12px;}
  .bars i{width:3px; background:#2a3448; border-radius:1px;}
  .bars i.on{background:var(--accent);}
  .bars i:nth-child(1){height:4px;} .bars i:nth-child(2){height:7px;}
  .bars i:nth-child(3){height:10px;} .bars i:nth-child(4){height:13px;}

  #status{margin-top:12px; font-size:12.5px; line-height:1.6; display:flex; gap:8px; align-items:flex-start;}
  #status svg{width:15px;height:15px; flex-shrink:0; margin-top:1px;}
  .ok{color:var(--ok);} .err{color:var(--danger);} .faint2{color:var(--faint);}
  .spin{
    display:inline-block; width:14px; height:14px; border:2px solid #2a3448; border-top-color:var(--accent);
    border-radius:50%; animation:sp .7s linear infinite; flex-shrink:0; margin-top:1px;
  }
  @keyframes sp{to{transform:rotate(360deg);}}
</style>
</head>
<body>

  <div class="head">
    <div class="icon">
      <svg viewBox="0 0 24 24" fill="none" stroke="#22d3ee" stroke-width="1.6"><rect x="7" y="7" width="10" height="10" rx="2"/><path d="M9 3v4M15 3v4M9 17v4M15 17v4M3 9h4M3 15h4M17 9h4M17 15h4"/></svg>
    </div>
    <div>
      <h1>Setup WiFi</h1>
      <div class="tag">Cooler Control Node</div>
    </div>
  </div>

  <div class="card" style="animation-delay:.02s">
    <div class="label">
      <svg viewBox="0 0 24 24" fill="none" stroke="#7b869c" stroke-width="1.8"><rect x="4" y="2" width="16" height="20" rx="2"/><path d="M10 18h4"/></svg>
      Identitas Device
    </div>
    <div class="row"><span class="k">Device ID</span><span class="v" id="deviceId">-</span></div>
    <div class="row"><span class="k">Password Kontrol</span><span class="v" id="authPass">-</span></div>
    <div class="hint">Catat 2 nilai di atas. Kalau nanti nambah cooler ini lewat menu "Manual (WiFi)" di aplikasi, kamu perlu masukkan keduanya supaya bisa mengontrol (bukan cuma lihat status).</div>
    <div class="netrow" id="rowNet" style="display:none;">
      <span class="k">Status Jaringan</span><span class="v" id="netInfo">-</span>
    </div>
  </div>

  <div class="card" style="animation-delay:.08s">
    <div class="label">
      <svg viewBox="0 0 24 24" fill="none" stroke="#7b869c" stroke-width="1.8"><path d="M5 12.5a11 11 0 0 1 14 0M8 16a6.5 6.5 0 0 1 8 0M12 19.5v.01"/></svg>
      Sambungkan ke WiFi
    </div>
    <button class="btn-outline" onclick="scanWifi()">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3"/></svg>
      Cari WiFi Sekitar
    </button>
    <div id="wifiList"></div>

    <input type="text" id="ssid" placeholder="Nama WiFi (SSID)">
    <input type="password" id="password" placeholder="Password WiFi">
    <button class="btn-primary" onclick="connectWifi()">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 12l2 2 4-4M12 3l9 6-9 6-9-6 9-6z"/></svg>
      Hubungkan
    </button>
    <div id="status"></div>
  </div>

<script>
const ICON_OK = '<svg viewBox="0 0 24 24" fill="none" stroke="#34e0a1" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M8 12l3 3 5-6"/></svg>';
const ICON_ERR = '<svg viewBox="0 0 24 24" fill="none" stroke="#ff5d6c" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 8v5M12 16v.01"/></svg>';

function setStatus(html, cls, icon){
  const el = document.getElementById('status');
  el.innerHTML = (icon || '') + '<span>' + html + '</span>';
  el.className = cls || '';
}

function barsHtml(rssi){
  const n = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  let h = '<div class="bars">';
  for (let i = 1; i <= 4; i++) h += '<i class="' + (i <= n ? 'on' : '') + '"></i>';
  return h + '</div>';
}

async function loadDeviceInfo(){
  try{
    const r = await fetch('/status');
    const j = await r.json();
    document.getElementById('deviceId').textContent = j.deviceId || '-';
    document.getElementById('authPass').textContent = j.httpAuthPass || '-';
    if (j.wifiConnected){
      document.getElementById('rowNet').style.display = 'flex';
      document.getElementById('netInfo').textContent = (j.ssid || '-') + ' - ' + (j.ip || '-');
    }
  }catch(e){}
}

async function scanWifi(){
  const list = document.getElementById('wifiList');
  list.innerHTML = '<p style="color:#7b869c;font-size:12px;display:flex;align-items:center;gap:8px;"><span class="spin"></span>Memindai jaringan...</p>';
  try{
    let networks = null;
    for (let i = 0; i < 12; i++){
      const r = await fetch('/scanwifi');
      const ct = r.headers.get('content-type') || '';
      if (ct.indexOf('application/json') !== -1){
        networks = await r.json();
        break;
      }
      await new Promise(res => setTimeout(res, 900));
    }
    if (!networks){
      list.innerHTML = '<p style="color:#ff5d6c;font-size:12px;">Gagal memindai, coba lagi.</p>';
      return;
    }
    if (networks.length === 0){
      list.innerHTML = '<p style="color:#7b869c;font-size:12px;">Tidak ada WiFi ditemukan.</p>';
      return;
    }
    networks.sort((a,b) => b.rssi - a.rssi);
    list.innerHTML = '';
    networks.forEach(n => {
      const div = document.createElement('div');
      div.className = 'wifi-item';
      div.innerHTML = '<span class="left"><span class="ssid">' + n.ssid + '</span></span>' + barsHtml(n.rssi);
      div.onclick = () => { document.getElementById('ssid').value = n.ssid; document.getElementById('password').focus(); };
      list.appendChild(div);
    });
  }catch(e){
    list.innerHTML = '<p style="color:#ff5d6c;font-size:12px;">Gagal memindai, coba lagi.</p>';
  }
}

async function connectWifi(){
  const ssid = document.getElementById('ssid').value.trim();
  const password = document.getElementById('password').value;
  if (!ssid){ setStatus('Isi nama WiFi dulu.', 'err', ICON_ERR); return; }
  setStatus('Menyimpan &amp; menghubungkan...', 'faint2', '<span class="spin"></span>');
  try{
    const body = 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password);
    await fetch('/setwifi', {
      method: 'POST',
      headers: {'Content-Type':'application/x-www-form-urlencoded'},
      body: body
    });
    setStatus('Tersimpan! ESP32 sedang restart &amp; mencoba konek ke WiFi rumah (kurang lebih 15 detik).<br>' +
               'Sambungkan HP kembali ke WiFi rumah, lalu buka aplikasi, Tambah Cooler, tab Manual (WiFi), masukkan Device ID di atas.', 'ok', ICON_OK);
  }catch(e){
    setStatus('Perintah terkirim. ESP32 kemungkinan sudah restart untuk konek WiFi (koneksi ke halaman ini terputus, itu normal).<br>' +
               'Sambungkan HP kembali ke WiFi rumah lalu buka aplikasi.', 'ok', ICON_OK);
  }
}

loadDeviceInfo();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", SETUP_PAGE_HTML);
}

void handleStatusHttp() {
  lastAppContact = millis();
  server.send(200, "application/json", buildStatusJson(configApActive));
}

void handleSetCmd() {
  if (!checkHttpAuth()) return;
  lastAppContact = millis();
  JsonDocument doc;
  if (server.hasArg("voltage")) doc["voltage"] = server.arg("voltage").toFloat();
  if (server.hasArg("ledMode")) doc["ledMode"] = server.arg("ledMode");
  if (server.hasArg("fanSpeed")) doc["fanSpeed"] = server.arg("fanSpeed").toInt();
  if (server.hasArg("ledSpeed")) doc["ledSpeed"] = server.arg("ledSpeed").toInt();
  if (server.hasArg("ledBrightness")) doc["ledBrightness"] = server.arg("ledBrightness").toInt();
  if (server.hasArg("customColor")) doc["customColor"] = server.arg("customColor");
  if (server.hasArg("customPattern")) doc["customPattern"] = server.arg("customPattern");
  if (server.hasArg("wledColor")) doc["wledColor"] = server.arg("wledColor");
  if (server.hasArg("oledText")) doc["oledText"] = server.arg("oledText");
  if (server.hasArg("oledTextSpeed")) doc["oledTextSpeed"] = server.arg("oledTextSpeed").toInt();
  if (server.hasArg("peltier")) doc["peltier"] = (server.arg("peltier") == "1" || server.arg("peltier") == "true");
  if (server.hasArg("action")) doc["action"] = server.arg("action");
  String cmd;
  serializeJson(doc, cmd);
  processCommandJson(cmd);
  server.send(200, "application/json", buildStatusJson(false));
}

void handleSwitchBle() {
  if (!checkHttpAuth()) return;
  server.send(200, "text/plain", "OK");
  prefs.putString("netMode", "ble");
  Serial.println("Pindah ke mode Bluetooth, restart...");
  delay(400);
  ESP.restart();
}

void handleSetSchedulesHttp() {
  if (!checkHttpAuth()) return;
  lastAppContact = millis();
  processCommandJson(server.arg("plain"));
  server.send(200, "application/json", "{\"result\":\"OK\"}");
}

void handleGetSchedulesHttp() {
  if (!checkHttpAuth()) return;
  lastAppContact = millis();
  server.send(200, "application/json", prefs.getString("schedules", "[]"));
}

void registerHttpHandlers() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scanwifi", HTTP_GET, handleScanWifi);
  server.on("/setwifi", HTTP_POST, handleSetWifi);
  server.on("/status", HTTP_GET, handleStatusHttp);
  server.on("/set", HTTP_POST, handleSetCmd);
  server.on("/switch_ble", HTTP_POST, handleSwitchBle);
  server.on("/schedules", HTTP_POST, handleSetSchedulesHttp);
  server.on("/schedules", HTTP_GET, handleGetSchedulesHttp);
}

void startConfigAP() {
  if (configApActive) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.begin();
  configApActive = true;
  Serial.println("Config AP aktif: " + String(AP_SSID) + " @ " + WiFi.softAPIP().toString());
}

void sendUdpBeacon() {
  JsonDocument doc;
  doc["deviceId"] = deviceId;
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  udp.beginPacket(IPAddress(255, 255, 255, 255), UDP_BEACON_PORT);
  udp.write((const uint8_t*)out.c_str(), out.length());
  udp.endPacket();
}

void startWifiControlMode(const String& ssid, const String& pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Menghubungkan ke WiFi: " + ssid);
  unsigned long attemptStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - attemptStart < 15000) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSUKSES: WiFi tersambung, IP: " + WiFi.localIP().toString());
    WiFi.setSleep(false);
    server.begin();
    udp.begin(UDP_BEACON_PORT);
    wifiControlActive = true;
    setupNtpTime();
  } else {
    Serial.println("\nGAGAL: Tidak bisa konek WiFi dalam 15 detik, kembali ke mode Bluetooth...");
    prefs.putString("netMode", "ble");
    prefs.putString("ssid", "");
    prefs.putString("pass", "");
    delay(300);
    ESP.restart();
  }
}

void startBleMode() {
  Serial.println("Menginisialisasi Bluetooth (NimBLE)...");

  NimBLEDevice::init(bleName.c_str());

  NimBLEDevice::setMTU(517);

  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  #ifdef ESP_PWR_LVL_P9
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  #else
    NimBLEDevice::setPower(9);
  #endif

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC |
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
    NIMBLE_PROPERTY::NOTIFY
  );
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pService->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setAppearance(0x0000);

  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setName(bleName.c_str());
  pAdvertising->setScanResponseData(scanResponseData);
  pAdvertising->enableScanResponse(true);

  bool advSuccess = pAdvertising->start();
  if (advSuccess) {
    Serial.println("SUKSES: BLE Advertising aktif dengan nama: " + bleName);
  } else {
    Serial.println("GAGAL: BLE Advertising gagal dimulai!");
  }
}

void handleBleAction(const String& action) {
  if (action == "start_wifi_setup") {
    NimBLEDevice::deinit(true);
    deviceConnected = false;
    pCharacteristic = nullptr;
    pServer = nullptr;
    delay(300);
    startConfigAP();
  }
}

void setup() {

  pinMode(PELTIER_PIN, OUTPUT);
  digitalWrite(PELTIER_PIN, LOW);

  Serial.begin(115200);
  delay(1000);
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  scanI2CBus();
  deviceId = computeDeviceId();
  bleName = "ESP32-Cooler-" + deviceId;
  Serial.println("BLE Device ID: " + deviceId);

  prefs.begin("cooler", false);
  netMode = prefs.getString("netMode", "ble");
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  httpAuthPass = loadOrCreateHttpAuthPass();
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_BTN_PIN) == LOW) {
    Serial.println("Tombol BOOT ditahan saat menyala - paksa balik ke mode Bluetooth + AP config.");
    netMode = "ble";
    prefs.putString("netMode", "ble");
  }

  float savedVoltage = prefs.getFloat("voltage", 5.0);
  int savedFanSpeed = prefs.getInt("fanSpeed", 100);
  String savedLedMode = prefs.getString("ledMode", "off");
  int savedLedSpeed = prefs.getInt("ledSpeed", 50);
  int savedLedBrightness = prefs.getInt("ledBright", 31);
  uint32_t savedCustomColor = prefs.getUInt("customColor", 0xFFFFFFu);
  customR = (savedCustomColor >> 16) & 0xFF;
  customG = (savedCustomColor >> 8) & 0xFF;
  customB = savedCustomColor & 0xFF;
  customPattern = prefs.getString("customPattern", "solid");
  loadWledColors();

  pinMode(PIN_ONBOARD_LED, OUTPUT);
  onboardLedWrite(false);

  ledcAttach(FAN_PWM_PIN, FAN_PWM_FREQ_HZ, FAN_PWM_RESOLUTION);
  setFanSpeed(savedFanSpeed);
  setLedSpeed(savedLedSpeed);

  pinMode(FAN_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), fanTachISR, FALLING);
  lastFanRpmCalc = millis();

  ch224aReady = ch224Begin();
  if (!ch224aReady) {
    Serial.println("CH224 not responding! Melanjutkan tanpa kontrol PD, akan dicoba lagi di background...");
  } else {
    Serial.print("CH224 terdeteksi di alamat I2C 0x");
    Serial.println(ch224Addr, HEX);
    applyVoltage(savedVoltage);
  }
  updatePdStatus();

  strip.begin();
  ledBrightnessPercent = savedLedBrightness;
  strip.setBrightness(map(savedLedBrightness, 0, 100, 0, 255));
  strip.show();
  applyLedMode(savedLedMode);

  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oledReady) {
    Serial.println("OLED SSD1315 tidak terdeteksi di 0x3C, melanjutkan tanpa display.");
  } else {
    u8f.begin(oled);
    u8f.setFontMode(1);
    u8f.setFontDirection(0);
    u8f.setForegroundColor(SSD1306_WHITE);
    u8f.setBackgroundColor(SSD1306_BLACK);

    playOledBootAnimation();

    roboEyes.begin(OLED_WIDTH, OLED_HEIGHT, 60);

    roboEyes.setWidth(34, 34);
    roboEyes.setHeight(26, 26);
    roboEyes.setBorderradius(6, 6);
    roboEyes.setSpacebetween(10);
    roboEyes.setAutoblinker(ON, 3, 2);
    roboEyes.setIdleMode(ON, 2, 2);
    roboEyes.setCuriosity(ON);
    roboEyes.setMood(DEFAULT);

    oledCustomText = prefs.getString("oledText", "");
    oledTextSpeedPercent = prefs.getInt("oledTextSpeed", 50);
    oledMarqueeX = OLED_WIDTH;
    oledSession = 0;
    oledEyeMood = -1;
    lastOledSessionSwitch = millis();
  }

  registerHttpHandlers();
  loadSchedulesFromPrefs();

  if (netMode == "wifi" && savedSsid.length() > 0) {
    startWifiControlMode(savedSsid, savedPass);
  } else {
    startBleMode();
    startConfigAP();
  }

  startMillis = millis();
}

void loop() {
  static char cmdBuf[BLE_RX_BUFFER_SIZE];
  cmdBuf[0] = '\0';
  portENTER_CRITICAL(&bleMux);

  if (bleWritePending) {
    memcpy(cmdBuf, bleCommandBuf, bleCommandLen);
    cmdBuf[bleCommandLen] = '\0';
    bleCommandLen = 0;
    bleCommandBuf[0] = '\0';
    bleWritePending = false;
  }
  portEXIT_CRITICAL(&bleMux);
  String cmd = String(cmdBuf);

  if (cmd.length() > 0) {
    processCommandJson(cmd);

    JsonDocument doc;
    if (!deserializeJson(doc, cmd) && doc["action"].is<const char*>()) {
      handleBleAction(doc["action"].as<String>());
    }
  }

  if (configApActive || wifiControlActive) {
    server.handleClient();
  }

  checkSchedulesAutonomous();

  if (wifiControlActive && millis() - lastBeacon > 2000) {
    sendUdpBeacon();
    lastBeacon = millis();
  }

  static unsigned long lastWifiCheck = 0;
  static unsigned long wifiDownSince = 0;
  if (wifiControlActive && millis() - lastWifiCheck > 2000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (wifiDownSince == 0) {
        wifiDownSince = millis();
        Serial.println("WiFi terputus, mencoba reconnect...");
        WiFi.reconnect();
      } else if (millis() - wifiDownSince > 20000) {
        Serial.println("WiFi tidak pulih dalam 20 detik, kembali ke mode Bluetooth...");
        prefs.putString("netMode", "ble");
        delay(300);
        ESP.restart();
      }
    } else {
      wifiDownSince = 0;
    }
  }

  handleStatusLed();
  handleLedAnimation();

  if (millis() - lastFanRpmCalc >= 1000) {
    updateFanRpm();
  }

  static unsigned long lastCh224Read = 0;
  static unsigned long lastCh224Retry = 0;
  if (ch224aReady) {
    if (millis() - lastCh224Read > 200) {
      lastCh224Read = millis();
      pgood = CH224X1->isPowerGood();
      current = CH224X1->getCurrentProfile() / 1000.0;
      chargerwatt = current * currentSetVoltage;
      updatePdStatus();
      Serial.print("Maximum current : ");
      Serial.print(current, 0);
      Serial.println(" A)");
      Serial.print("Available power : ");
      Serial.print(chargerwatt);
      Serial.println(" W");
      Serial.print("power good : ");
      Serial.println(pgood);
    }
  } else if (millis() - lastCh224Retry > 3000) {
    lastCh224Retry = millis();
    Serial.println("Mencoba deteksi ulang CH224A...");
    ch224aReady = ch224Begin();
    updatePdStatus();
    if (ch224aReady) {
      Serial.println("CH224A terdeteksi.");
      applyVoltage(currentSetVoltage);
    }
  }

  if (millis() - lastPublish > 300) {
    publishStatusBLE();
    lastPublish = millis();
  }

  updateOled();
}
