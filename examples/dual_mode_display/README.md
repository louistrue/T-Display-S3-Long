# Dual Mode Display

Switch between Uptime Monitor and Spotify Now Playing with a simple swipe gesture!

## Features

- **Two Modes in One**: Uptime Monitor + Spotify Player
- **Swipe Gesture Switching**: Swipe down from top to switch modes
- **Visual Mode Tabs**: See which mode you're in at a glance
- **Persistent State**: Each mode maintains its own state
- **Smart Updates**: Each mode updates on its own schedule

## Gesture Controls

### Mode Switching
- **Swipe down from top** → Switch between Uptime and Spotify modes

### Uptime Monitor Mode
- **Tap monitor card** → View details
- **Tap header** → Force refresh
- **Scroll** → Navigate through monitors

### Spotify Mode
- **Tap Previous/Play/Next buttons** → Control playback (Premium required)
- **Tap album art** → Force refresh track info
- **Auto-scrolling** → Long track names scroll automatically

## Visual Layout

```
┌──────────────────┐ 0
│[UPTIME][SPOTIFY] │ Mode tabs (shows active)
│ swipe to switch  │ Hint text
├──────────────────┤ 40
│                  │
│  Active Mode     │
│  Content         │
│  Displays        │
│  Here            │
│                  │
└──────────────────┘ 640
```

## Setup

### Requirements

All requirements from both examples:
- Uptime Monitor: BetterStack API token
- Spotify Player: Client ID, Secret, Refresh Token

### Installation

1. Install required libraries (same as spotify_player):
   - ArduinoJson
   - TJpg_Decoder
   - ESP32 Base64

2. Copy `config.h.example` to `config.h`

3. Fill in ALL credentials:
   - WiFi credentials
   - BetterStack token
   - Spotify client ID/secret/refresh token

4. Upload `dual_mode_display.ino`

## Configuration

Edit `config.h`:

```cpp
// WiFi (shared)
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"

// Uptime Monitor
#define BETTERSTACK_TOKEN "your_token"

// Spotify
#define SPOTIFY_CLIENT_ID "your_id"
#define SPOTIFY_CLIENT_SECRET "your_secret"
#define SPOTIFY_REFRESH_TOKEN "your_token"

// Gesture sensitivity
#define SWIPE_THRESHOLD 80        // Pixels to trigger swipe
#define SWIPE_ZONE_HEIGHT 80      // Top swipe zone height
```

## How It Works

### Mode Management

The app uses an enum to track the current mode:

```cpp
enum DisplayMode {
  MODE_UPTIME,
  MODE_SPOTIFY
};
```

Each mode has its own:
- Update timers
- State variables
- UI drawing functions
- Touch handlers

### Swipe Detection

Swipe down from top area (first 80 pixels):
1. Touch starts in swipe zone (y < 80)
2. Swipe down at least 80 pixels
3. Duration < 1 second
4. Triggers mode switch

### State Persistence

When you switch modes:
- Current mode state is preserved
- New mode resumes where it left off
- Update timers continue independently
- No data is lost

## Memory Usage

- **Framebuffer**: 225 KB (PSRAM)
- **Album art buffer**: 63 KB (PSRAM)
- **Monitor data**: ~2 KB (heap)
- **Spotify data**: ~2 KB (heap)
- **HTTP buffers**: ~16 KB (heap)

Total PSRAM: ~300 KB (of 8 MB)

## Troubleshooting

### Mode won't switch
- Make sure you're swiping from the top 80 pixels
- Swipe down at least 80 pixels
- Do it relatively quickly (< 1 second)

### Uptime mode not working
- Check BetterStack token in config.h
- Verify WiFi connection
- Check Serial Monitor for API errors

### Spotify mode not working
- Verify all Spotify credentials are correct
- Make sure you have Spotify Premium (for controls)
- Check that refresh token is valid

### Touch not responsive
- Check Serial Monitor for touch debugging
- Verify touch I2C is initialized
- Try power cycling the device

## Technical Details

### Update Intervals

- **Uptime Monitor**: 60 seconds
- **Spotify API**: 3 seconds
- **Spotify Progress**: 100ms (only when playing)
- **Mode Indicator**: Always visible

### Touch Debouncing

- Minimum swipe distance: 80 pixels
- Maximum swipe time: 1000ms
- Swipe zone height: 80 pixels from top

### API Rate Limiting

Both modes respect API rate limits:
- **BetterStack**: 60-second poll (1/minute)
- **Spotify**: 3-second poll (20/minute)

## Future Enhancements

Possible additions:
- More modes (weather, calendar, etc.)
- Horizontal swipe for additional actions
- Customizable mode order
- Theme switching
- Screensaver mode

## Credits

- **Uptime Monitor**: Based on uptime_monitor example
- **Spotify Player**: Based on spotify_player example
- **Gesture Control**: Custom implementation
- **Display Driver**: AXS15231B

## License

MIT License - Combine, modify, share freely!
