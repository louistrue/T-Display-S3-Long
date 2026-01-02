/**
 * Spotify Now Playing Display for T-Display S3 Long
 *
 * A beautiful full-featured Spotify player interface with:
 * - Album art display with JPEG decoding
 * - Touch controls for play/pause/skip (Spotify Premium required)
 * - Start favorites anytime, even when nothing is playing
 * - Smooth progress bar animation with local interpolation
 * - Scrolling text for long track names
 * - Volume bar visualization
 * - Device and playback status display
 *
 * Hardware: T-Display-S3-Long (180×640, ESP32-S3, 8MB PSRAM)
 * License: MIT
 */

#include "AXS15231B.h"
#include "config.h"
#include "pins_config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <base64.h>

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
// Color Palette - Spotify Theme (RGB565 byte-swapped)
// ============================================================================
#define SW(c) (((c & 0xFF) << 8) | ((c >> 8) & 0xFF))

// RGB565 values - byte swapped for display
#define C_BG 0x0000              // Pure black
#define C_CARD SW(0x1082)        // Dark card #181818
#define C_CARD_HOVER SW(0x2104)  // Hover state #282828
#define C_TEXT 0xFFFF            // White
#define C_TEXT_DIM SW(0xB596)    // Gray #B3B3B3
#define C_TEXT_DARK SW(0x6B4D)   // Darker gray #6A6A6A
#define C_SPOTIFY SW(0x1E0A)     // Spotify green #1DB954
#define C_SPOTIFY_DK SW(0x14A4)  // Dark green #1A7F37
#define C_PROGRESS_BG SW(0x4228) // Progress bg #404040
#define C_ORANGE SW(0xFD20)      // Orange for paused
#define C_RED SW(0xF800)         // Red for errors
#define C_HEART SW(0xF810)       // Pink for liked

// ============================================================================
// UI Layout Constants (180x640 screen)
// ============================================================================
#define ALBUM_ART_SIZE 180
#define ALBUM_ART_Y 0
#define TRACK_INFO_Y 190
#define ARTIST_INFO_Y 225
#define ALBUM_INFO_Y 245
#define PROGRESS_BAR_Y 280
#define TIME_LABELS_Y 300
#define CONTROLS_Y 340
#define CONTROLS_H 70
#define STATUS_Y 430
#define DEVICE_Y 470
#define VOLUME_Y 520

// Button sizes
#define BTN_SIZE 60
#define BTN_PLAY_SIZE 70
#define BTN_SPACING 10

// ============================================================================
// Spotify Data Structure
// ============================================================================
struct NowPlaying {
  String trackName;
  String artistName;
  String albumName;
  String albumArtUrl;
  String trackUri;
  int progressMs;
  int durationMs;
  bool isPlaying;
  String deviceName;
  String deviceId;
  int volumePercent;
  bool valid;
  bool hasActiveDevice;
  unsigned long lastUpdateTime;
};

NowPlaying currentTrack;
String lastAlbumArtUrl = "";
bool albumArtLoaded = false;

// ============================================================================
// Spotify Auth & State
// ============================================================================
String accessToken = "";
unsigned long tokenExpiry = 0;
bool isStartingPlayback = false; // Flag for showing "Starting..." state

// ============================================================================
// UI State
// ============================================================================
unsigned long lastApiPoll = 0;
unsigned long lastProgressUpdate = 0;
unsigned long lastScrollUpdate = 0;
int trackScrollOffset = 0;
int artistScrollOffset = 0;
bool needsRedraw = true;
bool needsFullRedraw = true;

// Touch state
int touchStartX = -1;
int touchStartY = -1;
unsigned long touchStartTime = 0;
bool isTouching = false;
int highlightedButton = -1; // -1=none, 0=prev, 1=play, 2=next, 3=favorites

// ============================================================================
// Frame buffer & Album Art Buffer
// ============================================================================
uint16_t *fb = nullptr;
uint16_t *albumArtBuffer = nullptr;

// ============================================================================
// HTTP Clients
// ============================================================================
WiFiClientSecure wifiClient;
HTTPClient httpClient;

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

void fbDrawCircle(int cx, int cy, int r, uint16_t c) {
  for (int a = 0; a < 360; a++) {
    float rad = a * 3.14159 / 180.0;
    int x = cx + r * cos(rad);
    int y = cy + r * sin(rad);
    fbSetPixel(x, y, c);
  }
}

void fbDrawHLine(int x, int y, int w, uint16_t c) {
  for (int i = x; i < x + w; i++)
    fbSetPixel(i, y, c);
}

