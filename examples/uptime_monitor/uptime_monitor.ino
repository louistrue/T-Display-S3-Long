/**
 * Uptime Monitor for T-Display S3 Long
 * Beautiful dark UI with touch interaction
 */

#include "AXS15231B.h"
#include "pins_config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

// ============================================================================
// Touch Configuration
// ============================================================================
#define TOUCH_ADDR 0x3B
#define TOUCH_SDA 15
#define TOUCH_SCL 10
#define TOUCH_RST 16
#define TOUCH_INT 11

uint8_t touchCmd[8] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};

// ============================================================================
// Color Palette - Byte-swapped RGB565 for this display
// ============================================================================
// Helper to swap bytes for RGB565 (display expects different byte order)
#define SW(c) (((c & 0xFF) << 8) | ((c >> 8) & 0xFF))

#define C_BG 0x0000           // Pure black (no swap needed)
#define C_CARD SW(0x1082)     // Dark gray card
#define C_CARD_HL SW(0x2104)  // Highlighted card
#define C_TEXT 0xFFFF         // White (no swap needed - symmetric)
#define C_TEXT_DIM SW(0x7BEF) // Gray text
#define C_GREEN SW(0x07E0)    // Bright green - UP is good!
#define C_GREEN_DK SW(0x03E0) // Dark green glow
#define C_RED SW(0xF800)      // Red - DOWN is bad!
#define C_RED_DK SW(0x7800)   // Dark red glow
#define C_ORANGE SW(0xFD20)   // Orange for warnings
#define C_CYAN SW(0x07FF)     // Cyan accent
#define C_HEADER SW(0x0841)   // Header bg

// ============================================================================
// Monitor Data Structure - Extended
// ============================================================================
struct Monitor {
  String id;
  String name;
  String url;
  String status;
  String monitorType;
  String lastChecked;
  int checkFrequency;
  int sslExpiration;
  int domainExpiration;
  String regions;
  bool valid = false;
};

#define MAX_MONITORS 12
Monitor monitors[MAX_MONITORS];
int monitorCount = 0;

// ============================================================================
// UI State
// ============================================================================
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 60000;
int scrollOffset = 0;
int maxScroll = 0;
bool isRefreshing = false;

// Touch state
int touchStartX = -1;
int touchStartY = -1;
int lastTouchX = -1;
int lastTouchY = -1;
unsigned long touchStartTime = 0;
bool isTouching = false;
bool isDragging = false;

// Detail view
int selectedMonitor = -1; // -1 = list view, >= 0 = detail view
unsigned long detailViewEnterTime = 0;
const unsigned long DETAIL_TIMEOUT = 10000; // 10 seconds

// ============================================================================
// Frame buffer
// ============================================================================
uint16_t *fb = nullptr;

