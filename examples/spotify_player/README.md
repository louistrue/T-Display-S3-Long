# Spotify Now Playing Display

A beautiful full-featured Spotify player interface for the T-Display-S3-Long with album art, touch controls, and real-time playback information.

![Spotify Player Demo](demo.jpg)

## Features

- **Album Art Display** - High-quality JPEG decoding with caching
- **Touch Controls** - Play/Pause, Next, Previous (Spotify Premium required)
- **Smooth Progress Bar** - Interpolated animation for fluid playback tracking
- **Scrolling Text** - Long track names scroll automatically
- **Device Info** - Shows active device and volume level
- **Auto-refresh** - Polls Spotify API every 3 seconds for updates

## Hardware Requirements

- **T-Display-S3-Long** (180×640 display, ESP32-S3, 8MB PSRAM)
- **WiFi Connection**
- **Spotify Premium Account** (for playback controls)

## Software Requirements

### Arduino IDE Libraries

Install these libraries via Arduino Library Manager:

1. **ArduinoJson** by Benoit Blanchon (v7.0.4 or later)
   - Library Manager → Search "ArduinoJson" → Install

2. **TJpg_Decoder** by Bodmer
   - Library Manager → Search "TJpg_Decoder" → Install

3. **ESP32 Base64** by Arturo Guadalupia
   - Library Manager → Search "base64" → Install the ESP32 version

### Board Support

Make sure you have ESP32 board support installed:
- **ESP32 by Espressif Systems** (version 2.0.11 or later)
- Board: ESP32S3 Dev Module
- Flash Size: 16MB
- PSRAM: OPI PSRAM

## Setup Instructions

### Step 1: Create Spotify App

1. Go to https://developer.spotify.com/dashboard
2. Log in with your Spotify account
3. Click **Create an App**
4. Fill in:
   - App Name: `ESP32 Now Playing`
   - App Description: `Personal Spotify player for ESP32`
5. Click **Create**
6. Click **Edit Settings**
7. Add Redirect URI: `http://127.0.0.1:8888/callback`
8. Click **Save**
9. Copy your **Client ID** and **Client Secret**

### Step 2: Get Your Refresh Token

1. Open `get_refresh_token.ino.example`
2. Fill in your WiFi credentials and Spotify Client ID/Secret
3. Upload to your T-Display-S3-Long
4. Open Serial Monitor (115200 baud)
5. Copy the authorization URL from Serial Monitor
6. Paste it into your web browser
7. Log in and authorize the app
8. After authorizing, you'll be redirected to `http://127.0.0.1:8888/callback?code=...`
9. **The page won't load - that's normal!**
10. Copy the **entire URL** from your browser's address bar
11. Paste it into the Serial Monitor
12. Your **refresh token** will be printed - save it!

### Step 3: Configure the Player

1. Copy `config.h.example` to `config.h`:
   ```bash
   cp config.h.example config.h
   ```

2. Edit `config.h` and fill in:
   - `WIFI_SSID` - Your WiFi network name
   - `WIFI_PASSWORD` - Your WiFi password
   - `SPOTIFY_CLIENT_ID` - From Step 1
   - `SPOTIFY_CLIENT_SECRET` - From Step 1
   - `SPOTIFY_REFRESH_TOKEN` - From Step 2

### Step 4: Upload

1. Open `spotify_player.ino` in Arduino IDE
2. Select board: **ESP32S3 Dev Module**
3. Configure board settings:
   - USB CDC On Boot: Enabled
   - Flash Size: 16MB
   - PSRAM: OPI PSRAM
   - Partition Scheme: Default 4MB with spiffs
4. Select your COM port
5. Click **Upload**

## Usage

### Display Layout

```
┌──────────────────┐ 0
│                  │
│    Album Art     │
│    (180×180)     │
│                  │
├──────────────────┤ 190
│   Track Name     │
│   (scrolls)      │
├──────────────────┤ 220
│   Artist Name    │
│   Album Name     │
├──────────────────┤ 360
│  Progress Bar    │
│  1:23 / 3:45     │
├──────────────────┤ 410
│  [<<] [>] [>>]   │  Touch controls
├──────────────────┤ 480
│  ● PLAYING       │
│  Device: Speaker │
│  Vol: 65%        │
└──────────────────┘ 640
```

### Touch Controls

- **Previous Button** - Skip to previous track
- **Play/Pause Button** - Toggle playback
- **Next Button** - Skip to next track
- **Tap Album Art** - Force refresh current track info

### Status Indicators

- **Green dot** - Currently playing
- **Orange dot** - Paused

## Configuration Options

Edit `config.h` to customize:

```cpp
// API polling interval (how often to check Spotify)
#define API_POLL_INTERVAL 3000  // 3 seconds

// Progress bar update rate
#define PROGRESS_UPDATE_INTERVAL 100  // 10 FPS

// Text scrolling speed
#define TEXT_SCROLL_DELAY 50  // milliseconds per pixel

// Album art caching
#define CACHE_ALBUM_ART true  // Don't re-download same album art
```

## API Rate Limits

Spotify's API has rate limits:
- **Web API calls**: ~180 requests per minute per user
- **Recommended polling**: 3-5 seconds
- This app polls every 3 seconds (20 requests/minute) - well within limits

## Troubleshooting

### "Nothing Playing" shows even when music is playing

- Make sure you're playing music on an active Spotify device
- Check that your access token is valid (restart the device)
- Verify your Spotify Premium account is active

### Album art not loading

- Check your WiFi connection
- Verify you have enough free heap memory
- Try increasing `API_POLL_INTERVAL` to reduce memory pressure
- Album art downloads can take 2-3 seconds on slow WiFi

### Touch controls not working

- **Spotify Premium is required** for playback control
- Make sure you requested the correct scopes during authorization
- Verify the device you're controlling supports remote control
- Check Serial Monitor for API error messages

### "Auth Failed" on startup

- Double-check your `config.h` credentials
- Make sure your refresh token is correct (re-run get_refresh_token)
- Verify your Spotify app settings match the redirect URI
- Check WiFi connection

### Compilation errors

- Make sure all required libraries are installed
- Update to latest ESP32 board support (2.0.11+)
- Verify you selected the correct board (ESP32S3 Dev Module)
- Check that PSRAM is enabled in board settings

## Memory Usage

Typical memory allocation:
- **Framebuffer**: 180 × 640 × 2 = 225 KB (PSRAM)
- **Album art buffer**: 180 × 180 × 2 = 63 KB (PSRAM)
- **JPEG download buffer**: ~100 KB (PSRAM)
- **HTTP buffers**: ~16 KB (heap)
- **JSON parsing**: ~8 KB (heap)

Total PSRAM usage: ~400 KB (of 8 MB available)

## Advanced Customization

### Change Color Scheme

Edit the color palette in `spotify_player.ino`:

```cpp
#define C_SPOTIFY_GREEN SW(0x1DB954 >> 8)  // Spotify green
#define C_PLAYING SW(0x1DB9)               // Playing indicator
#define C_PAUSED SW(0xFD20)                // Paused indicator (orange)
```

### Add More Controls

The touch handler is in the `handleTouch()` function. You can add volume controls, shuffle, repeat, etc. by:

1. Adding more touch zones in `handleTouch()`
2. Implementing the Spotify API calls (see https://developer.spotify.com/documentation/web-api)
3. Adding UI elements in `drawUI()`

### Display Lyrics

Spotify's API doesn't provide lyrics, but you could integrate:
- Genius API
- Musixmatch API
- Lyrics.ovh API

## Technical Details

### Spotify API Endpoints Used

- **Authentication**: `POST /api/token` - Refresh access token
- **Currently Playing**: `GET /v1/me/player/currently-playing` - Get track info
- **Play**: `PUT /v1/me/player/play` - Resume playback
- **Pause**: `PUT /v1/me/player/pause` - Pause playback
- **Next**: `POST /v1/me/player/next` - Skip to next track
- **Previous**: `POST /v1/me/player/previous` - Skip to previous

### JPEG Decoding

- Uses **TJpg_Decoder** library
- Decodes album art in chunks to save RAM
- Images are decoded directly to PSRAM buffer
- Typical 300×300 JPEG takes ~2 seconds to download and decode

### Touch Coordinate System

- Touch controller is at I2C address 0x3B
- Raw coordinates are transformed for screen orientation
- Touch zones are defined with generous margins (50×50px buttons)

## Credits

- **Display Driver**: AXS15231B by T-Display-S3-Long team
- **JPEG Decoder**: TJpg_Decoder by Bodmer
- **Spotify API**: https://developer.spotify.com
- **Example Code**: Inspired by uptime_monitor example

## License

MIT License - Feel free to modify and share!

## Contributing

Found a bug or want to add a feature? PRs welcome!

## Support

For issues specific to this player:
- Check the troubleshooting section above
- Open an issue on GitHub
- Check Serial Monitor output for debugging info

For Spotify API questions:
- https://developer.spotify.com/documentation/web-api

## Changelog

### v1.0 (2025-01-02)
- Initial release
- Album art display with JPEG decoding
- Touch controls for playback
- Smooth progress bar
- Scrolling text
- Device and volume display