void fbDrawVLine(int x, int y, int h, uint16_t c) {
  for (int i = y; i < y + h; i++)
    fbSetPixel(x, i, c);
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

int textWidth(String s, int sz) { return textWidth(s.c_str(), sz); }

void fbDrawTextCentered(int y, const char *s, uint16_t c, int sz) {
  int w = textWidth(s, sz);
  fbDrawText((180 - w) / 2, y, s, c, sz);
}

void fbDrawTextCentered(int y, String s, uint16_t c, int sz) {
  fbDrawTextCentered(y, s.c_str(), c, sz);
}

// Draw a play triangle
void fbDrawPlayIcon(int cx, int cy, int size, uint16_t c) {
  for (int i = 0; i < size; i++) {
    int h = (i * 2 * size) / size;
    fbDrawVLine(cx - size / 2 + i, cy - h / 2, h, c);
  }
}

// Draw pause bars
void fbDrawPauseIcon(int cx, int cy, int size, uint16_t c) {
  int barW = size / 3;
  int gap = size / 4;
  fbFillRect(cx - gap - barW, cy - size / 2, barW, size, c);
  fbFillRect(cx + gap, cy - size / 2, barW, size, c);
}

// Draw skip icon (two triangles)
void fbDrawSkipIcon(int cx, int cy, int size, bool next, uint16_t c) {
  int dir = next ? 1 : -1;
  for (int i = 0; i < size / 2; i++) {
    int h = (i * size) / (size / 2);
    int x = next ? (cx - size / 3 + i) : (cx + size / 3 - i);
    fbDrawVLine(x, cy - h / 2, h, c);
  }
  for (int i = 0; i < size / 2; i++) {
    int h = (i * size) / (size / 2);
    int x = next ? (cx + i) : (cx - i);
    fbDrawVLine(x, cy - h / 2, h, c);
  }
}

// Draw heart icon
void fbDrawHeartIcon(int cx, int cy, int size, uint16_t c, bool filled) {
  // Simple heart shape
  int r = size / 3;
  if (filled) {
    fbFillCircle(cx - r, cy - r / 2, r, c);
    fbFillCircle(cx + r, cy - r / 2, r, c);
    // Bottom triangle
    for (int y = 0; y < size; y++) {
      int w = size - (y * size) / size;
      fbFillRect(cx - w, cy - r / 2 + y, w * 2, 1, c);
    }
  } else {
    fbDrawCircle(cx - r, cy - r / 2, r, c);
    fbDrawCircle(cx + r, cy - r / 2, r, c);
  }
}

// Forward declaration
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);

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
// WiFi & Splash
// ============================================================================
void drawSplash(const char *msg, const char *submsg = nullptr) {
  fbClear(C_BG);

  // Spotify branding
  fbFillCircle(90, 250, 40, C_SPOTIFY);
  fbDrawPlayIcon(95, 250, 30, C_BG);

  fbDrawTextCentered(320, "SPOTIFY", C_TEXT, 3);
  fbDrawTextCentered(360, msg, C_TEXT_DIM, 2);
  if (submsg) {
    fbDrawTextCentered(400, submsg, C_TEXT_DARK, 1);
  }
  lcd_PushColors(0, 0, 180, 640, fb);
}

void connectWiFi() {
  drawSplash("Connecting...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    drawSplash("Connected!", WiFi.localIP().toString().c_str());
    delay(500);
  } else {
    Serial.println("WiFi Failed");
    drawSplash("WiFi Failed!", "Check credentials");
    delay(3000);
  }
}

// ============================================================================
// Spotify API - Authentication
// ============================================================================
bool refreshAccessToken() {
  Serial.println("Refreshing Spotify access token...");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://accounts.spotify.com/api/token");
  httpClient.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String auth = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
  String authB64 = base64::encode(auth);
  httpClient.addHeader("Authorization", "Basic " + authB64);

  String postData =
      "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN);

  int code = httpClient.POST(postData);
  Serial.printf("Token refresh HTTP: %d\n", code);

  bool success = false;
  if (code == 200) {
    String payload = httpClient.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      accessToken = doc["access_token"].as<String>();
      int expiresIn = doc["expires_in"].as<int>();
      tokenExpiry = millis() + (expiresIn - 60) * 1000;
      Serial.println("Access token refreshed successfully");
      success = true;
    }
  } else {
    Serial.printf("Token refresh failed: %d\n", code);
  }

  httpClient.end();
  return success;
}

bool ensureValidToken() {
  if (accessToken.length() == 0 || millis() >= tokenExpiry) {
    return refreshAccessToken();
  }
  return true;
}