// ============================================================================
// Font 6x8
// ============================================================================
const uint8_t font6x8[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00, 0x00, 0x00, 0x24, 0x2A, 0x7F, 0x2A,
    0x12, 0x00, 0x00, 0x00, 0x23, 0x13, 0x08, 0x64, 0x62, 0x00, 0x00, 0x00,
    0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x00, 0x00, 0x00, 0x05, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x08, 0x2A, 0x1C, 0x2A,
    0x08, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x50, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x3E, 0x51, 0x49, 0x45,
    0x3E, 0x00, 0x00, 0x00, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00, 0x21, 0x41, 0x45, 0x4B,
    0x31, 0x00, 0x00, 0x00, 0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, 0x00, 0x00,
    0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00, 0x3C, 0x4A, 0x49, 0x49,
    0x30, 0x00, 0x00, 0x00, 0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00, 0x00,
    0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00, 0x06, 0x49, 0x49, 0x29,
    0x1E, 0x00, 0x00, 0x00, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x56, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x14, 0x22,
    0x41, 0x00, 0x00, 0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00,
    0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x51, 0x09,
    0x06, 0x00, 0x00, 0x00, 0x32, 0x49, 0x79, 0x41, 0x3E, 0x00, 0x00, 0x00,
    0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00, 0x00, 0x00, 0x7F, 0x49, 0x49, 0x49,
    0x36, 0x00, 0x00, 0x00, 0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00,
    0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x7F, 0x49, 0x49, 0x49,
    0x41, 0x00, 0x00, 0x00, 0x7F, 0x09, 0x09, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x3E, 0x41, 0x41, 0x51, 0x32, 0x00, 0x00, 0x00, 0x7F, 0x08, 0x08, 0x08,
    0x7F, 0x00, 0x00, 0x00, 0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, 0x00, 0x00, 0x7F, 0x08, 0x14, 0x22,
    0x41, 0x00, 0x00, 0x00, 0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00,
    0x7F, 0x02, 0x04, 0x02, 0x7F, 0x00, 0x00, 0x00, 0x7F, 0x04, 0x08, 0x10,
    0x7F, 0x00, 0x00, 0x00, 0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, 0x00, 0x00,
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00, 0x3E, 0x41, 0x51, 0x21,
    0x5E, 0x00, 0x00, 0x00, 0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00, 0x00,
    0x46, 0x49, 0x49, 0x49, 0x31, 0x00, 0x00, 0x00, 0x01, 0x01, 0x7F, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, 0x00, 0x00,
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, 0x00, 0x00, 0x7F, 0x20, 0x18, 0x20,
    0x7F, 0x00, 0x00, 0x00, 0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00, 0x00,
    0x03, 0x04, 0x78, 0x04, 0x03, 0x00, 0x00, 0x00, 0x61, 0x51, 0x49, 0x45,
    0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x41, 0x41, 0x00, 0x00, 0x00,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00, 0x41, 0x41, 0x7F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x54, 0x54, 0x54, 0x78, 0x00, 0x00, 0x00,
    0x7F, 0x48, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x38, 0x44, 0x44, 0x44,
    0x20, 0x00, 0x00, 0x00, 0x38, 0x44, 0x44, 0x48, 0x7F, 0x00, 0x00, 0x00,
    0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x00, 0x00, 0x08, 0x7E, 0x09, 0x01,
    0x02, 0x00, 0x00, 0x00, 0x08, 0x14, 0x54, 0x54, 0x3C, 0x00, 0x00, 0x00,
    0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00, 0x00, 0x00, 0x44, 0x7D, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x44, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x41, 0x7F, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x7C, 0x04, 0x18, 0x04, 0x78, 0x00, 0x00, 0x00,
    0x7C, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00, 0x00, 0x38, 0x44, 0x44, 0x44,
    0x38, 0x00, 0x00, 0x00, 0x7C, 0x14, 0x14, 0x14, 0x08, 0x00, 0x00, 0x00,
    0x08, 0x14, 0x14, 0x18, 0x7C, 0x00, 0x00, 0x00, 0x7C, 0x08, 0x04, 0x04,
    0x08, 0x00, 0x00, 0x00, 0x48, 0x54, 0x54, 0x54, 0x20, 0x00, 0x00, 0x00,
    0x04, 0x3F, 0x44, 0x40, 0x20, 0x00, 0x00, 0x00, 0x3C, 0x40, 0x40, 0x20,
    0x7C, 0x00, 0x00, 0x00, 0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00, 0x00, 0x00,
    0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00, 0x00, 0x00, 0x44, 0x28, 0x10, 0x28,
    0x44, 0x00, 0x00, 0x00, 0x0C, 0x50, 0x50, 0x50, 0x3C, 0x00, 0x00, 0x00,
    0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, 0x00, 0x00,
};

// ============================================================================
// Drawing Helpers
// ============================================================================
inline void fbSetPixel(int x, int y, uint16_t c) {
  if (x >= 0 && x < 180 && y >= 0 && y < 640)
    fb[y * 180 + x] = c;
}

void fbClear(uint16_t c) {
  for (int i = 0; i < 180 * 640; i++)
    fb[i] = c;
}

void fbFillRect(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h && j < 640; j++) {
    if (j < 0)
      continue;
    for (int i = x; i < x + w && i < 180; i++) {
      if (i >= 0)
        fb[j * 180 + i] = c;
    }
  }
}

void fbFillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  fbFillRect(x + r, y, w - 2 * r, h, c);
  fbFillRect(x, y + r, r, h - 2 * r, c);
  fbFillRect(x + w - r, y + r, r, h - 2 * r, c);
  for (int i = 0; i < r; i++) {
    for (int j = 0; j < r; j++) {
      if (i * i + j * j <= r * r) {
        fbSetPixel(x + r - i - 1, y + r - j - 1, c);
        fbSetPixel(x + w - r + i, y + r - j - 1, c);
        fbSetPixel(x + r - i - 1, y + h - r + j, c);
        fbSetPixel(x + w - r + i, y + h - r + j, c);
      }
    }
  }
}

void fbFillCircle(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r)
        fbSetPixel(cx + x, cy + y, c);
    }
  }
}

void fbDrawHLine(int x, int y, int w, uint16_t c) {
  for (int i = x; i < x + w; i++)
    fbSetPixel(i, y, c);
}

void fbDrawChar(int x, int y, char ch, uint16_t c, int sz) {
  if (ch < 32 || ch > 122)
    return;
  int idx = (ch - 32) * 8;
  for (int i = 0; i < 6; i++) {
    uint8_t line = font6x8[idx + i];
    for (int j = 0; j < 8; j++) {
      if (line & (1 << j)) {
        for (int sy = 0; sy < sz; sy++) {
          for (int sx = 0; sx < sz; sx++) {
            fbSetPixel(x + i * sz + sx, y + j * sz + sy, c);
          }
        }
      }
    }
  }
}

void fbDrawText(int x, int y, const char *s, uint16_t c, int sz) {
  while (*s) {
    fbDrawChar(x, y, *s, c, sz);
    x += 6 * sz + (sz > 1 ? 1 : 0);
    s++;
  }
}

void fbDrawText(int x, int y, String s, uint16_t c, int sz) {
  fbDrawText(x, y, s.c_str(), c, sz);
}

int textWidth(const char *s, int sz) {
  return strlen(s) * (6 * sz + (sz > 1 ? 1 : 0));
}

void fbDrawTextCentered(int y, const char *s, uint16_t c, int sz) {
  int w = textWidth(s, sz);
  fbDrawText((180 - w) / 2, y, s, c, sz);
}

// ============================================================================
// Touch Functions
// ============================================================================
void initTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Serial.println("Touch I2C initialized");
}

bool readTouch(int &x, int &y) {
  uint8_t buf[14];
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(touchCmd, 8);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)8);

  unsigned long start = millis();
  while (!Wire.available() && millis() - start < 20)
    ;
  if (!Wire.available())
    return false;

  Wire.readBytes(buf, 8);

  uint8_t gesture = buf[0];
  uint8_t points = buf[1];

  if (points > 0 && gesture == 0) {
    int rawX = ((buf[2] & 0x0F) << 8) | buf[3];
    int rawY = ((buf[4] & 0x0F) << 8) | buf[5];

    // Transform coordinates for our screen orientation
    x = rawY;
    y = 640 - rawX;
    if (y < 0)
      y = 0;
    if (y > 639)
      y = 639;
    if (x < 0)
      x = 0;
    if (x > 179)
      x = 179;

    return true;
  }
  return false;
}

// ============================================================================
// Utility
// ============================================================================
String getDomain(String url) {
  if (url.startsWith("https://"))
    url = url.substring(8);
  if (url.startsWith("http://"))
    url = url.substring(7);
  int slash = url.indexOf('/');
  if (slash > 0)
    url = url.substring(0, slash);
  if (url.startsWith("www."))
    url = url.substring(4);
  return url;
}

uint16_t getStatusColor(String status) {
  if (status == "up")
    return C_GREEN;
  if (status == "down")
    return C_RED;
  if (status == "validating")
    return C_ORANGE;
  if (status == "pending")
    return C_CYAN;
  return C_ORANGE; // paused, maintenance, etc
}

uint16_t getStatusGlow(String status) {
  if (status == "up")
    return C_GREEN_DK;
  if (status == "down")
    return C_RED_DK;
  return C_HEADER;
}

