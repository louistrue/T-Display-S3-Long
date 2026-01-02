/**
 * Spotify Now Playing Display for T-Display S3 Long
 *
 * A beautiful full-featured Spotify player interface with:
 * - Album art display with JPEG decoding
 * - Touch controls for play/pause/skip (Spotify Premium required)
 * - Smooth progress bar animation
 * - Scrolling text for long track names
 * - Device and volume display
 *
 * Hardware: T-Display-S3-Long (180×640, ESP32-S3, 8MB PSRAM)
 * Author: Generated for your T-Display-S3-Long
 * License: MIT
 */

#include "AXS15231B.h"
#include "pins_config.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <TJpg_Decoder.h>
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
// Color Palette - Spotify Theme
// ============================================================================
#define SW(c) (((c & 0xFF) << 8) | ((c >> 8) & 0xFF))

#define C_BG 0x0000                // Pure black background
#define C_SPOTIFY_GREEN SW(0x1DB954 >> 8) // Spotify green (adjusted for RGB565)
#define C_SPOTIFY_GREEN_DK SW(0x0A5A2A >> 8) // Dark green
#define C_TEXT 0xFFFF              // White text
#define C_TEXT_DIM SW(0x8410)      // Dim gray text
#define C_CARD SW(0x1082)          // Dark card background
#define C_PROGRESS_BG SW(0x4208)   // Progress bar background
#define C_BUTTON SW(0x2104)        // Button background
#define C_BUTTON_ACTIVE SW(0x39E7) // Active button
#define C_PLAYING SW(0x1DB9)       // Playing indicator (Spotify green in RGB565)
#define C_PAUSED SW(0xFD20)        // Paused indicator (orange)

// ============================================================================
// Spotify Data Structure
// ============================================================================
struct NowPlaying {
    String trackName;
    String artistName;
    String albumName;
    String albumArtUrl;
    int progressMs;
    int durationMs;
    bool isPlaying;
    String deviceName;
    int volumePercent;
    bool valid;
    unsigned long lastUpdateTime; // millis() when we got this data
};

NowPlaying currentTrack;
String lastAlbumArtUrl = "";
bool albumArtLoaded = false;

// ============================================================================
// Spotify Auth
// ============================================================================
String accessToken = "";
unsigned long tokenExpiry = 0;

// ============================================================================
// UI State
// ============================================================================
unsigned long lastApiPoll = 0;
unsigned long lastProgressUpdate = 0;
unsigned long lastScrollUpdate = 0;
int scrollOffset = 0;
bool needsRedraw = true;

// Touch state
int touchStartX = -1;
int touchStartY = -1;
unsigned long touchStartTime = 0;
bool isTouching = false;

// ============================================================================
// Frame buffer & Album Art Buffer
// ============================================================================
uint16_t *fb = nullptr;
uint16_t *albumArtBuffer = nullptr; // 180×180 album art

// ============================================================================
// HTTP Clients (Global to prevent memory leaks)
// ============================================================================
WiFiClientSecure wifiClient;
HTTPClient httpClient;

// ============================================================================
// Font 6x8 (copied from uptime_monitor)
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
// Drawing Helpers (from uptime_monitor)
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

int textWidth(String s, int sz) {
  return textWidth(s.c_str(), sz);
}

void fbDrawTextCentered(int y, const char *s, uint16_t c, int sz) {
  int w = textWidth(s, sz);
  fbDrawText((180 - w) / 2, y, s, c, sz);
}

// Forward declaration for JPEG decoder callback
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

// ============================================================================
// Touch Functions (from uptime_monitor)
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
// WiFi & Splash
// ============================================================================
void drawSplash(const char *msg) {
  fbClear(C_BG);
  // Spotify logo color
  fbDrawTextCentered(280, "SPOTIFY", C_PLAYING, 3);
  fbDrawTextCentered(320, "NOW PLAYING", C_TEXT, 2);
  fbDrawTextCentered(420, msg, C_TEXT_DIM, 2);
  lcd_PushColors(0, 0, 180, 640, fb);
}