// ============================================================================
// Spotify API - Get Player State (includes device info)
// ============================================================================
bool fetchPlayerState() {
  if (!ensureValidToken())
    return false;

  Serial.println("Fetching player state...");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://api.spotify.com/v1/me/player");
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.setTimeout(10000);

  int code = httpClient.GET();
  Serial.printf("Player state HTTP: %d\n", code);

  bool success = false;

  if (code == 200) {
    String payload = httpClient.getString();
    JsonDocument doc;

    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      // Device info
      if (!doc["device"].isNull()) {
        currentTrack.deviceName = doc["device"]["name"].as<String>();
        currentTrack.deviceId = doc["device"]["id"].as<String>();
        currentTrack.volumePercent = doc["device"]["volume_percent"].as<int>();
        currentTrack.hasActiveDevice = true;
      }

      // Track info
      if (!doc["item"].isNull()) {
        currentTrack.trackName = doc["item"]["name"].as<String>();
        currentTrack.trackUri = doc["item"]["uri"].as<String>();

        JsonArray artists = doc["item"]["artists"];
        currentTrack.artistName = "";
        for (size_t i = 0; i < artists.size() && i < 3; i++) {
          if (i > 0)
            currentTrack.artistName += ", ";
          currentTrack.artistName += artists[i]["name"].as<String>();
        }

        currentTrack.albumName = doc["item"]["album"]["name"].as<String>();

        JsonArray images = doc["item"]["album"]["images"];
        if (images.size() > 1) {
          currentTrack.albumArtUrl = images[1]["url"].as<String>();
        } else if (images.size() > 0) {
          currentTrack.albumArtUrl = images[0]["url"].as<String>();
        }

        currentTrack.progressMs = doc["progress_ms"].as<int>();
        currentTrack.durationMs = doc["item"]["duration_ms"].as<int>();
        currentTrack.isPlaying = doc["is_playing"].as<bool>();
        currentTrack.valid = true;
        currentTrack.lastUpdateTime = millis();

        Serial.printf("Playing: %s - %s\n", currentTrack.artistName.c_str(),
                      currentTrack.trackName.c_str());
        success = true;
      } else {
        // Device active but nothing playing
        currentTrack.valid = false;
        currentTrack.hasActiveDevice = true;
      }
    }
  } else if (code == 204) {
    Serial.println("No active device");
    currentTrack.valid = false;
    currentTrack.hasActiveDevice = false;
  }

  httpClient.end();
  return success;
}

// Alias for backward compatibility
bool fetchCurrentlyPlaying() { return fetchPlayerState(); }

// ============================================================================
// Spotify API - Get Available Devices
// ============================================================================
String getFirstAvailableDevice() {
  if (!ensureValidToken())
    return "";

  Serial.println("Getting available devices...");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://api.spotify.com/v1/me/player/devices");
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.setTimeout(10000);

  int code = httpClient.GET();
  Serial.printf("Devices HTTP: %d\n", code);

  String deviceId = "";

  if (code == 200) {
    String payload = httpClient.getString();
    JsonDocument doc;

    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray devices = doc["devices"];
      for (size_t i = 0; i < devices.size(); i++) {
        String id = devices[i]["id"].as<String>();
        String name = devices[i]["name"].as<String>();
        bool isActive = devices[i]["is_active"].as<bool>();
        Serial.printf("Device: %s (%s) active=%d\n", name.c_str(), id.c_str(),
                      isActive);

        // Prefer active device, otherwise take first
        if (isActive || deviceId.length() == 0) {
          deviceId = id;
          if (isActive)
            break;
        }
      }
    }
  }

  httpClient.end();
  return deviceId;
}