// ============================================================================
// WiFi & Splash
// ============================================================================
void drawSplash(const char *msg) {
  fbClear(C_BG);
  fbDrawTextCentered(280, "UPTIME", C_CYAN, 4);
  fbDrawTextCentered(330, "MONITOR", C_TEXT, 3);
  fbDrawTextCentered(420, msg, C_TEXT_DIM, 2);
  lcd_PushColors(0, 0, 180, 640, fb);
}

void connectWiFi() {
  drawSplash("Connecting...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    drawSplash("Connected!");
    delay(500);
  } else {
    Serial.println("WiFi Failed");
    drawSplash("WiFi Failed");
    delay(2000);
  }
}

// ============================================================================
// API: Fetch Monitors
// ============================================================================
bool fetchMonitors() {
  isRefreshing = true;
  Serial.println("Fetching monitors...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, "https://uptime.betterstack.com/api/v2/monitors");
  http.addHeader("Authorization", String("Bearer ") + BETTERSTACK_TOKEN);

  int code = http.GET();
  Serial.printf("HTTP: %d\n", code);

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray data = doc["data"];
      monitorCount = 0;
      for (JsonVariant m : data) {
        if (monitorCount >= MAX_MONITORS)
          break;
        Monitor &mon = monitors[monitorCount];
        mon.id = m["id"].as<String>();
        mon.name = m["attributes"]["pronounceable_name"].as<String>();
        mon.url = m["attributes"]["url"].as<String>();
        mon.status = m["attributes"]["status"].as<String>();
        mon.monitorType = m["attributes"]["monitor_type"].as<String>();
        mon.lastChecked = m["attributes"]["last_checked_at"].as<String>();
        mon.checkFrequency = m["attributes"]["check_frequency"].as<int>();
        mon.sslExpiration = m["attributes"]["ssl_expiration"].as<int>();
        mon.domainExpiration = m["attributes"]["domain_expiration"].as<int>();

        // Get regions as string
        JsonArray regions = m["attributes"]["regions"];
        mon.regions = "";
        for (JsonVariant r : regions) {
          if (mon.regions.length() > 0)
            mon.regions += ", ";
          mon.regions += r.as<String>();
        }

        mon.valid = true;
        monitorCount++;
      }
      Serial.printf("Got %d monitors\n", monitorCount);
      http.end();
      isRefreshing = false;
      return true;
    }
  }
  http.end();
  isRefreshing = false;
  return false;
}

// ============================================================================
// Draw List View
// ============================================================================
void drawMonitorCard(int idx, int yPos) {
  if (yPos < -90 || yPos > 640)
    return;

  Monitor &m = monitors[idx];
  uint16_t statusColor = getStatusColor(m.status);
  uint16_t glowColor = getStatusGlow(m.status);
  bool isUp = (m.status == "up");

  // Card background
  fbFillRoundRect(8, yPos, 164, 85, 8, C_CARD);

  // Status accent bar on left (always shown)
  fbFillRoundRect(8, yPos, 5, 85, 2, statusColor);

  // Status indicator dot - ONLY show if NOT up (problems need attention)
  if (!isUp) {
    fbFillCircle(152, yPos + 25, 14, glowColor);
    fbFillCircle(152, yPos + 25, 10, statusColor);
  }

  // Domain name - can be wider if no dot
  String domain = getDomain(m.url);
  int maxLen = isUp ? 14 : 11;
  if (domain.length() > maxLen)
    domain = domain.substring(0, maxLen - 2) + "..";
  fbDrawText(20, yPos + 12, domain, C_TEXT, 2);

  // Monitor name (smaller)
  String name = m.name;
  if (name.length() > 20)
    name = name.substring(0, 18) + "..";
  fbDrawText(20, yPos + 38, name, C_TEXT_DIM, 1);

  // Status text
  fbDrawText(20, yPos + 55, m.status.c_str(), statusColor, 2);

  // Check frequency
  char freq[16];
  sprintf(freq, "%ds", m.checkFrequency);
  fbDrawText(100, yPos + 70, freq, C_TEXT_DIM, 1);

  // Tap hint
  fbDrawText(140, yPos + 70, ">", C_TEXT_DIM, 1);
}