void connectWiFi() {
  drawSplash("Connecting WiFi...");
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
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    drawSplash("Connected!");
    delay(500);
  } else {
    Serial.println("WiFi Failed");
    drawSplash("WiFi Failed!");
    delay(2000);
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

  // Create Basic Auth header
  String auth = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
  String authB64 = base64::encode(auth);
  httpClient.addHeader("Authorization", "Basic " + authB64);

  // POST data
  String postData = "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN);

  int code = httpClient.POST(postData);
  Serial.printf("Token refresh HTTP: %d\n", code);

  bool success = false;
  if (code == 200) {
    String payload = httpClient.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      accessToken = doc["access_token"].as<String>();
      int expiresIn = doc["expires_in"].as<int>();
      tokenExpiry = millis() + (expiresIn - 60) * 1000; // Refresh 1 min early
      Serial.println("Access token refreshed successfully");
      success = true;
    } else {
      Serial.println("Token JSON parse error");
    }
  } else {
    Serial.printf("Token refresh failed: %d\n", code);
    Serial.println(httpClient.getString());
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
// Spotify API - Currently Playing
// ============================================================================
bool fetchCurrentlyPlaying() {
  if (!ensureValidToken()) {
    Serial.println("No valid token, skipping fetch");
    return false;
  }

  Serial.println("Fetching currently playing...");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://api.spotify.com/v1/me/player/currently-playing");
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.setTimeout(10000);

  int code = httpClient.GET();
  Serial.printf("Currently playing HTTP: %d\n", code);

  bool success = false;

  if (code == 200) {
    String payload = httpClient.getString();
    JsonDocument doc;

    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      // Track info
      currentTrack.trackName = doc["item"]["name"].as<String>();

      // Artists (combine if multiple)
      JsonArray artists = doc["item"]["artists"];
      currentTrack.artistName = "";
      for (size_t i = 0; i < artists.size(); i++) {
        if (i > 0) currentTrack.artistName += ", ";
        currentTrack.artistName += artists[i]["name"].as<String>();
      }

      currentTrack.albumName = doc["item"]["album"]["name"].as<String>();

      // Album art (use medium size - index 1)
      JsonArray images = doc["item"]["album"]["images"];
      if (images.size() > 1) {
        currentTrack.albumArtUrl = images[1]["url"].as<String>();
      } else if (images.size() > 0) {
        currentTrack.albumArtUrl = images[0]["url"].as<String>();
      }

      currentTrack.progressMs = doc["progress_ms"].as<int>();
      currentTrack.durationMs = doc["item"]["duration_ms"].as<int>();
      currentTrack.isPlaying = doc["is_playing"].as<bool>();

      // Device info
      if (doc["device"]) {
        currentTrack.deviceName = doc["device"]["name"].as<String>();
        currentTrack.volumePercent = doc["device"]["volume_percent"].as<int>();
      }

      currentTrack.valid = true;
      currentTrack.lastUpdateTime = millis();

      Serial.printf("Playing: %s - %s\n",
                   currentTrack.artistName.c_str(),
                   currentTrack.trackName.c_str());
      success = true;
    } else {
      Serial.println("JSON parse error");
    }
  } else if (code == 204) {
    // No content - nothing playing
    Serial.println("Nothing currently playing");
    currentTrack.valid = false;
  } else {
    Serial.printf("HTTP error: %d\n", code);
  }

  httpClient.end();
  return success;
}

// ============================================================================
// Spotify API - Playback Control
// ============================================================================
bool spotifyPlayPause() {
  if (!ensureValidToken()) return false;

  const char* endpoint = currentTrack.isPlaying ?
    "https://api.spotify.com/v1/me/player/pause" :
    "https://api.spotify.com/v1/me/player/play";

  Serial.printf("Sending %s command\n", currentTrack.isPlaying ? "pause" : "play");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, endpoint);
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.addHeader("Content-Length", "0");

  int code = httpClient.PUT("");
  Serial.printf("Play/Pause HTTP: %d\n", code);

  httpClient.end();

  if (code == 204 || code == 202) {
    currentTrack.isPlaying = !currentTrack.isPlaying;
    return true;
  }
  return false;
}