// ============================================================================
// Spotify API - Start Liked Songs (Favorites)
// ============================================================================
bool startFavorites() {
  if (!ensureValidToken())
    return false;

  Serial.println("Starting Liked Songs...");
  isStartingPlayback = true;
  needsRedraw = true;

  // First, try to get a device
  String deviceId = currentTrack.deviceId;
  if (deviceId.length() == 0) {
    deviceId = getFirstAvailableDevice();
  }

  if (deviceId.length() == 0) {
    Serial.println("No device available!");
    isStartingPlayback = false;
    return false;
  }

  wifiClient.setInsecure();
  String url =
      "https://api.spotify.com/v1/me/player/play?device_id=" + deviceId;
  httpClient.begin(wifiClient, url);
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.addHeader("Content-Type", "application/json");

  // Start liked songs with shuffle
  String body = "{\"context_uri\":\"spotify:user:" + String(SPOTIFY_CLIENT_ID) +
                ":collection\"}";
  // Alternative: just start playback without specific context (resumes last)
  // For liked songs, we use a different approach - transfer playback and
  // shuffle

  int code = httpClient.PUT(body);
  Serial.printf("Start favorites HTTP: %d\n", code);

  // If context_uri fails (403), try starting without context
  if (code == 403 || code == 404) {
    httpClient.end();

    // Try alternative: just resume/start playback
    httpClient.begin(wifiClient, url);
    httpClient.addHeader("Authorization", "Bearer " + accessToken);
    httpClient.addHeader("Content-Length", "0");
    code = httpClient.PUT("");
    Serial.printf("Resume playback HTTP: %d\n", code);
  }

  httpClient.end();
  isStartingPlayback = false;

  if (code == 204 || code == 202 || code == 200) {
    delay(500);
    fetchPlayerState();
    if (currentTrack.valid && currentTrack.albumArtUrl.length() > 0) {
      downloadAlbumArt(currentTrack.albumArtUrl);
    }
    needsFullRedraw = true;
    return true;
  }

  return false;
}

// ============================================================================
// Spotify API - Playback Control
// ============================================================================
bool spotifyPlayPause() {
  if (!ensureValidToken())
    return false;

  // If nothing is playing, start favorites
  if (!currentTrack.valid || !currentTrack.hasActiveDevice) {
    return startFavorites();
  }

  const char *endpoint = currentTrack.isPlaying
                             ? "https://api.spotify.com/v1/me/player/pause"
                             : "https://api.spotify.com/v1/me/player/play";

  Serial.printf("Sending %s command\n",
                currentTrack.isPlaying ? "pause" : "play");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, endpoint);
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.addHeader("Content-Length", "0");

  int code = httpClient.PUT("");
  Serial.printf("Play/Pause HTTP: %d\n", code);

  httpClient.end();

  if (code == 204 || code == 202) {
    currentTrack.isPlaying = !currentTrack.isPlaying;
    currentTrack.lastUpdateTime = millis();
    return true;
  }
  return false;
}

bool spotifyNext() {
  if (!ensureValidToken())
    return false;

  Serial.println("Sending next command");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://api.spotify.com/v1/me/player/next");
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.addHeader("Content-Length", "0");

  int code = httpClient.POST("");
  Serial.printf("Next HTTP: %d\n", code);

  httpClient.end();

  if (code == 204 || code == 202) {
    lastAlbumArtUrl = "";
    albumArtLoaded = false;
    delay(500);
    fetchPlayerState();
    if (currentTrack.valid && currentTrack.albumArtUrl.length() > 0) {
      downloadAlbumArt(currentTrack.albumArtUrl);
    }
    return true;
  }
  return false;
}

bool spotifyPrevious() {
  if (!ensureValidToken())
    return false;

  Serial.println("Sending previous command");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://api.spotify.com/v1/me/player/previous");
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.addHeader("Content-Length", "0");

  int code = httpClient.POST("");
  Serial.printf("Previous HTTP: %d\n", code);

  httpClient.end();

  if (code == 204 || code == 202) {
    lastAlbumArtUrl = "";
    albumArtLoaded = false;
    delay(500);
    fetchPlayerState();
    if (currentTrack.valid && currentTrack.albumArtUrl.length() > 0) {
      downloadAlbumArt(currentTrack.albumArtUrl);
    }
    return true;
  }
  return false;
}

// ============================================================================
// Album Art Download & Decode
// ============================================================================
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h,
                uint16_t *bitmap) {
  if (!albumArtBuffer)
    return false;

  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int destX = x + i;
      int destY = y + j;
      if (destX >= 0 && destX < 180 && destY >= 0 && destY < 180) {
        albumArtBuffer[destY * 180 + destX] = bitmap[j * w + i];
      }
    }
  }
  return true;
}