void drawListView() {
  fbClear(C_BG);

  // Header
  fbFillRect(0, 0, 180, 50, C_HEADER);
  fbDrawText(10, 16, "MONITORS", C_CYAN, 2);

  // Count
  int up = 0, down = 0;
  for (int i = 0; i < monitorCount; i++) {
    if (monitors[i].status == "up")
      up++;
    else if (monitors[i].status == "down")
      down++;
  }

  // Status summary
  char summary[16];
  sprintf(summary, "%d/%d", up, monitorCount);
  uint16_t summaryColor = (up == monitorCount) ? C_GREEN : C_ORANGE;
  fbDrawText(130, 18, summary, summaryColor, 2);

  // Divider
  fbDrawHLine(0, 50, 180, C_CARD);

  // Calculate scroll
  maxScroll = max(0, (monitorCount * 95) - 570);
  if (scrollOffset > maxScroll)
    scrollOffset = maxScroll;
  if (scrollOffset < 0)
    scrollOffset = 0;

  // Draw cards
  if (monitorCount == 0) {
    fbDrawTextCentered(300, "No monitors", C_TEXT_DIM, 2);
    fbDrawTextCentered(330, "Tap to refresh", C_TEXT_DIM, 1);
  } else {
    for (int i = 0; i < monitorCount; i++) {
      int y = 60 + i * 95 - scrollOffset;
      drawMonitorCard(i, y);
    }
  }

  // Scrollbar
  if (monitorCount > 6) {
    int sbH = max(30, 570 * 570 / (monitorCount * 95));
    int sbY = 55 + (570 - sbH) * scrollOffset / maxScroll;
    fbFillRoundRect(175, 55, 3, 570, 1, C_HEADER);
    fbFillRoundRect(175, sbY, 3, sbH, 1, C_CYAN);
  }

  // Footer
  fbFillRect(0, 625, 180, 15, C_HEADER);
  unsigned long ago = (millis() - lastUpdate) / 1000;
  char footer[24];
  sprintf(footer, "Updated %lus ago", ago);
  fbDrawTextCentered(627, footer, C_TEXT_DIM, 1);

  lcd_PushColors(0, 0, 180, 640, fb);
}

