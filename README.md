# Jingle Machine

ESP32-based sound jingle player with Cheap Yellow Display (CYD), Bluetooth audio output, and web-based configuration.

## Features

- **8 Touch Buttons** - Play different audio jingles with a simple touch
- **Bluetooth Audio** - Streams audio to external Bluetooth speakers via A2DP with MAC address pairing
- **SD Card Storage** - WAV files stored on SD card (40MHz SPI for smooth streaming)
- **Smart Startup Flow**:
  - **Normal Mode** - Waits 30s for Bluetooth connection, then plays jingles
  - **Settings Mode** - Automatic entry after BT timeout, web-based configuration + OTA updates
- **Advanced Customization**:
  - Button labels, colors, and audio file assignments
  - Global text rotation (0°, 90°, 180°, 270°)
  - Global border color and thickness
  - Audio fade-in/fade-out to prevent clicks
- **Optimized Performance** - WiFi disabled during playback for glitch-free audio

## Hardware Requirements

- ESP32 Development Board
- Cheap Yellow Display (CYD) - 2.8" ILI9341 320x240 TFT with XPT2046 Touch
- MicroSD card (formatted as FAT32)
- Bluetooth speaker (A2DP compatible)

## Audio Format

WAV files must be:
- **Sample Rate**: 44.1 kHz
- **Bit Depth**: 16-bit
- **Channels**: Stereo (2 channels)
- **Format**: PCM uncompressed

## Installation

1. **Install PlatformIO** (VS Code extension or CLI)

2. **Clone and Build**:
   ```bash
   cd Jingle_Machine
   pio run
   ```

3. **Upload Firmware**:
   ```bash
   pio run --target upload
   ```

4. **Upload Filesystem** (web interface files):
   ```bash
   pio run --target uploadfs
   ```

5. **Prepare SD Card**:
   - Format as FAT32
   - Create `/jingles/` folder
   - Copy WAV files (e.g., `sound1.wav`, `sound2.wav`, etc.)

## First Boot & Startup Flow

The device uses an intelligent startup flow to ensure reliable operation:

### Normal Mode (Default)

Every boot starts in **Normal Mode**:

1. **Bluetooth Connection Attempt** (30 seconds)
   - Display shows countdown timer
   - Connects to configured Bluetooth speaker via MAC address
   - If connection succeeds → Shows button interface

2. **Bluetooth Timeout** (after 30s)
   - If no connection → Sets flag and reboots into Settings Mode
   - Allows you to configure or scan for new speakers

### Settings Mode (Auto-Entry After Timeout)

Automatically entered after BT timeout. Settings Mode performs:

1. **Bluetooth Device Scan** (30 seconds)
   - Scans for nearby Bluetooth speakers
   - Lists all discoverable devices

2. **WiFi Access Point**
   - SSID: `jinglebox`
   - Password: `jingle1234`
   - IP Address: `http://192.168.4.1`

3. **Web Interface Features**:
   - **Configure Bluetooth** - Select speaker from scan results, set volume
   - **Customize Buttons** - Change labels, colors, text rotation, borders, and audio files
   - **Upload Files** - Upload new WAV files directly via web interface
   - **OTA Updates** - Update firmware wirelessly via `/update` endpoint
   - **Exit Settings** - Click to save and return to Normal Mode (device restarts)

### Exiting Settings Mode

Click "Exit Settings Mode" in the web interface. The device will:
- Save configuration to NVS (Non-Volatile Storage)
- Restart into Normal Mode
- Attempt BT connection with new settings

## Pin Configuration

The pin configuration is defined in `include/pin_config.h`:

```cpp
// TFT Display (ILI9341)
TFT_CS   = 15
TFT_DC   = 2
TFT_MOSI = 13
TFT_MISO = 12
TFT_SCLK = 14
TFT_BL   = 21

// Touch (XPT2046)
TOUCH_CS = 33
TOUCH_IRQ = 36

// SD Card
SD_CS = 5
```

Adjust these pins if your CYD board has a different configuration.

## Configuration Storage

Configuration is stored in **NVS (Non-Volatile Storage)** as JSON:

```json
{
  "btDevice": "B8:69:D1:8C:E7:AC",
  "btVolume": 80,
  "rotation": 0,
  "borderColor": "#FFFFFF",
  "borderThickness": 3,
  "buttons": [
    {
      "id": 0,
      "label": "Intro",
      "file": "/jingles/intro.wav",
      "color": "#FF5733",
      "textColor": "#FFFFFF"
    },
    ...
  ]
}
```

### Configuration Options

- **btDevice**: Bluetooth speaker MAC address (e.g., `B8:69:D1:8C:E7:AC`)
- **btVolume**: Volume level 0-100 (default: 80)
- **rotation**: Global text rotation in degrees: `0`, `90`, `180`, `270` (default: 0)
- **borderColor**: Global border color in hex (default: `#FFFFFF`)
- **borderThickness**: Border thickness in pixels 1-5 (default: 3)
- **buttons**: Array of button configurations (max 8)
  - **id**: Button index 0-7
  - **label**: Display text
  - **file**: Path to WAV file on SD card (e.g., `/jingles/sound1.wav`)
  - **color**: Button background color in hex
  - **textColor**: Button text color in hex

## Troubleshooting

### SD Card Error

If you see "SD CARD ERROR" on boot:
- Check SD card is properly inserted
- Ensure SD card is formatted as FAT32
- Verify SD_CS pin is correct

### File Not Found