bool downloadAlbumArt(String url) {
  if (url.length() == 0)
    return false;

  if (CACHE_ALBUM_ART && url == lastAlbumArtUrl && albumArtLoaded) {
    Serial.println("Using cached album art");
    return true;
  }

  Serial.println("Downloading album art: " + url);

  String host, path;
  String urlCopy = url;
  if (urlCopy.startsWith("https://")) {
    urlCopy = urlCopy.substring(8);
  }
  int slashIdx = urlCopy.indexOf('/');
  if (slashIdx > 0) {
    host = urlCopy.substring(0, slashIdx);
    path = urlCopy.substring(slashIdx);
  } else {
    return false;
  }

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://" + host + path);
  httpClient.setTimeout(15000);

  int code = httpClient.GET();
  Serial.printf("Album art HTTP: %d\n", code);

  bool success = false;

  if (code == 200) {
    int len = httpClient.getSize();
    Serial.printf("Image size: %d bytes\n", len);

    uint8_t *jpegBuf = (uint8_t *)ps_malloc(len + 1024);
    if (jpegBuf) {
      WiFiClient *stream = httpClient.getStreamPtr();
      int bytesRead = stream->readBytes(jpegBuf, len);

      if (bytesRead == len) {
        TJpgDec.setJpgScale(1);
        TJpgDec.setCallback(tft_output);

        if (TJpgDec.drawJpg(0, 0, jpegBuf, len) == 0) {
          Serial.println("Album art decoded successfully");
          lastAlbumArtUrl = url;
          albumArtLoaded = true;
          success = true;
        } else {
          Serial.println("JPEG decode failed");
        }
      }
      free(jpegBuf);
    }
  }

  httpClient.end();
  return success;
}

// ============================================================================
// UI Drawing - Album Art Area
// ============================================================================
void drawAlbumArt() {
  if (!albumArtLoaded || !albumArtBuffer) {
    // Draw placeholder with gradient
    for (int y = 0; y < ALBUM_ART_SIZE; y++) {
      uint16_t shade = (y * 0x08) / ALBUM_ART_SIZE;
      uint16_t c = SW((shade << 11) | (shade << 6) | shade);
      for (int x = 0; x < 180; x++) {
        fb[y * 180 + x] = C_CARD;
      }
    }

    // Music note icon
    fbFillCircle(90, 90, 30, C_TEXT_DARK);
    fbDrawTextCentered(82, "?", C_CARD, 4);
    return;
  }

  // Copy album art
  for (int y = 0; y < ALBUM_ART_SIZE; y++) {
    for (int x = 0; x < 180; x++) {
      fb[y * 180 + x] = albumArtBuffer[y * 180 + x];
    }
  }
}

// ============================================================================
// UI Drawing - Track Info with Scrolling
// ============================================================================
void drawTrackInfo() {
  // Clear track info area
  fbFillRect(0, TRACK_INFO_Y - 5, 180, 70, C_BG);

  // Track name - large, white, scrolling if needed
  String trackName = currentTrack.trackName;
  int trackWidth = textWidth(trackName, 2);

  if (trackWidth > 170) {
    // Scrolling animation
    int maxScroll = trackWidth - 150;
    int scrollTime = millis() / 40;
    int scrollPos = scrollTime % (maxScroll * 2);
    if (scrollPos > maxScroll)
      scrollPos = maxScroll * 2 - scrollPos;

    fbDrawText(5 - scrollPos, TRACK_INFO_Y, trackName, C_TEXT, 2);

    // Fade edges
    for (int i = 0; i < 8; i++) {
      uint8_t alpha = i * 32;
      fbFillRect(i, TRACK_INFO_Y, 1, 20, C_BG);
      fbFillRect(179 - i, TRACK_INFO_Y, 1, 20, C_BG);
    }
  } else {
    fbDrawTextCentered(TRACK_INFO_Y, trackName, C_TEXT, 2);
  }

  // Artist name - medium, gray
  String artistName = currentTrack.artistName;
  int artistWidth = textWidth(artistName, 1);

  if (artistWidth > 170) {
    // Scrolling with different phase
    int maxScroll = artistWidth - 150;
    int scrollTime = (millis() + 2000) / 50;
    int scrollPos = scrollTime % (maxScroll * 2);
    if (scrollPos > maxScroll)
      scrollPos = maxScroll * 2 - scrollPos;

    fbDrawText(5 - scrollPos, ARTIST_INFO_Y, artistName, C_TEXT_DIM, 1);

    for (int i = 0; i < 5; i++) {
      fbFillRect(i, ARTIST_INFO_Y, 1, 10, C_BG);
      fbFillRect(179 - i, ARTIST_INFO_Y, 1, 10, C_BG);
    }
  } else {
    fbDrawTextCentered(ARTIST_INFO_Y, artistName, C_TEXT_DIM, 1);
  }

  // Album name - small, darker gray
  String albumName = currentTrack.albumName;
  if (albumName.length() > 28) {
    albumName = albumName.substring(0, 26) + "..";
  }
  fbDrawTextCentered(ALBUM_INFO_Y, albumName, C_TEXT_DARK, 1);
}