bool spotifyNext() {
  if (!ensureValidToken()) return false;

  Serial.println("Sending next command");

  wifiClient.setInsecure();
  httpClient.begin(wifiClient, "https://api.spotify.com/v1/me/player/next");
  httpClient.addHeader("Authorization", "Bearer " + accessToken);
  httpClient.addHeader("Content-Length", "0");

  int code = httpClient.POST("");
  Serial.printf("Next HTTP: %d\n", code);

  httpClient.end();

  if (code == 204 || code == 202) {
    // Clear album art cache so it reloads
    lastAlbumArtUrl = "";
    albumArtLoaded = false;
    // Fetch new track info immediately
    delay(500);
    fetchCurrentlyPlaying();
    return true;
  }
  return false;
}

bool spotifyPrevious() {
  if (!ensureValidToken()) return false;

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
    fetchCurrentlyPlaying();
    return true;
  }
  return false;
}

// ============================================================================
// Album Art Download & Decode
// ============================================================================
// TJpg_Decoder callback to draw image chunks to album art buffer
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Draw to album art buffer (180x180)
  if (!albumArtBuffer) return false;

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
  if (url.length() == 0) return false;

  // Check cache
  if (CACHE_ALBUM_ART && url == lastAlbumArtUrl && albumArtLoaded) {
    Serial.println("Using cached album art");
    return true;
  }

  Serial.println("Downloading album art: " + url);

  // Extract host and path from URL
  String host, path;
  if (url.startsWith("https://")) {
    url = url.substring(8);
  }
  int slashIdx = url.indexOf('/');
  if (slashIdx > 0) {
    host = url.substring(0, slashIdx);
    path = url.substring(slashIdx);
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

    // Allocate buffer in PSRAM for JPEG data
    uint8_t* jpegBuf = (uint8_t*)ps_malloc(len);
    if (jpegBuf) {
      WiFiClient* stream = httpClient.getStreamPtr();
      int bytesRead = stream->readBytes(jpegBuf, len);

      if (bytesRead == len) {
        // Decode JPEG
        TJpgDec.setJpgScale(1); // Scale: 1=1/1, 2=1/2, 4=1/4, 8=1/8
        TJpgDec.setCallback(tft_output);

        if (TJpgDec.drawJpg(0, 0, jpegBuf, len) == 0) {
          Serial.println("Album art decoded successfully");
          lastAlbumArtUrl = currentTrack.albumArtUrl;
          albumArtLoaded = true;
          success = true;
        } else {
          Serial.println("JPEG decode failed");
        }
      }

      free(jpegBuf);
    } else {
      Serial.println("Failed to allocate JPEG buffer");
    }
  }

  httpClient.end();
  return success;
}

// ============================================================================
// UI Drawing
// ============================================================================
void drawAlbumArt() {
  if (!albumArtLoaded || !albumArtBuffer) {
    // Draw placeholder
    fbFillRect(0, 0, 180, 180, C_CARD);
    fbDrawTextCentered(85, "No album", C_TEXT_DIM, 2);
    fbDrawTextCentered(105, "art", C_TEXT_DIM, 2);
    return;
  }

  // Copy album art buffer to framebuffer
  for (int y = 0; y < 180; y++) {
    for (int x = 0; x < 180; x++) {
      fb[y * 180 + x] = albumArtBuffer[y * 180 + x];
    }
  }
}

void drawTrackInfo() {
  int y = 190;

  // Track name (scrolling if too long)
  String trackName = currentTrack.trackName;
  if (trackName.length() > 21) {
    // Scroll text
    int fullWidth = textWidth(trackName, 2);
    int visibleWidth = 170;
    int maxOffset = fullWidth - visibleWidth + 20;

    if (maxOffset > 0) {
      int cycleTime = 5000; // Full scroll cycle in ms
      int offsetMs = (millis() / 50) % (cycleTime / 50);
      int offset = (offsetMs * maxOffset) / (cycleTime / 50);
      fbDrawText(5 - offset, y, trackName, C_TEXT, 2);

      // Draw fade on edges
      for (int i = 0; i < 5; i++) {
        fbFillRect(i, y, 1, 18, C_BG);
        fbFillRect(179 - i, y, 1, 18, C_BG);
      }
    } else {
      fbDrawTextCentered(y, trackName.c_str(), C_TEXT, 2);
    }
  } else {
    fbDrawTextCentered(y, trackName.c_str(), C_TEXT, 2);
  }

  y += 30;

  // Artist name
  String artistName = currentTrack.artistName;
  if (artistName.length() > 28) {
    artistName = artistName.substring(0, 26) + "..";
  }
  fbDrawTextCentered(y, artistName.c_str(), C_TEXT_DIM, 1);

  y += 15;

  // Album name
  String albumName = currentTrack.albumName;
  if (albumName.length() > 28) {
    albumName = albumName.substring(0, 26) + "..";
  }
  fbDrawTextCentered(y, albumName.c_str(), C_TEXT_DIM, 1);
}