If a button shows "File not found":
- Check the file exists in `/jingles/` on SD card
- Verify filename matches configuration
- Ensure WAV format is correct (44.1kHz, 16-bit, stereo)

### Bluetooth Connection Issues

If "Waiting for Bluetooth Connection" is shown for 30s:
- Ensure Bluetooth speaker is powered on and in range
- Check speaker is discoverable/pairing mode
- Device will automatically enter Settings Mode after 30s timeout
- In Settings Mode, scan for available speakers and select your device

**Note**: Device uses MAC address pairing for reliability. If your speaker's MAC changes (rare), you'll need to re-scan and re-pair in Settings Mode.

If device immediately goes to Settings Mode on every boot:
- Your configured speaker may be out of range or powered off
- Turn on speaker or select a different speaker in Settings Mode

### Invalid WAV Format

The audio player validates WAV headers. If playback fails:
- Convert file to 44.1kHz, 16-bit, Stereo PCM
- Use a tool like Audacity or FFmpeg:
  ```bash
  ffmpeg -i input.mp3 -ar 44100 -ac 2 -sample_fmt s16 output.wav
  ```

## Serial Monitor

Connect via serial (115200 baud) for debug output:

```bash
pio device monitor
```

## Project Structure

```
Jingle_Machine/
├── platformio.ini           # Build configuration
├── partitions.csv           # ESP32 partition table
├── include/                 # Header files
│   ├── pin_config.h         # Hardware pin definitions
│   ├── audio_player.h       # Bluetooth A2DP audio
│   ├── button_manager.h     # Touch UI system
│   ├── config_manager.h     # Configuration management
│   └── web_server.h         # Web server (normal + settings)
├── src/                     # Source files
│   ├── main.cpp             # Main application
│   ├── audio_player.cpp
│   ├── button_manager.cpp
│   ├── config_manager.cpp
│   └── web_server.cpp
└── data/                    # Web interface (LittleFS)
    ├── index.html
    ├── style.css
    └── main.js
```

## Development

### Simulated Touch (Testing Without Hardware)

For testing without physical touchscreen, enable simulated touch in Normal Mode:

```cpp
// In main.cpp setup():
btnMgr.setSimulatedTouch(true);  // Random touches every 5-10 seconds
```

**IMPORTANT**: Simulated touch generates minimal debug output to avoid audio interference. All `Serial.printf()` calls in touch code must be avoided during audio playback.

### Touch Calibration

Touch coordinates may need calibration. Adjust mapping in `button_manager.cpp`:

```cpp
int x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
int y = map(p.y, 240, 3800, 0, SCREEN_HEIGHT);
```

Test by touching corners and checking serial output.

### Audio Performance Optimization

To maintain glitch-free audio streaming:

1. **WiFi is disabled during Normal Mode** - Reduces RF interference
2. **Main loop does nothing during playback** - Avoids callback interruption
3. **No Serial.printf() during audio** - printf blocks and causes stuttering
4. **Inline functions for performance** - Button helpers are inline to minimize overhead
5. **40MHz SD SPI clock** - Faster SD reads for better streaming
6. **Fade-in/fade-out** - 30ms fade-in, 50ms fade-out to prevent clicks

### Test Modes

Two test modes are available in `main.cpp` (compile-time flags):

```cpp
#define TEST_MODE_LOOP 1      // Loop playback every 10s (no WiFi)
#define TEST_MODE_ROTATION 1  // Cycle through rotations every 3s
```

Enable for testing, disable for production.

### Adding More Buttons

To add more than 8 buttons:

1. Modify `MAX_BUTTONS` constant in `button_manager.h`
2. Adjust `BUTTON_GRID_COLS` and `BUTTON_GRID_ROWS` for layout
3. Update default config in `config_manager.cpp`

### Code Architecture

The ButtonManager class has been refactored for maintainability:
- **Named constants** instead of magic numbers
- **Helper structures** (Point, ButtonBounds, DrawColors) for clarity
- **Inline functions** for performance-critical operations
- **Separated concerns** - Drawing, touch detection, coordinate transformation
- **Validation helpers** for safe operation

This makes the codebase easier to understand, test, and extend.

## Important Notes & Lessons Learned

### Memory Constraints

- **Audio playback is memory-intensive** - Heap fragmentation can occur
- **Sprite-based text rotation failed** - TFT_eSprite creation causes out-of-memory during playback
- **Current solution**: Direct coordinate transformation with display rotation API
- **Heap monitoring recommended** - Use `ESP.getFreeHeap()` and `ESP.getMinFreeHeap()` for debugging

### Performance Considerations

- **Serial output blocks execution** - Avoid `Serial.printf()` during audio callbacks
- **WiFi/BT coexistence** - WiFi disabled in Normal Mode to prevent RF interference
- **Main loop priority** - Returns immediately during audio playback to avoid interrupting callbacks
- **SD card speed matters** - 40MHz SPI clock provides smoother streaming than default 4MHz

### Design Decisions

- **MAC address pairing** - More reliable than device name matching
- **NVS storage** - Preferences library provides simple key-value storage for config
- **Settings Mode reboot** - Clean BT scan requires fresh boot (BT stack reset)
- **30s timeout** - Balances user wait time with connection reliability

## License

This project is provided as-is for personal use.

## Credits

Built with:
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) - Display driver
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) - Bluetooth audio
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) - OTA updates
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Web server
- [ArduinoJson](https://arduinojson.org/) - JSON parsing