// ============================================================================
// UI Drawing - Progress Bar
// ============================================================================
void drawProgressBar() {
  int barX = 10;
  int barW = 160;
  int barH = 4;
  int knobR = 6;

  // Calculate interpolated progress
  int displayProgress = currentTrack.progressMs;
  if (currentTrack.isPlaying && currentTrack.valid) {
    unsigned long elapsed = millis() - currentTrack.lastUpdateTime;
    displayProgress += elapsed;
    if (displayProgress > currentTrack.durationMs) {
      displayProgress = currentTrack.durationMs;
    }
  }

  float progress = 0;
  if (currentTrack.durationMs > 0) {
    progress = (float)displayProgress / currentTrack.durationMs;
  }

  // Background bar
  fbFillRoundRect(barX, PROGRESS_BAR_Y, barW, barH, 2, C_PROGRESS_BG);

  // Progress fill
  int fillW = (int)(barW * progress);
  if (fillW > 0) {
    fbFillRoundRect(barX, PROGRESS_BAR_Y, fillW, barH, 2, C_SPOTIFY);
  }

  // Knob (only if playing)
  if (currentTrack.valid) {
    int knobX = barX + fillW;
    fbFillCircle(knobX, PROGRESS_BAR_Y + barH / 2, knobR, C_TEXT);
  }

  // Time labels
  int currentSec = displayProgress / 1000;
  int totalSec = currentTrack.durationMs / 1000;

  char timeStr[16];
  sprintf(timeStr, "%d:%02d", currentSec / 60, currentSec % 60);
  fbDrawText(10, TIME_LABELS_Y, timeStr, C_TEXT_DIM, 1);

  sprintf(timeStr, "%d:%02d", totalSec / 60, totalSec % 60);
  int tw = textWidth(timeStr, 1);
  fbDrawText(170 - tw, TIME_LABELS_Y, timeStr, C_TEXT_DIM, 1);
}

// ============================================================================
// UI Drawing - Controls
// ============================================================================
void drawControls() {
  int centerX = 90;
  int y = CONTROLS_Y;

  // Previous button
  int prevX = centerX - BTN_PLAY_SIZE / 2 - BTN_SPACING - BTN_SIZE;
  uint16_t prevColor = (highlightedButton == 0) ? C_CARD_HOVER : C_CARD;
  fbFillCircle(prevX + BTN_SIZE / 2, y + BTN_SIZE / 2, BTN_SIZE / 2, prevColor);
  fbDrawSkipIcon(prevX + BTN_SIZE / 2, y + BTN_SIZE / 2, 20, false, C_TEXT);

  // Play/Pause button (larger, Spotify green when playing)
  uint16_t playBgColor = currentTrack.isPlaying ? C_SPOTIFY : C_TEXT;
  uint16_t playFgColor = C_BG;
  if (highlightedButton == 1)
    playBgColor = C_SPOTIFY_DK;

  fbFillCircle(centerX, y + BTN_PLAY_SIZE / 2, BTN_PLAY_SIZE / 2, playBgColor);

  if (currentTrack.isPlaying) {
    fbDrawPauseIcon(centerX, y + BTN_PLAY_SIZE / 2, 24, playFgColor);
  } else {
    fbDrawPlayIcon(centerX + 3, y + BTN_PLAY_SIZE / 2, 24, playFgColor);
  }

  // Next button
  int nextX = centerX + BTN_PLAY_SIZE / 2 + BTN_SPACING;
  uint16_t nextColor = (highlightedButton == 2) ? C_CARD_HOVER : C_CARD;
  fbFillCircle(nextX + BTN_SIZE / 2, y + BTN_SIZE / 2, BTN_SIZE / 2, nextColor);
  fbDrawSkipIcon(nextX + BTN_SIZE / 2, y + BTN_SIZE / 2, 20, true, C_TEXT);
}

// ============================================================================
// UI Drawing - Status & Device Info
// ============================================================================
void drawStatusBar() {
  fbFillRect(0, STATUS_Y, 180, 80, C_BG);

  // Playing status with animated dot
  uint16_t statusColor = currentTrack.isPlaying ? C_SPOTIFY : C_ORANGE;
  const char *statusText = currentTrack.isPlaying ? "NOW PLAYING" : "PAUSED";

  // Pulsing dot when playing
  int dotSize = 4;
  if (currentTrack.isPlaying) {
    int pulse = (millis() / 500) % 2;
    dotSize = pulse ? 5 : 4;
  }

  fbFillCircle(15, STATUS_Y + 8, dotSize, statusColor);
  fbDrawText(25, STATUS_Y + 2, statusText, statusColor, 1);

  // Shuffle/Repeat icons could go here
}