// ============================================================================
// Draw Detail View
// ============================================================================
void drawDetailView() {
  if (selectedMonitor < 0 || selectedMonitor >= monitorCount)
    return;

  Monitor &m = monitors[selectedMonitor];
  uint16_t statusColor = getStatusColor(m.status);
  uint16_t glowColor = getStatusGlow(m.status);

  fbClear(C_BG);

  // Progress bar on left (top-down countdown)
  unsigned long elapsed = millis() - detailViewEnterTime;
  float progress = (float)elapsed / DETAIL_TIMEOUT;
  if (progress > 1.0)
    progress = 1.0;
  int barHeight = (int)(640 * progress);

  // Bar track (dim)
  fbFillRect(0, 0, 6, 640, C_HEADER);
  // Bar fill (cyan, grows from top)
  fbFillRect(0, 0, 6, barHeight, C_CYAN);

  // Header (no back button - auto returns)
  fbFillRect(6, 0, 174, 50, C_HEADER);
  fbDrawText(16, 16, "DETAILS", C_TEXT_DIM, 2);

  // Big status indicator
  fbFillCircle(96, 100, 35, glowColor);
  fbFillCircle(96, 100, 28, statusColor);

  // Status text
  String statusUpper = m.status;
  statusUpper.toUpperCase();
  fbDrawTextCentered(155, statusUpper.c_str(), statusColor, 3);

  // Domain
  String domain = getDomain(m.url);
  if (domain.length() > 14)
    domain = domain.substring(0, 12) + "..";
  fbDrawTextCentered(200, domain.c_str(), C_TEXT, 2);

  // Monitor name
  fbDrawTextCentered(225, m.name.substring(0, 20).c_str(), C_TEXT_DIM, 1);

  // Details section
  int y = 270;
  int rowH = 50;

  // Type
  fbFillRoundRect(10, y, 160, 40, 6, C_CARD);
  fbDrawText(18, y + 8, "Type", C_TEXT_DIM, 1);
  fbDrawText(18, y + 22, m.monitorType.c_str(), C_TEXT, 2);
  y += rowH;

  // Check frequency
  fbFillRoundRect(10, y, 160, 40, 6, C_CARD);
  fbDrawText(18, y + 8, "Check Interval", C_TEXT_DIM, 1);
  char freqStr[16];
  sprintf(freqStr, "%d seconds", m.checkFrequency);
  fbDrawText(18, y + 22, freqStr, C_TEXT, 2);
  y += rowH;

  // SSL expiration
  if (m.sslExpiration > 0) {
    fbFillRoundRect(10, y, 160, 40, 6, C_CARD);
    fbDrawText(18, y + 8, "SSL Expires In", C_TEXT_DIM, 1);
    char sslStr[16];
    sprintf(sslStr, "%d days", m.sslExpiration);
    uint16_t sslColor = m.sslExpiration < 14 ? C_ORANGE : C_GREEN;
    fbDrawText(18, y + 22, sslStr, sslColor, 2);
    y += rowH;
  }

  // Domain expiration
  if (m.domainExpiration > 0) {
    fbFillRoundRect(10, y, 160, 40, 6, C_CARD);
    fbDrawText(18, y + 8, "Domain Expires", C_TEXT_DIM, 1);
    char domStr[16];
    sprintf(domStr, "%d days", m.domainExpiration);
    uint16_t domColor = m.domainExpiration < 30 ? C_ORANGE : C_GREEN;
    fbDrawText(18, y + 22, domStr, domColor, 2);
    y += rowH;
  }

  // Regions
  if (m.regions.length() > 0) {
    fbFillRoundRect(10, y, 160, 40, 6, C_CARD);
    fbDrawText(18, y + 8, "Regions", C_TEXT_DIM, 1);
    String regDisplay = m.regions;
    if (regDisplay.length() > 14)
      regDisplay = regDisplay.substring(0, 12) + "..";
    fbDrawText(18, y + 22, regDisplay.c_str(), C_TEXT, 2);
    y += rowH;
  }

  // Last checked
  fbFillRoundRect(10, y, 160, 40, 6, C_CARD);
  fbDrawText(18, y + 8, "Last Checked", C_TEXT_DIM, 1);
  String lastCheck = m.lastChecked;
  if (lastCheck.length() > 16) {
    // Extract time part
    int tIdx = lastCheck.indexOf('T');
    if (tIdx > 0) {
      lastCheck = lastCheck.substring(tIdx + 1, tIdx + 9);
    }
  }
  fbDrawText(18, y + 22, lastCheck.c_str(), C_TEXT, 2);
  y += rowH;

  // URL at bottom
  fbDrawText(10, 600, "URL:", C_TEXT_DIM, 1);
  String urlDisplay = m.url;
  if (urlDisplay.length() > 28)
    urlDisplay = urlDisplay.substring(0, 26) + "..";
  fbDrawText(10, 615, urlDisplay.c_str(), C_CYAN, 1);

  lcd_PushColors(0, 0, 180, 640, fb);
}

// ============================================================================
// Draw current view
// ============================================================================
void drawUI() {
  if (selectedMonitor >= 0) {
    drawDetailView();
  } else {
    drawListView();
  }
}

