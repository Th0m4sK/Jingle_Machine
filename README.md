# Jingle Machine

ESP32-based sound jingle player with Cheap Yellow Display (CYD), Bluetooth audio output, and web-based configuration.

## Features

- **8 Touch Buttons** - Play different audio jingles with a simple touch
- **Bluetooth Audio** - Streams audio to external Bluetooth speakers (A2DP)
- **SD Card Storage** - WAV files stored on SD card
- **Dual Mode System**:
  - **Normal Mode** - Jingle playback interface
  - **Settings Mode** - Web-based configuration + OTA updates
- **Customizable** - Configure button labels, colors, and audio files via web interface

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

## First Boot

On first boot, the device starts in **Normal Mode** with default configuration.

### Access Web Interface (Normal Mode)

1. Connect to WiFi network:
   - SSID: `jinglebox`
   - Password: `jingle1234`

2. Open browser: `http://192.168.4.1`

3. Click "Enter Settings Mode" to configure

## Settings Mode

In Settings Mode you can:

- **Configure Bluetooth** - Set speaker name and volume
- **Customize Buttons** - Change labels, colors, and audio file assignments
- **Upload Files** - Upload new WAV files directly via web interface
- **OTA Updates** - Update firmware wirelessly via `/update`

### Exiting Settings Mode

Click "Exit Settings Mode" in the web interface to return to Normal Mode. The device will restart.

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

## Configuration File

Button configuration is stored in `/button_config.json` on LittleFS:

```json
{
  "btDevice": "JBL Flip 5",
  "btVolume": 80,
  "buttons": [
    {
      "id": 0,
      "label": "Intro",
      "file": "/jingles/intro.wav",
      "color": "#FF5733"
    },
    ...
  ]
}
```

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

### Bluetooth Disconnected

If "BT: DISC" is shown:
- Ensure Bluetooth speaker is powered on
- Check speaker is in pairing mode
- Verify speaker name matches configuration
- Device will auto-reconnect when speaker is available

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

### Touch Calibration

Touch coordinates may need calibration. Adjust mapping in `button_manager.cpp`:

```cpp
int x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
int y = map(p.y, 240, 3800, 0, SCREEN_HEIGHT);
```

Test by touching corners and checking serial output.

### Adding More Buttons

To add more than 8 buttons:

1. Modify `Button buttons[8]` array size in `button_manager.h`
2. Adjust grid layout in `calculateButtonLayout()`
3. Update default config in `config_manager.cpp`

## License

This project is provided as-is for personal use.

## Credits

Built with:
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) - Display driver
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) - Bluetooth audio
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) - OTA updates
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Web server
- [ArduinoJson](https://arduinojson.org/) - JSON parsing