void drawDeviceInfo() {
  fbFillRect(0, DEVICE_Y, 180, 40, C_BG);

  fbDrawText(10, DEVICE_Y, "DEVICE", C_TEXT_DARK, 1);

  String deviceName = currentTrack.deviceName;
  if (deviceName.length() == 0)
    deviceName = "No device";
  if (deviceName.length() > 24) {
    deviceName = deviceName.substring(0, 22) + "..";
  }
  fbDrawText(10, DEVICE_Y + 14, deviceName, C_TEXT_DIM, 1);
}

void drawVolumeBar() {
  fbFillRect(0, VOLUME_Y, 180, 30, C_BG);

  // Volume icon
  fbDrawText(10, VOLUME_Y + 4, "Vol", C_TEXT_DARK, 1);

  // Volume bar
  int barX = 40;
  int barW = 100;
  int barH = 4;
  int y = VOLUME_Y + 8;

  fbFillRoundRect(barX, y, barW, barH, 2, C_PROGRESS_BG);

  int fillW = (barW * currentTrack.volumePercent) / 100;
  if (fillW > 0) {
    fbFillRoundRect(barX, y, fillW, barH, 2, C_TEXT_DIM);
  }

  // Volume percentage
  char volStr[8];
  sprintf(volStr, "%d%%", currentTrack.volumePercent);
  int tw = textWidth(volStr, 1);
  fbDrawText(170 - tw, VOLUME_Y + 4, volStr, C_TEXT_DIM, 1);
}

// ============================================================================
// UI Drawing - Nothing Playing State
// ============================================================================
void drawNothingPlaying() {
  fbClear(C_BG);

  // Big play button in center
  int centerY = 280;

  // Outer ring
  fbDrawCircle(90, centerY, 55, C_CARD);
  fbDrawCircle(90, centerY, 54, C_CARD);

  // Filled circle (button)
  uint16_t btnColor = (highlightedButton == 3) ? C_SPOTIFY_DK : C_SPOTIFY;
  fbFillCircle(90, centerY, 50, btnColor);

  // Play icon
  fbDrawPlayIcon(95, centerY, 35, C_BG);

  // Text
  fbDrawTextCentered(370, "Tap to play", C_TEXT, 2);
  fbDrawTextCentered(400, "your favorites", C_TEXT_DIM, 1);

  // Status
  if (isStartingPlayback) {
    fbDrawTextCentered(450, "Starting...", C_SPOTIFY, 1);
  } else if (currentTrack.hasActiveDevice) {
    fbDrawTextCentered(450, "Device ready", C_TEXT_DARK, 1);
    fbDrawTextCentered(470, currentTrack.deviceName, C_TEXT_DIM, 1);
  } else {
    fbDrawTextCentered(450, "Open Spotify on a device", C_TEXT_DARK, 1);
  }

  // Spotify branding at bottom
  fbDrawTextCentered(600, "Spotify", C_TEXT_DARK, 1);
}

// ============================================================================
// Main UI Draw
// ============================================================================
void drawUI() {
  if (!currentTrack.valid) {
    drawNothingPlaying();
    lcd_PushColors(0, 0, 180, 640, fb);
    return;
  }

  fbClear(C_BG);
  drawAlbumArt();
  drawTrackInfo();
  drawProgressBar();
  drawControls();
  drawStatusBar();
  drawDeviceInfo();
  drawVolumeBar();

  lcd_PushColors(0, 0, 180, 640, fb);
}