// ============================================================================
// Touch Handler
// ============================================================================
void handleTouch() {
  int tx, ty;
  bool touching = readTouch(tx, ty);

  if (touching && !isTouching) {
    // Touch start
    isTouching = true;
    touchStartX = tx;
    touchStartY = ty;
    lastTouchX = tx;
    lastTouchY = ty;
    touchStartTime = millis();
    isDragging = false;
    Serial.printf("Touch START: %d, %d\n", tx, ty);
  } else if (touching && isTouching) {
    // Touch move
    int deltaY = lastTouchY - ty;
    if (abs(ty - touchStartY) > 15 || abs(tx - touchStartX) > 15) {
      isDragging = true;
    }

    if (isDragging && selectedMonitor < 0) {
      // Scroll in list view
      scrollOffset += deltaY;
    }
    lastTouchX = tx;
    lastTouchY = ty;
  } else if (!touching && isTouching) {
    // Touch end - use lastTouchX/Y since tx/ty are invalid now
    unsigned long touchDuration = millis() - touchStartTime;
    int totalMoveY = abs(lastTouchY - touchStartY);
    int totalMoveX = lastTouchX - touchStartX; // Positive = swipe right

    Serial.printf(
        "Touch END: start=(%d,%d) end=(%d,%d) dur=%lu moveX=%d moveY=%d\n",
        touchStartX, touchStartY, lastTouchX, lastTouchY, touchDuration,
        totalMoveX, totalMoveY);

    if (selectedMonitor >= 0) {
      // === DETAIL VIEW ===
      // No manual back - auto-returns after timeout
      // Touch resets the timer
      detailViewEnterTime = millis();
    } else {
      // === LIST VIEW ===
      // Was it a tap? (short touch, minimal movement)
      if (touchDuration < 500 && totalMoveY < 30 && abs(totalMoveX) < 30) {
        Serial.printf("TAP at x=%d y=%d (scroll=%d)\n", touchStartX,
                      touchStartY, scrollOffset);

        // Check for monitor tap
        if (touchStartY > 55 && touchStartY < 620) {
          int cardIdx = (touchStartY - 60 + scrollOffset) / 95;
          if (cardIdx >= 0 && cardIdx < monitorCount) {
            selectedMonitor = cardIdx;
            detailViewEnterTime = millis(); // Start timeout timer
            Serial.printf("Selected monitor: %d\n", cardIdx);
            drawUI();
          }
        }
        // Check for header tap (refresh)
        else if (touchStartY < 55) {
          Serial.println("Header tap - refresh");
          drawSplash("Refreshing...");
          fetchMonitors();
          lastUpdate = millis();
          drawUI();
        }
      }
    }

    isTouching = false;
    isDragging = false;
  }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\nUptime Monitor v2.1");

  // Allocate framebuffer
  fb = (uint16_t *)ps_malloc(180 * 640 * sizeof(uint16_t));
  if (!fb) {
    Serial.println("FATAL: No framebuffer!");
    while (1)
      delay(1000);
  }

  // Init touch I2C BEFORE display
  initTouch();

  // Init display
  axs15231_init();
  lcd_setRotation(0);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Clear and splash
  fbClear(C_BG);
  lcd_PushColors(0, 0, 180, 640, fb);

  drawSplash("Starting...");
  delay(500);

  connectWiFi();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  drawSplash("Loading...");
  fetchMonitors();
  lastUpdate = millis();

  drawUI();
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  handleTouch();

  // Redraw if scrolling (list view)
  static int lastScrollOffset = -1;
  if (scrollOffset != lastScrollOffset && selectedMonitor < 0) {
    drawUI();
    lastScrollOffset = scrollOffset;
  }

  // Detail view: animate progress bar + auto-return
  if (selectedMonitor >= 0) {
    static unsigned long lastDetailRedraw = 0;
    // Redraw every 100ms to animate the progress bar
    if (millis() - lastDetailRedraw > 100) {
      drawUI();
      lastDetailRedraw = millis();
    }
    // Auto-return after timeout
    if (millis() - detailViewEnterTime > DETAIL_TIMEOUT) {
      Serial.println("Detail timeout - returning to list");
      selectedMonitor = -1;
      drawUI();
    }
  }

  // Auto refresh (only in list view)
  if (selectedMonitor < 0 && millis() - lastUpdate > UPDATE_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED)
      connectWiFi();
    if (WiFi.status() == WL_CONNECTED)
      fetchMonitors();
    drawUI();
    lastUpdate = millis();
  }

  // Update footer time (only in list view)
  static unsigned long lastTimeUpdate = 0;
  if (millis() - lastTimeUpdate > 10000 && selectedMonitor < 0) {
    drawUI();
    lastTimeUpdate = millis();
  }

  delay(20);
}