void drawProgressBar() {
  int y = 360;
  int barY = y + 10;
  int barH = 6;
  int barW = 160;
  int barX = 10;

  // Calculate current progress (with interpolation if playing)
  int displayProgress = currentTrack.progressMs;
  if (currentTrack.isPlaying) {
    unsigned long elapsed = millis() - currentTrack.lastUpdateTime;
    displayProgress += elapsed;
    if (displayProgress > currentTrack.durationMs) {
      displayProgress = currentTrack.durationMs;
    }
  }

  // Progress bar background
  fbFillRoundRect(barX, barY, barW, barH, 3, C_PROGRESS_BG);

  // Progress bar fill
  int fillW = (barW * displayProgress) / currentTrack.durationMs;
  if (fillW > 0) {
    fbFillRoundRect(barX, barY, fillW, barH, 3, C_PLAYING);
  }

  // Time labels
  int currentSec = displayProgress / 1000;
  int totalSec = currentTrack.durationMs / 1000;

  char timeStr[16];
  sprintf(timeStr, "%d:%02d", currentSec / 60, currentSec % 60);
  fbDrawText(10, y + 20, timeStr, C_TEXT_DIM, 1);

  sprintf(timeStr, "%d:%02d", totalSec / 60, totalSec % 60);
  int tw = textWidth(timeStr, 1);
  fbDrawText(170 - tw, y + 20, timeStr, C_TEXT_DIM, 1);
}

void drawControls() {
  int y = 410;
  int buttonW = 50;
  int buttonH = 50;
  int spacing = 5;

  // Three buttons: Previous, Play/Pause, Next
  int startX = (180 - (3 * buttonW + 2 * spacing)) / 2;

  // Previous button (⏮)
  fbFillRoundRect(startX, y, buttonW, buttonH, 8, C_BUTTON);
  fbDrawTextCentered(y + 18, "<<", C_TEXT, 2);

  // Play/Pause button (▶/⏸)
  fbFillRoundRect(startX + buttonW + spacing, y, buttonW, buttonH, 8, C_BUTTON_ACTIVE);
  if (currentTrack.isPlaying) {
    fbDrawTextCentered(y + 18, "||", C_TEXT, 2);
  } else {
    fbDrawTextCentered(y + 18, ">", C_TEXT, 2);
  }

  // Next button (⏭)
  fbFillRoundRect(startX + 2 * (buttonW + spacing), y, buttonW, buttonH, 8, C_BUTTON);
  fbDrawTextCentered(y + 18, ">>", C_TEXT, 2);
}

void drawDeviceInfo() {
  int y = 480;

  // Playing status indicator
  uint16_t statusColor = currentTrack.isPlaying ? C_PLAYING : C_PAUSED;
  const char* statusText = currentTrack.isPlaying ? "PLAYING" : "PAUSED";

  fbFillCircle(20, y + 6, 4, statusColor);
  fbDrawText(30, y, statusText, statusColor, 1);

  y += 20;

  // Device name
  fbDrawText(10, y, "Device:", C_TEXT_DIM, 1);
  String deviceName = currentTrack.deviceName;
  if (deviceName.length() > 20) {
    deviceName = deviceName.substring(0, 18) + "..";
  }
  fbDrawText(10, y + 12, deviceName, C_TEXT, 1);

  // Volume
  if (currentTrack.volumePercent >= 0) {
    char volStr[16];
    sprintf(volStr, "Vol: %d%%", currentTrack.volumePercent);
    fbDrawText(10, y + 24, volStr, C_TEXT_DIM, 1);
  }
}