// ============================================================================
// Touch Handler
// ============================================================================
void handleTouch() {
  int tx, ty;
  bool touching = readTouch(tx, ty);

  if (touching && !isTouching) {
    isTouching = true;
    touchStartX = tx;
    touchStartY = ty;
    touchStartTime = millis();

    // Highlight detection
    highlightedButton = -1;

    if (!currentTrack.valid) {
      // Check big play button (center of screen)
      int dx = tx - 90;
      int dy = ty - 280;
      if (dx * dx + dy * dy < 50 * 50) {
        highlightedButton = 3;
      }
    } else {
      // Check control buttons
      int y = CONTROLS_Y;
      int centerX = 90;

      // Previous
      int prevCX = centerX - BTN_PLAY_SIZE / 2 - BTN_SPACING - BTN_SIZE / 2;
      if ((tx - prevCX) * (tx - prevCX) +
              (ty - y - BTN_SIZE / 2) * (ty - y - BTN_SIZE / 2) <
          (BTN_SIZE / 2) * (BTN_SIZE / 2)) {
        highlightedButton = 0;
      }

      // Play/Pause
      if ((tx - centerX) * (tx - centerX) +
              (ty - y - BTN_PLAY_SIZE / 2) * (ty - y - BTN_PLAY_SIZE / 2) <
          (BTN_PLAY_SIZE / 2) * (BTN_PLAY_SIZE / 2)) {
        highlightedButton = 1;
      }

      // Next
      int nextCX = centerX + BTN_PLAY_SIZE / 2 + BTN_SPACING + BTN_SIZE / 2;
      if ((tx - nextCX) * (tx - nextCX) +
              (ty - y - BTN_SIZE / 2) * (ty - y - BTN_SIZE / 2) <
          (BTN_SIZE / 2) * (BTN_SIZE / 2)) {
        highlightedButton = 2;
      }
    }

    if (highlightedButton >= 0) {
      needsRedraw = true;
    }

  } else if (!touching && isTouching) {
    unsigned long touchDuration = millis() - touchStartTime;

    if (touchDuration < 500) {
      Serial.printf("TAP at x=%d y=%d button=%d\n", touchStartX, touchStartY,
                    highlightedButton);

      if (highlightedButton == 3) {
        // Big play button - start favorites
        startFavorites();
      } else if (highlightedButton == 0) {
        spotifyPrevious();
      } else if (highlightedButton == 1) {
        spotifyPlayPause();
      } else if (highlightedButton == 2) {
        spotifyNext();
      } else if (touchStartY < ALBUM_ART_SIZE && currentTrack.valid) {
        // Tap album art to refresh
        Serial.println("Album art tapped - refreshing");
        fetchPlayerState();
        if (currentTrack.valid && currentTrack.albumArtUrl != lastAlbumArtUrl) {
          downloadAlbumArt(currentTrack.albumArtUrl);
        }
      }

      needsRedraw = true;
    }

    highlightedButton = -1;
    isTouching = false;
    needsRedraw = true;
  }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\nSpotify Now Playing Display v2.0");

  // Allocate framebuffer in PSRAM
  fb = (uint16_t *)ps_malloc(180 * 640 * sizeof(uint16_t));
  if (!fb) {
    Serial.println("FATAL: No framebuffer!");
    while (1)
      delay(1000);
  }

  // Allocate album art buffer in PSRAM
  albumArtBuffer = (uint16_t *)ps_malloc(180 * 180 * sizeof(uint16_t));
  if (!albumArtBuffer) {
    Serial.println("FATAL: No album art buffer!");
    while (1)
      delay(1000);
  }

  // Clear album art buffer
  memset(albumArtBuffer, 0, 180 * 180 * sizeof(uint16_t));

  // Init touch BEFORE display
  initTouch();

  // Init display
  axs15231_init();
  lcd_setRotation(0);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Initial splash
  fbClear(C_BG);
  lcd_PushColors(0, 0, 180, 640, fb);
  drawSplash("Starting...");
  delay(300);

  // Connect WiFi
  connectWiFi();

  // Authenticate
  drawSplash("Authenticating...");
  if (refreshAccessToken()) {
    drawSplash("Connected!", "Loading...");
  } else {
    drawSplash("Auth Failed!", "Check credentials");
    delay(3000);
  }

  // Initial fetch
  fetchPlayerState();
  if (currentTrack.valid && currentTrack.albumArtUrl.length() > 0) {
    drawSplash("Loading art...");
    downloadAlbumArt(currentTrack.albumArtUrl);
  }

  drawUI();
  lastApiPoll = millis();
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  handleTouch();

  // Poll Spotify API periodically
  if (millis() - lastApiPoll > API_POLL_INTERVAL) {
    bool hadTrack = currentTrack.valid;
    String oldArtUrl = currentTrack.albumArtUrl;

    fetchPlayerState();

    // Check if album art changed
    if (currentTrack.valid && currentTrack.albumArtUrl != oldArtUrl) {
      downloadAlbumArt(currentTrack.albumArtUrl);
    }

    // Full redraw if state changed significantly
    if (hadTrack != currentTrack.valid) {
      needsFullRedraw = true;
    }

    needsRedraw = true;
    lastApiPoll = millis();
  }

  // Smooth progress bar updates
  if (millis() - lastProgressUpdate > PROGRESS_UPDATE_INTERVAL) {
    if (currentTrack.valid && currentTrack.isPlaying) {
      needsRedraw = true;
    }
    lastProgressUpdate = millis();
  }

  // Redraw
  if (needsRedraw || needsFullRedraw) {
    drawUI();
    needsRedraw = false;
    needsFullRedraw = false;
  }

  delay(20);
}