void drawNothingPlaying() {
  fbClear(C_BG);
  fbDrawTextCentered(280, "Nothing", C_TEXT_DIM, 3);
  fbDrawTextCentered(320, "Playing", C_TEXT_DIM, 3);
  fbDrawTextCentered(400, "Start playback on", C_TEXT_DIM, 1);
  fbDrawTextCentered(415, "your Spotify device", C_TEXT_DIM, 1);
  lcd_PushColors(0, 0, 180, 640, fb);
}

void drawUI() {
  if (!currentTrack.valid) {
    drawNothingPlaying();
    return;
  }

  fbClear(C_BG);
  drawAlbumArt();
  drawTrackInfo();
  drawProgressBar();
  drawControls();
  drawDeviceInfo();
  lcd_PushColors(0, 0, 180, 640, fb);
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
    touchStartTime = millis();
    Serial.printf("Touch START: %d, %d\n", tx, ty);
  } else if (!touching && isTouching) {
    // Touch end - check for tap
    unsigned long touchDuration = millis() - touchStartTime;

    if (touchDuration < 500) {
      Serial.printf("TAP at x=%d y=%d\n", touchStartX, touchStartY);

      // Check if tap is in control area (y: 410-460)
      if (touchStartY >= 410 && touchStartY <= 460) {
        int buttonW = 50;
        int spacing = 5;
        int startX = (180 - (3 * buttonW + 2 * spacing)) / 2;

        // Previous button
        if (touchStartX >= startX && touchStartX < startX + buttonW) {
          Serial.println("Previous button tapped");
          spotifyPrevious();
          needsRedraw = true;
        }
        // Play/Pause button
        else if (touchStartX >= startX + buttonW + spacing &&
                 touchStartX < startX + 2 * buttonW + spacing) {
          Serial.println("Play/Pause button tapped");
          spotifyPlayPause();
          needsRedraw = true;
        }
        // Next button
        else if (touchStartX >= startX + 2 * (buttonW + spacing) &&
                 touchStartX < startX + 3 * buttonW + 2 * spacing) {
          Serial.println("Next button tapped");
          spotifyNext();
          needsRedraw = true;
        }
      }
      // Tap album art to refresh
      else if (touchStartY < 180) {
        Serial.println("Album art tapped - refreshing");
        fetchCurrentlyPlaying();
        if (currentTrack.valid && currentTrack.albumArtUrl.length() > 0) {
          downloadAlbumArt(currentTrack.albumArtUrl);
        }
        needsRedraw = true;
      }
    }

    isTouching = false;
  }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\nSpotify Now Playing Display v1.0");

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

  drawSplash("Authenticating...");
  if (refreshAccessToken()) {
    drawSplash("Ready!");
  } else {
    drawSplash("Auth Failed!");
    Serial.println("Failed to get access token!");
  }
  delay(1000);

  // Initial fetch
  drawSplash("Loading...");
  fetchCurrentlyPlaying();
  if (currentTrack.valid && currentTrack.albumArtUrl.length() > 0) {
    downloadAlbumArt(currentTrack.albumArtUrl);
  }

  drawUI();
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  handleTouch();

  // Poll Spotify API periodically
  if (millis() - lastApiPoll > API_POLL_INTERVAL) {
    if (fetchCurrentlyPlaying()) {
      // Check if album art changed
      if (currentTrack.albumArtUrl != lastAlbumArtUrl) {
        downloadAlbumArt(currentTrack.albumArtUrl);
      }
      needsRedraw = true;
    }
    lastApiPoll = millis();
  }

  // Update progress bar and scrolling text
  if (millis() - lastProgressUpdate > PROGRESS_UPDATE_INTERVAL) {
    if (currentTrack.valid) {
      needsRedraw = true;
    }
    lastProgressUpdate = millis();
  }

  // Redraw if needed
  if (needsRedraw) {
    drawUI();
    needsRedraw = false;
  }

  delay(20);
}
